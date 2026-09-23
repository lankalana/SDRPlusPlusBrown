// detectPass(): greatest-of integration, channelized and grouped.
//
// Parameterised over two geometries on purpose, because the raster-to-bandwidth ratio inverts
// between the bands this has to serve:
//
//   airband      25 kHz raster, 10 kHz passband  -- carrier-dominated, narrow kernel wins
//   broadcast FM 100 kHz raster, 150 kHz passband -- fills the channel, wide kernel wins
//
// A detector tuned for either one alone gets the other badly wrong.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <dsp/detector/noise_floor.h>
#include <dsp/detector/spectrum_detector.h>
#include <dsp/detector/spectrum_integrator.h>

#include <cmath>
#include <vector>

using namespace dsp::detector;

namespace {
    const int BINS = 2048;
    const double CENTER = 100e6;
    const double SPAN = 200e3;              // 97.65625 Hz per bin
    const double BIN_HZ = SPAN / BINS;
    const float FLOOR_DB = -100.0f;

    NoiseFloorModel flatFloor() {
        NoiseFloorModel floor;
        floor.configure(BINS, BIN_HZ);
        floor.setMode(NoiseFloorMode::MANUAL);
        floor.setManualFloorDb(FLOOR_DB);
        return floor;
    }

    double noiseLin() {
        return std::pow(10.0, (double)flatFloor().getNoisePowerDb(0) / 10.0);
    }

    int binOf(double frequency) {
        return (int)std::lround(((frequency - (CENTER - (SPAN / 2.0))) / BIN_HZ) - 0.5);
    }

    // Flat noise with a rectangular signal of `snrPerBinDb` over `widthHz` centred on `frequency`.
    std::vector<float> spectrumWith(double frequency, double widthHz, double snrPerBinDb) {
        std::vector<float> power(BINS, (float)noiseLin());
        int half = std::max<int>(0, (int)std::lround((widthHz / 2.0) / BIN_HZ));
        int c = binOf(frequency);
        double lin = noiseLin() * std::pow(10.0, snrPerBinDb / 10.0);
        for (int b = c - half; b <= c + half; b++) {
            if (b >= 0 && b < BINS) { power[b] = (float)lin; }
        }
        return power;
    }

    void addSignal(std::vector<float>& power, double frequency, double widthHz,
                   double snrPerBinDb) {
        int half = std::max<int>(0, (int)std::lround((widthHz / 2.0) / BIN_HZ));
        int c = binOf(frequency);
        double lin = noiseLin() * std::pow(10.0, snrPerBinDb / 10.0);
        for (int b = c - half; b <= c + half; b++) {
            if (b >= 0 && b < BINS) { power[b] = (float)lin; }
        }
    }

    PassParams channelParams(double stepHz, double bandwidthHz) {
        PassParams p;
        p.loBin = 0;
        p.hiBin = BINS;
        p.kernelBins = kernelSetFor(bandwidthHz, BIN_HZ, BINS);
        p.marginDb = 10.0f;
        p.channelStepHz = stepHz;
        p.channelBandwidthHz = bandwidthHz;
        return p;
    }

    std::vector<DetectedSignal> run(const std::vector<float>& power, const PassParams& params,
                                    const std::vector<SpectrumIntegrator::Excision>& ex = {}) {
        NoiseFloorModel floor = flatFloor();
        SpectrumIntegrator integ;
        integ.build(power.data(), BINS, floor, ex);
        return detectPass(integ, CENTER, SPAN, params);
    }
}

TEST_CASE("kernelSetFor spans carrier to channel width", "[detector][channel]") {
    // 10 kHz at ~98 Hz/bin: bw/32 clamps to the 4 bin minimum, bw fills the channel.
    auto air = kernelSetFor(10e3, BIN_HZ, BINS);
    REQUIRE(air.size() >= 2);
    CHECK(air.front() == 4);
    CHECK(air.back() == (int)std::lround(10e3 / BIN_HZ));

    // Ascending and deduplicated.
    for (size_t i = 1; i < air.size(); i++) { CHECK(air[i] > air[i - 1]); }

    // A narrow SSB channel collapses toward the 4 bin floor rather than producing 1 bin kernels.
    auto ssb = kernelSetFor(2.8e3, BIN_HZ, BINS);
    for (int k : ssb) { CHECK(k >= 4); }

    // Never wider than the pass.
    auto clamped = kernelSetFor(150e3, BIN_HZ, 32);
    for (int k : clamped) { CHECK(k <= 32); }
}

