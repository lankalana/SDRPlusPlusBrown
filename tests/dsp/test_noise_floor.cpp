// The detection floor: a flat level the user sets, or a spectral estimate measured on demand.
//
// Nothing in here ever starts by itself. That is the point: a floor that re-measures itself
// behind the user's back is impossible to reason about when tuning detection by eye.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <dsp/detector/noise_floor.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace dsp::detector;

TEST_CASE("a manual floor is usable immediately", "[detector][floor]") {
    NoiseFloorModel floor;
    floor.configure(64, 1000.0);
    floor.setMode(NoiseFloorMode::MANUAL);
    floor.setManualFloorDb(-90.0f);
    floor.setMarginDb(10.0f);

    CHECK(floor.isUsable());
    CHECK(floor.getMeasurementState() == MeasurementState::IDLE);
    CHECK(floor.getFloorDb(0) == -90.0f);
    CHECK(floor.getThresholdDb(0) == -80.0f);
    // Flat means flat: every bin reads the same, including out-of-range ones.
    CHECK(floor.getThresholdDb(63) == -80.0f);
    CHECK(floor.getThresholdDb(10000) == -80.0f);
}

TEST_CASE("threshold is floor plus margin and nothing else", "[detector][floor]") {
    NoiseFloorModel floor;
    floor.configure(8, 1000.0);
    floor.setManualFloorDb(-100.0f);

    floor.setMarginDb(0.0f);
    CHECK(floor.getThresholdDb(0) == -100.0f);
    floor.setMarginDb(25.0f);
    CHECK(floor.getThresholdDb(0) == -75.0f);
}

TEST_CASE("a measured floor is unusable until it has been measured", "[detector][floor]") {
    NoiseFloorModel floor;
    floor.configure(64, 1000.0);
    floor.setMode(NoiseFloorMode::MEASURED);

    CHECK_FALSE(floor.isUsable());
    CHECK(floor.getThresholdDb(0) == std::numeric_limits<float>::infinity());

    // Frames are ignored while no measurement is running: nothing measures automatically.
    std::vector<float> frame(64, -100.0f);
    CHECK_FALSE(floor.addFrame(frame.data(), 64));
    CHECK(floor.getMeasurementState() == MeasurementState::IDLE);
    CHECK_FALSE(floor.isUsable());
}

TEST_CASE("a measurement completes after the requested frames", "[detector][floor]") {
    NoiseFloorModel floor;
    floor.configure(64, 1000.0);
    floor.startMeasurement(5);
    CHECK(floor.getMode() == NoiseFloorMode::MEASURED);
    CHECK(floor.getMeasurementState() == MeasurementState::MEASURING);

    std::vector<float> frame(64, -100.0f);
    for (int i = 0; i < 4; i++) {
        CHECK_FALSE(floor.addFrame(frame.data(), 64));
        CHECK(floor.getMeasurementState() == MeasurementState::MEASURING);
    }
    CHECK(floor.addFrame(frame.data(), 64));
    CHECK(floor.getMeasurementState() == MeasurementState::READY);
    CHECK(floor.isUsable());
    CHECK(floor.getFloorDb(0) == Catch::Approx(-100.0f));

    // Further frames are not consumed by a finished measurement.
    CHECK_FALSE(floor.addFrame(frame.data(), 64));
}

TEST_CASE("measurement progress tracks frames seen", "[detector][floor]") {
    NoiseFloorModel floor;
    floor.configure(8, 1000.0);
    floor.startMeasurement(10);
    CHECK(floor.getMeasurementProgress() == 0.0f);

    std::vector<float> frame(8, -100.0f);
    for (int i = 0; i < 5; i++) { floor.addFrame(frame.data(), 8); }
    CHECK(floor.getMeasurementProgress() == Catch::Approx(0.5f));
}

TEST_CASE("a measurement can be cancelled", "[detector][floor]") {
    NoiseFloorModel floor;
    floor.configure(8, 1000.0);
    floor.startMeasurement(100);
    std::vector<float> frame(8, -100.0f);
    floor.addFrame(frame.data(), 8);

    floor.cancelMeasurement();
    CHECK(floor.getMeasurementState() == MeasurementState::IDLE);
    CHECK_FALSE(floor.isUsable());
}

TEST_CASE("a frame of the wrong width aborts the measurement", "[detector][floor]") {
    NoiseFloorModel floor;
    floor.configure(8, 1000.0);
    floor.startMeasurement(10);
    std::vector<float> good(8, -100.0f);
    floor.addFrame(good.data(), 8);

    std::vector<float> bad(16, -100.0f);
    CHECK_FALSE(floor.addFrame(bad.data(), 16));
    CHECK(floor.getMeasurementState() == MeasurementState::IDLE);
}

