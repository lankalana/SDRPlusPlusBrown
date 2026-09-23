// Grouping above-threshold FFT bins into structured DetectedSignal ranges.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <dsp/detector/noise_floor.h>
#include <dsp/detector/spectrum_detector.h>

#include <vector>

using namespace dsp::detector;

namespace {
    const int BINS = 100;
    const double CENTER = 100e6;
    const double SPAN = 1e6; // 10 kHz per bin

    // A flat -100 dB floor with a 10 dB margin, so the detection threshold is exactly -90 dB.
    NoiseFloorModel flatFloor(int bins = BINS) {
        NoiseFloorModel floor;
        floor.configure(bins, SPAN / bins);
        floor.setMode(NoiseFloorMode::MANUAL);
        floor.setManualFloorDb(-100.0f);
        floor.setMarginDb(10.0f);
        return floor;
    }

    std::vector<float> quietFrame(int bins = BINS) { return std::vector<float>(bins, -100.0f); }

    double binLowEdge(int bin) { return CENTER - (SPAN / 2.0) + (bin * SPAN / BINS); }

    // minBins in bin counts, expressed the way the production config does (Hz).
    double bwOfBins(int bins) { return bins * SPAN / BINS; }
}

TEST_CASE("noise alone produces no detections", "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    DetectionParams params;

    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, params);
    CHECK(signals.empty());
}

TEST_CASE("an unmeasured floor detects nothing", "[detector][grouping]") {
    NoiseFloorModel floor;
    floor.configure(BINS, SPAN / BINS);
    floor.setMode(NoiseFloorMode::MEASURED); // never measured
    auto frame = quietFrame();
    for (auto& v : frame) { v = 0.0f; } // extremely loud everywhere

    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, DetectionParams{});
    CHECK(signals.empty());
}

TEST_CASE("a measured floor of the wrong width detects nothing", "[detector][grouping]") {
    NoiseFloorModel floor;
    floor.configure(BINS, SPAN / BINS);
    floor.startMeasurement(1);
    std::vector<float> calFrame(BINS, -100.0f);
    REQUIRE(floor.addFrame(calFrame.data(), BINS));

    std::vector<float> frame(BINS / 2, 0.0f);
    auto signals = detectSignals(frame.data(), BINS / 2, floor, CENTER, SPAN, DetectionParams{});
    CHECK(signals.empty());
}

TEST_CASE("a flat manual floor applies to any frame width", "[detector][grouping]") {
    // Unlike a measured floor, a flat level is not tied to a bin count.
    auto floor = flatFloor(BINS);
    std::vector<float> frame(BINS / 2, -100.0f);
    for (int b = 10; b <= 15; b++) { frame[b] = -50.0f; }

    auto signals = detectSignals(frame.data(), BINS / 2, floor, CENTER, SPAN, DetectionParams{});
    CHECK(signals.size() == 1);
}

TEST_CASE("contiguous above-threshold bins become one signal", "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    for (int b = 40; b <= 49; b++) { frame[b] = -70.0f; }

    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, DetectionParams{});
    REQUIRE(signals.size() == 1);

    const auto& s = signals[0];
    CHECK(s.lowerFrequency == Catch::Approx(binLowEdge(40)));
    CHECK(s.upperFrequency == Catch::Approx(binLowEdge(50)));
    CHECK(s.bandwidth == Catch::Approx(10 * SPAN / BINS));
    CHECK(s.centerFrequency == Catch::Approx((binLowEdge(40) + binLowEdge(50)) / 2.0));
    CHECK(s.peakDb == Catch::Approx(-70.0f));
    CHECK(s.noiseFloorDb == Catch::Approx(-100.0f));
    CHECK(s.snrDb == Catch::Approx(30.0f));
}

TEST_CASE("separated groups become separate signals", "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    for (int b = 20; b <= 24; b++) { frame[b] = -60.0f; }
    for (int b = 60; b <= 66; b++) { frame[b] = -80.0f; }

    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, DetectionParams{});
    REQUIRE(signals.size() == 2);

    CHECK(signals[0].lowerFrequency == Catch::Approx(binLowEdge(20)));
    CHECK(signals[0].upperFrequency == Catch::Approx(binLowEdge(25)));
    CHECK(signals[0].snrDb == Catch::Approx(40.0f));

    CHECK(signals[1].lowerFrequency == Catch::Approx(binLowEdge(60)));
    CHECK(signals[1].upperFrequency == Catch::Approx(binLowEdge(67)));
    CHECK(signals[1].snrDb == Catch::Approx(20.0f));
}

TEST_CASE("a small gap inside a signal is bridged", "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    for (int b = 30; b <= 39; b++) { frame[b] = -70.0f; }
    frame[35] = -100.0f; // a notch in the middle of one transmission

    DetectionParams params;
    params.mergeGapHz = SPAN / BINS; // one bin
    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, params);
    REQUIRE(signals.size() == 1);
    CHECK(signals[0].lowerFrequency == Catch::Approx(binLowEdge(30)));
    CHECK(signals[0].upperFrequency == Catch::Approx(binLowEdge(40)));
}