TEST_CASE("a carrier on a channel centre gives exactly one detection", "[detector][channel]") {
    // Airband-like: 25 kHz raster, 10 kHz passband, a narrow carrier.
    auto power = spectrumWith(100.0e6, 300.0, 20.0);
    auto found = run(power, channelParams(25e3, 10e3));

    REQUIRE(found.size() == 1);
    CHECK(found[0].channelFrequency == Catch::Approx(100.0e6));
    CHECK(found[0].channelIndex == 4000);
    CHECK(found[0].bandwidth == Catch::Approx(10e3));
    // The signal itself is far narrower than the channel it sits in.
    CHECK(found[0].occupiedBandwidth < 2e3);
}

TEST_CASE("a carrier off centre snaps to the nearest channel", "[detector][channel]") {
    // 8 kHz above a channel centre, still inside the same 25 kHz slot.
    auto power = spectrumWith(100.008e6, 300.0, 20.0);
    auto found = run(power, channelParams(25e3, 10e3));

    REQUIRE(found.size() == 1);
    CHECK(found[0].channelFrequency == Catch::Approx(100.0e6));
}

TEST_CASE("the channel frequency is bit-exact across frames", "[detector][channel]") {
    // Identity depends on exact equality, so the same channel must produce the same double no
    // matter where inside it the signal sits.
    auto a = run(spectrumWith(100.002e6, 300.0, 20.0), channelParams(25e3, 10e3));
    auto b = run(spectrumWith(100.009e6, 300.0, 20.0), channelParams(25e3, 10e3));
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(a[0].channelFrequency == b[0].channelFrequency);
    CHECK(a[0].channelIndex == b[0].channelIndex);
}

TEST_CASE("two genuinely adjacent channels give two detections", "[detector][channel]") {
    // The case a fixed non-maximum-suppression radius would have destroyed.
    std::vector<float> power(BINS, (float)noiseLin());
    addSignal(power, 100.0e6, 300.0, 20.0);
    addSignal(power, 100.025e6, 300.0, 20.0);

    auto found = run(power, channelParams(25e3, 10e3));
    REQUIRE(found.size() == 2);
    CHECK(found[0].channelIndex != found[1].channelIndex);
    CHECK(std::abs(found[0].channelIndex - found[1].channelIndex) == 1);
}

TEST_CASE("one wide station is not reported as two adjacent channels", "[detector][channel]") {
    // FM-like: a 150 kHz emission on a 100 kHz raster is wider than the channel spacing, so it
    // carries several local maxima. They are one station, not two.
    auto power = spectrumWith(100.0e6, 60e3, 15.0);
    PassParams p = channelParams(20e3, 30e3);
    auto found = run(power, p);

    REQUIRE(found.size() == 1);
    CHECK(found[0].channelFrequency == Catch::Approx(100.0e6));
}

TEST_CASE("a signal far wider than the channel is rejected as an intruder",
          "[detector][channel]") {
    // A wideband blast across the band is not a 10 kHz channel.
    auto wide = run(spectrumWith(100.0e6, 120e3, 12.0), channelParams(25e3, 10e3));
    CHECK(wide.empty());

    // ...while something that fits is accepted.
    auto fits = run(spectrumWith(100.0e6, 6e3, 12.0), channelParams(25e3, 10e3));
    CHECK(fits.size() == 1);
}

TEST_CASE("a channel whose window runs off the frame edge is not evaluated",
          "[detector][channel]") {
    // A truncated kernel has a wider noise distribution, so evaluating it would manufacture edge
    // detections rather than measure one.
    auto power = spectrumWith(CENTER - (SPAN / 2.0) + 200.0, 300.0, 30.0);
    auto found = run(power, channelParams(25e3, 10e3));
    for (const auto& s : found) {
        CHECK(s.lowerFrequency >= CENTER - (SPAN / 2.0) - 1.0);
    }
}

TEST_CASE("nothing is detected in pure noise", "[detector][channel]") {
    std::vector<float> power(BINS, (float)noiseLin());
    CHECK(run(power, channelParams(25e3, 10e3)).empty());

    PassParams grouped;
    grouped.loBin = 0;
    grouped.hiBin = BINS;
    grouped.kernelBins = { 4, 16, 64, 256 };
    grouped.marginDb = 10.0f;
    CHECK(run(power, grouped).empty());
}

TEST_CASE("an LO spike is excised along with its kernel neighbourhood",
          "[detector][channel]") {
    // Skipping the spiked bins at evaluation time would not be enough: a wide kernel centred
    // hundreds of bins away still integrates the spike.
    std::vector<float> power(BINS, (float)noiseLin());
    int c = BINS / 2;
    for (int b = c - 2; b <= c + 2; b++) { power[b] = (float)(noiseLin() * 1e6); }

    PassParams grouped;
    grouped.loBin = 0;
    grouped.hiBin = BINS;
    grouped.kernelBins = { 4, 16, 64, 256 };
    grouped.marginDb = 10.0f;

    CHECK_FALSE(run(power, grouped).empty()); // without excision it is found
    CHECK(run(power, grouped, { { c - 4, c + 4 } }).empty()); // with excision, nothing anywhere
}