TEST_CASE("reconfiguring to a new shape discards a measured floor", "[detector][floor]") {
    NoiseFloorModel floor;
    floor.configure(8, 1000.0);
    floor.startMeasurement(2);
    std::vector<float> frame(8, -100.0f);
    floor.addFrame(frame.data(), 8);
    floor.addFrame(frame.data(), 8);
    REQUIRE(floor.isUsable());

    floor.configure(16, 1000.0);
    CHECK_FALSE(floor.isUsable());
    CHECK(floor.getMeasurementState() == MeasurementState::IDLE);
}

TEST_CASE("switching back to manual leaves the user's level intact", "[detector][floor]") {
    NoiseFloorModel floor;
    floor.configure(8, 1000.0);
    floor.setManualFloorDb(-77.0f);
    floor.startMeasurement(50);
    REQUIRE(floor.getMode() == NoiseFloorMode::MEASURED);

    floor.setMode(NoiseFloorMode::MANUAL);
    CHECK(floor.isUsable());
    CHECK(floor.getFloorDb(0) == -77.0f);
    CHECK(floor.getMeasurementState() == MeasurementState::IDLE);
}

// ------------------------------------------------------------ spectral estimator

TEST_CASE("the spectral floor follows a sloping baseline", "[detector][floor]") {
    const int BINS = 1024;
    std::vector<float> frame(BINS);
    for (int b = 0; b < BINS; b++) { frame[b] = -110.0f + (b * 40.0f / (BINS - 1)); }

    std::vector<float> out;
    NoiseFloorModel::spectralFloor(frame.data(), BINS, 128, 0.25f, out);

    REQUIRE(out.size() == BINS);
    // A low percentile over a window sits slightly below the window's centre value, but must
    // track the slope rather than flattening it.
    CHECK(out[100] == Catch::Approx(frame[100]).margin(2.0));
    CHECK(out[900] == Catch::Approx(frame[900]).margin(2.0));
    CHECK(out[900] - out[100] == Catch::Approx(frame[900] - frame[100]).margin(2.0));
}

TEST_CASE("the spectral floor ignores signals narrower than its window", "[detector][floor]") {
    const int BINS = 1024;
    std::vector<float> frame(BINS, -100.0f);
    // A wide, strong transmitter occupying 64 of 1024 bins.
    for (int b = 400; b < 464; b++) { frame[b] = -40.0f; }

    std::vector<float> out;
    NoiseFloorModel::spectralFloor(frame.data(), BINS, 512, 0.25f, out);

    // Inside the transmitter the floor still reads the surrounding noise, so the whole station
    // stands above it as one block. A temporal per-bin estimate would have learned -40 dB here.
    CHECK(out[430] == Catch::Approx(-100.0f).margin(1.0));
    CHECK(out[100] == Catch::Approx(-100.0f).margin(1.0));
}

TEST_CASE("a signal wider than the window drags its own floor up", "[detector][floor]") {
    // Documents the limit that makes the window size the critical setting.
    const int BINS = 1024;
    std::vector<float> frame(BINS, -100.0f);
    for (int b = 200; b < 800; b++) { frame[b] = -40.0f; }

    std::vector<float> out;
    NoiseFloorModel::spectralFloor(frame.data(), BINS, 64, 0.25f, out);
    CHECK(out[500] == Catch::Approx(-40.0f).margin(1.0));
}

TEST_CASE("estimateFlatFloorDb reports the quiet part of a frame", "[detector][floor]") {
    const int BINS = 1000;
    std::vector<float> frame(BINS, -100.0f);
    // A quarter of the frame is occupied by transmitters.
    for (int b = 0; b < 250; b++) { frame[b] = -30.0f; }

    float level = NoiseFloorModel::estimateFlatFloorDb(frame.data(), BINS, 0.25f);
    CHECK(level == Catch::Approx(-100.0f).margin(1.0));
}

TEST_CASE("a measurement averages frames before estimating", "[detector][floor]") {
    const int BINS = 256;
    NoiseFloorModel floor;
    floor.configure(BINS, 1000.0);
    floor.setSpectralWindowHz(128 * 1000.0); // 128 bins
    floor.setSpectralPercentile(0.25f);
    floor.startMeasurement(4);

    // Alternating frames 10 dB apart: the average is the midpoint, so a single noisy frame
    // cannot shape the floor.
    std::vector<float> lo(BINS, -105.0f);
    std::vector<float> hi(BINS, -95.0f);
    floor.addFrame(lo.data(), BINS);
    floor.addFrame(hi.data(), BINS);
    floor.addFrame(lo.data(), BINS);
    REQUIRE(floor.addFrame(hi.data(), BINS));

    CHECK(floor.getFloorDb(128) == Catch::Approx(-100.0f).margin(0.5));
}

// --------------------------------------------------------------------------------------------
// Percentile to mean-power bias.
//
// getFloorDb() is a low percentile, so it sits below the mean noise power. That is harmless for a
// per-bin comparison, where the offset just folds into the margin, but the integrated detector
// divides by noise power -- so the offset has to be known rather than absorbed.
// --------------------------------------------------------------------------------------------