TEST_CASE("a gap wider than the tolerance splits the signal", "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    for (int b = 30; b <= 39; b++) { frame[b] = -70.0f; }
    frame[34] = -100.0f;
    frame[35] = -100.0f;
    frame[36] = -100.0f;

    DetectionParams params;
    params.mergeGapHz = SPAN / BINS; // one bin
    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, params);
    REQUIRE(signals.size() == 2);
    CHECK(signals[0].upperFrequency == Catch::Approx(binLowEdge(34)));
    CHECK(signals[1].lowerFrequency == Catch::Approx(binLowEdge(37)));
}

TEST_CASE("runs narrower than minBins are discarded", "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    frame[50] = -50.0f; // a single loud bin

    DetectionParams params;
    params.minBins = 2;
    CHECK(detectSignals(frame.data(), BINS, floor, CENTER, SPAN, params).empty());

    params.minBins = 1;
    CHECK(detectSignals(frame.data(), BINS, floor, CENTER, SPAN, params).size() == 1);
}

TEST_CASE("only the usable part of the spectrum is searched", "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    // Right at the bottom edge of the capture, inside the filter roll-off.
    for (int b = 0; b <= 5; b++) { frame[b] = -50.0f; }
    // Comfortably in the middle.
    for (int b = 48; b <= 53; b++) { frame[b] = -50.0f; }

    DetectionParams params;
    params.usableSpectrumRatio = 0.8; // keeps bins 10..89
    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, params);
    REQUIRE(signals.size() == 1);
    CHECK(signals[0].lowerFrequency == Catch::Approx(binLowEdge(48)));
}

TEST_CASE("a signal running to the edge of the usable window is still reported",
          "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    for (int b = 90; b < BINS; b++) { frame[b] = -50.0f; }

    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, DetectionParams{});
    REQUIRE(signals.size() == 1);
    CHECK(signals[0].lowerFrequency == Catch::Approx(binLowEdge(90)));
    CHECK(signals[0].upperFrequency == Catch::Approx(CENTER + (SPAN / 2.0)));
}

TEST_CASE("a measured floor follows the local slope, not a global constant",
          "[detector][grouping]") {
    // Floor sloping from -110 dB to -70 dB across the capture.
    const int bins = 1024;
    const double span = 1e6;
    NoiseFloorModel floor;
    floor.configure(bins, span / bins);
    floor.setSpectralWindowHz(span / 8.0);
    floor.setMarginDb(10.0f);
    floor.startMeasurement(1);

    std::vector<float> base(bins);
    for (int b = 0; b < bins; b++) { base[b] = -110.0f + (b * 40.0f / (bins - 1)); }
    REQUIRE(floor.addFrame(base.data(), bins));

    // -85 dB is far above the floor at the low end and far below it at the high end, so a single
    // global threshold could not separate these two.
    std::vector<float> frame = base;
    for (int b = 50; b <= 80; b++) { frame[b] = -85.0f; }
    for (int b = 940; b <= 970; b++) { frame[b] = -85.0f; }

    auto signals = detectSignals(frame.data(), bins, floor, CENTER, span, DetectionParams{});
    REQUIRE(signals.size() == 1);
    CHECK(signals[0].centerFrequency < CENTER);
}

// ------------------------------------------------- bandwidth limits, expressed in Hz

TEST_CASE("signals narrower than the minimum bandwidth are rejected", "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    for (int b = 20; b <= 22; b++) { frame[b] = -50.0f; } // 30 kHz
    for (int b = 60; b <= 79; b++) { frame[b] = -50.0f; } // 200 kHz

    DetectionParams params;
    params.minBandwidthHz = 100e3;
    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, params);
    REQUIRE(signals.size() == 1);
    CHECK(signals[0].bandwidth == Catch::Approx(bwOfBins(20)));
}

TEST_CASE("signals wider than the maximum bandwidth are rejected", "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    for (int b = 10; b <= 14; b++) { frame[b] = -50.0f; } // 50 kHz
    for (int b = 40; b <= 79; b++) { frame[b] = -50.0f; } // 400 kHz

    DetectionParams params;
    params.maxBandwidthHz = 100e3;
    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, params);
    REQUIRE(signals.size() == 1);
    CHECK(signals[0].bandwidth == Catch::Approx(bwOfBins(5)));
}

TEST_CASE("a maximum bandwidth of zero means no upper limit", "[detector][grouping]") {
    auto floor = flatFloor();
    auto frame = quietFrame();
    for (int b = 10; b <= 89; b++) { frame[b] = -50.0f; }

    DetectionParams params;
    params.maxBandwidthHz = 0.0;
    CHECK(detectSignals(frame.data(), BINS, floor, CENTER, SPAN, params).size() == 1);
}

TEST_CASE("a wide merge gap keeps a notched transmission whole", "[detector][grouping]") {
    // What a single frame of broadcast FM looks like: one channel, deeply notched.
    auto floor = flatFloor();
    auto frame = quietFrame();
    for (int b = 30; b <= 47; b++) { frame[b] = -50.0f; }
    frame[35] = frame[36] = frame[40] = frame[41] = frame[42] = -100.0f;

    DetectionParams noBridge;
    CHECK(detectSignals(frame.data(), BINS, floor, CENTER, SPAN, noBridge).size() == 3);

    DetectionParams bridged;
    bridged.mergeGapHz = 50e3; // 5 bins
    auto signals = detectSignals(frame.data(), BINS, floor, CENTER, SPAN, bridged);
    REQUIRE(signals.size() == 1);
    CHECK(signals[0].bandwidth == Catch::Approx(bwOfBins(18)));
}