TEST_CASE("a narrow carrier survives a wide kernel in the set", "[detector][grouping]") {
    // The regression that guards greatest-of against a single channel-width kernel: integrating
    // a carrier over the whole channel buries it, so the narrow kernel has to be consulted too.
    auto power = spectrumWith(100.0e6, 200.0, 14.0);

    PassParams narrowOnly;
    narrowOnly.loBin = 0;
    narrowOnly.hiBin = BINS;
    narrowOnly.kernelBins = { 4 };
    narrowOnly.marginDb = 10.0f;
    REQUIRE_FALSE(run(power, narrowOnly).empty());

    PassParams wideOnly = narrowOnly;
    wideOnly.kernelBins = { 256 };
    CHECK(run(power, wideOnly).empty());

    PassParams both = narrowOnly;
    both.kernelBins = { 4, 16, 64, 256 };
    CHECK_FALSE(run(power, both).empty());
}

TEST_CASE("a wide weak signal needs a wide kernel to be reachable", "[detector][grouping]") {
    // The other direction, and the reason a narrow-only kernel set is not enough.
    //
    // Note what integration does and does not buy. For energy spread flat over K bins, the SNR
    // integrated over those K bins equals the per-bin SNR -- integration adds no gain against a
    // fixed margin. What it changes is the *noise distribution*: over many bins the statistic is
    // tight, so a low margin is statistically safe, while over four bins CFAR still demands about
    // 10 dB no matter what margin is asked for. A weak wide signal is therefore reachable only
    // through a wide kernel.
    auto power = spectrumWith(100.0e6, 25e3, 3.0);

    PassParams narrowOnly;
    narrowOnly.loBin = 0;
    narrowOnly.hiBin = BINS;
    narrowOnly.kernelBins = { 4 };
    narrowOnly.marginDb = 2.0f;
    CHECK(run(power, narrowOnly).empty());

    PassParams both = narrowOnly;
    both.kernelBins = { 4, 16, 64, 256 };
    CHECK_FALSE(run(power, both).empty());
}

TEST_CASE("CFAR keeps a narrow kernel honest at a low margin", "[detector][grouping]") {
    // Asking for a 2 dB margin must not make a four bin kernel fire on noise: with so few
    // independent samples the noise distribution alone reaches well past 2 dB.
    std::vector<float> noiseOnly(BINS, (float)noiseLin());

    PassParams p;
    p.loBin = 0;
    p.hiBin = BINS;
    p.kernelBins = { 4 };
    p.marginDb = 2.0f;
    CHECK(run(noiseOnly, p).empty());

    CHECK(cfarRatio(4.0 / 2.02, 1e-7) > std::pow(10.0, 0.2));
}

TEST_CASE("grouped mode reports the occupied extent, not the flagged run",
          "[detector][grouping]") {
    // The run is as wide as the widest kernel that fired; the signal is not.
    auto power = spectrumWith(100.0e6, 5e3, 15.0);

    PassParams p;
    p.loBin = 0;
    p.hiBin = BINS;
    p.kernelBins = { 4, 16, 64, 256 };
    p.marginDb = 10.0f;

    auto found = run(power, p);
    REQUIRE(found.size() == 1);
    // Within a couple of bins of the true 5 kHz.
    CHECK(found[0].bandwidth == Catch::Approx(5e3).margin(4 * BIN_HZ));
}

TEST_CASE("a detection never reports less SNR than the margin that admitted it",
          "[detector][grouping]") {
    auto power = spectrumWith(100.0e6, 300.0, 20.0);

    for (float margin : { 6.0f, 10.0f, 14.0f }) {
        PassParams p = channelParams(25e3, 10e3);
        p.marginDb = margin;
        auto found = run(power, p);
        if (found.empty()) { continue; }
        CHECK(found[0].snrDb >= margin);
    }
}

TEST_CASE("cfarRatio falls with more looks and rises with a stricter rate",
          "[detector][channel]") {
    // More independent samples means a tighter noise distribution, so less headroom is needed.
    CHECK(cfarRatio(4.0, 1e-7) > cfarRatio(400.0, 1e-7));
    // A rarer false alarm demands more.
    CHECK(cfarRatio(16.0, 1e-9) > cfarRatio(16.0, 1e-3));
    // It is a power ratio, so never below unity.
    CHECK(cfarRatio(10000.0, 0.5) >= 1.0);
}