namespace {
    // Exponentially distributed bin power, which is what a single-look power spectrum has.
    struct Lcg {
        uint64_t state;
        explicit Lcg(uint64_t seed) : state(seed) {}
        double next() {
            state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
            return (double)((state >> 11) & ((1ULL << 53) - 1)) / (double)(1ULL << 53);
        }
        // Exp(1), avoiding log(0).
        double exponential() { return -std::log(1.0 - next() * 0.9999999); }
    };
}

TEST_CASE("the single look bias is the analytic value", "[detector][floor]") {
    // For Exp(1) power, the p-th quantile of 10*log10 is 10*log10(-ln(1-p)), so the offset from
    // the mean is -4.3429 * ln(-ln(1-p)). At the 25th percentile that is 5.41 dB.
    CHECK(NoiseFloorModel::percentileToMeanBiasDb(0.25f, 1) == Catch::Approx(5.4114f).margin(0.01));
    // The median sits 1.59 dB below mean power.
    CHECK(NoiseFloorModel::percentileToMeanBiasDb(0.5f, 1) == Catch::Approx(1.5917f).margin(0.01));
    // A lower percentile is further below the mean.
    CHECK(NoiseFloorModel::percentileToMeanBiasDb(0.1f, 1) >
          NoiseFloorModel::percentileToMeanBiasDb(0.25f, 1));
}

TEST_CASE("averaging frames shrinks the bias but never removes it", "[detector][floor]") {
    // More frames narrows the distribution, so the percentile creeps toward the mean.
    float m1 = NoiseFloorModel::percentileToMeanBiasDb(0.25f, 1);
    float m4 = NoiseFloorModel::percentileToMeanBiasDb(0.25f, 4);
    float m20 = NoiseFloorModel::percentileToMeanBiasDb(0.25f, 20);
    CHECK(m1 > m4);
    CHECK(m4 > m20);

    // It converges on 2.507 dB rather than zero, and that floor is not about the percentile at
    // all: the measurement averages *dB* values, which estimates the geometric mean of the power,
    // and for exponential noise that sits 10*log10(e^gamma) = 2.507 dB below the arithmetic mean
    // however many frames are averaged. This is precisely why the detector cannot divide by
    // getFloorDb() and why the frame accumulator had to move to linear power.
    CHECK(m20 > 2.5f);
    CHECK(NoiseFloorModel::percentileToMeanBiasDb(0.5f, 10000) ==
          Catch::Approx(2.507f).margin(0.05));
}

TEST_CASE("the bias recovers mean noise power from a percentile", "[detector][floor]") {
    // Monte-Carlo against synthetic exponential noise: take the percentile of an M-frame dB
    // average, add the bias, and the result must be the true mean power.
    const int BINS = 20000;
    const double TRUE_MEAN_DB = -90.0;
    const double trueMeanLin = std::pow(10.0, TRUE_MEAN_DB / 10.0);

    for (int frames : { 1, 4, 20 }) {
        for (float pct : { 0.1f, 0.25f, 0.5f }) {
            Lcg rng(0x5EED1234u + frames * 100 + (int)(pct * 1000));

            std::vector<float> avgDb(BINS, 0.0f);
            for (int f = 0; f < frames; f++) {
                for (int b = 0; b < BINS; b++) {
                    double lin = trueMeanLin * rng.exponential();
                    avgDb[b] += (float)(10.0 * std::log10(lin));
                }
            }
            for (int b = 0; b < BINS; b++) { avgDb[b] /= (float)frames; }

            float percentileDb = NoiseFloorModel::estimateFlatFloorDb(avgDb.data(), BINS, pct);
            float recovered = percentileDb + NoiseFloorModel::percentileToMeanBiasDb(pct, frames);

            INFO("frames=" << frames << " pct=" << pct << " percentile=" << percentileDb
                           << " recovered=" << recovered);
            CHECK(recovered == Catch::Approx(TRUE_MEAN_DB).margin(0.35));
        }
    }
}

TEST_CASE("getNoisePowerDb sits above the drawn floor", "[detector][floor]") {
    NoiseFloorModel floor;
    floor.configure(64, 1000.0);
    floor.setMode(NoiseFloorMode::MANUAL);
    floor.setManualFloorDb(-100.0f);
    floor.setSpectralPercentile(0.25f);

    // The line the user placed stays exactly where they put it...
    CHECK(floor.getFloorDb(0) == Catch::Approx(-100.0f));
    // ...while the detector normalises by mean noise power, which is above it.
    CHECK(floor.getNoisePowerDb(0) == Catch::Approx(-100.0f + 5.4114f).margin(0.01));
    // The threshold is still floor + margin, untouched by any of this.
    floor.setMarginDb(10.0f);
    CHECK(floor.getThresholdDb(0) == Catch::Approx(-90.0f));
}
