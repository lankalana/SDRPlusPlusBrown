// Grouping above-threshold FFT bins into structured DetectedSignal ranges.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <dsp/detector/noise_calibration.h>
#include <dsp/detector/spectrum_detector.h>

#include <vector>

using namespace dsp::detector;

namespace {
    const int BINS = 100;
    const double CENTER = 100e6;
    const double SPAN = 1e6; // 10 kHz per bin

    // A calibration with a perfectly flat -100 dB floor and a 10 dB margin, so the detection
    // threshold is exactly -90 dB in every bin.
    NoiseFloorCalibration flatCalibration(int bins = BINS) {
        NoiseFloorCalibration cal;
        cal.configure(bins, 10);
        cal.setMinimumMarginDb(10.0f);
        cal.begin();
        std::vector<float> frame(bins, -100.0f);
        for (int i = 0; i < 10; i++) { cal.addFrame(frame.data(), bins); }
        return cal;
    }

    std::vector<float> quietFrame(int bins = BINS) { return std::vector<float>(bins, -100.0f); }

    double binLowEdge(int bin) { return CENTER - (SPAN / 2.0) + (bin * SPAN / BINS); }
}

TEST_CASE("noise alone produces no detections", "[detector][grouping]") {
    auto cal = flatCalibration();
    auto frame = quietFrame();
    DetectionParams params;

    auto signals = detectSignals(frame.data(), BINS, cal, CENTER, SPAN, params);
    CHECK(signals.empty());
}

TEST_CASE("an uncalibrated model detects nothing", "[detector][grouping]") {
    NoiseFloorCalibration cal; // never calibrated
    auto frame = quietFrame();
    for (auto& v : frame) { v = 0.0f; } // extremely loud everywhere

    auto signals = detectSignals(frame.data(), BINS, cal, CENTER, SPAN, DetectionParams{});
    CHECK(signals.empty());
}

TEST_CASE("a calibration of the wrong width detects nothing", "[detector][grouping]") {
    auto cal = flatCalibration(BINS);
    std::vector<float> frame(BINS / 2, 0.0f);

    auto signals = detectSignals(frame.data(), BINS / 2, cal, CENTER, SPAN, DetectionParams{});
    CHECK(signals.empty());
}

TEST_CASE("contiguous above-threshold bins become one signal", "[detector][grouping]") {
    auto cal = flatCalibration();
    auto frame = quietFrame();
    for (int b = 40; b <= 49; b++) { frame[b] = -70.0f; }

    auto signals = detectSignals(frame.data(), BINS, cal, CENTER, SPAN, DetectionParams{});
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
    auto cal = flatCalibration();
    auto frame = quietFrame();
    for (int b = 20; b <= 24; b++) { frame[b] = -60.0f; }
    for (int b = 60; b <= 66; b++) { frame[b] = -80.0f; }

    auto signals = detectSignals(frame.data(), BINS, cal, CENTER, SPAN, DetectionParams{});
    REQUIRE(signals.size() == 2);

    CHECK(signals[0].lowerFrequency == Catch::Approx(binLowEdge(20)));
    CHECK(signals[0].upperFrequency == Catch::Approx(binLowEdge(25)));
    CHECK(signals[0].snrDb == Catch::Approx(40.0f));

    CHECK(signals[1].lowerFrequency == Catch::Approx(binLowEdge(60)));
    CHECK(signals[1].upperFrequency == Catch::Approx(binLowEdge(67)));
    CHECK(signals[1].snrDb == Catch::Approx(20.0f));
}

TEST_CASE("a small gap inside a signal is bridged", "[detector][grouping]") {
    auto cal = flatCalibration();
    auto frame = quietFrame();
    for (int b = 30; b <= 39; b++) { frame[b] = -70.0f; }
    frame[35] = -100.0f; // a notch in the middle of one transmission

    DetectionParams params;
    params.maxGapBins = 1;
    auto signals = detectSignals(frame.data(), BINS, cal, CENTER, SPAN, params);
    REQUIRE(signals.size() == 1);
    CHECK(signals[0].lowerFrequency == Catch::Approx(binLowEdge(30)));
    CHECK(signals[0].upperFrequency == Catch::Approx(binLowEdge(40)));
}

TEST_CASE("a gap wider than the tolerance splits the signal", "[detector][grouping]") {
    auto cal = flatCalibration();
    auto frame = quietFrame();
    for (int b = 30; b <= 39; b++) { frame[b] = -70.0f; }
    frame[34] = -100.0f;
    frame[35] = -100.0f;
    frame[36] = -100.0f;

    DetectionParams params;
    params.maxGapBins = 1;
    auto signals = detectSignals(frame.data(), BINS, cal, CENTER, SPAN, params);
    REQUIRE(signals.size() == 2);
    CHECK(signals[0].upperFrequency == Catch::Approx(binLowEdge(34)));
    CHECK(signals[1].lowerFrequency == Catch::Approx(binLowEdge(37)));
}

TEST_CASE("runs narrower than minBins are discarded", "[detector][grouping]") {
    auto cal = flatCalibration();
    auto frame = quietFrame();
    frame[50] = -50.0f; // a single loud bin

    DetectionParams params;
    params.minBins = 2;
    CHECK(detectSignals(frame.data(), BINS, cal, CENTER, SPAN, params).empty());

    params.minBins = 1;
    CHECK(detectSignals(frame.data(), BINS, cal, CENTER, SPAN, params).size() == 1);
}

TEST_CASE("only the usable part of the spectrum is searched", "[detector][grouping]") {
    auto cal = flatCalibration();
    auto frame = quietFrame();
    // Right at the bottom edge of the capture, inside the filter roll-off.
    for (int b = 0; b <= 5; b++) { frame[b] = -50.0f; }
    // Comfortably in the middle.
    for (int b = 48; b <= 53; b++) { frame[b] = -50.0f; }

    DetectionParams params;
    params.usableSpectrumRatio = 0.8; // keeps bins 10..89
    auto signals = detectSignals(frame.data(), BINS, cal, CENTER, SPAN, params);
    REQUIRE(signals.size() == 1);
    CHECK(signals[0].lowerFrequency == Catch::Approx(binLowEdge(48)));
}

TEST_CASE("a signal running to the edge of the usable window is still reported",
          "[detector][grouping]") {
    auto cal = flatCalibration();
    auto frame = quietFrame();
    for (int b = 90; b < BINS; b++) { frame[b] = -50.0f; }

    auto signals = detectSignals(frame.data(), BINS, cal, CENTER, SPAN, DetectionParams{});
    REQUIRE(signals.size() == 1);
    CHECK(signals[0].lowerFrequency == Catch::Approx(binLowEdge(90)));
    CHECK(signals[0].upperFrequency == Catch::Approx(CENTER + (SPAN / 2.0)));
}

TEST_CASE("the reported floor follows the calibrated slope, not a global constant",
          "[detector][grouping]") {
    // Floor sloping from -110 dB to -70 dB; flat 10 dB margin on top.
    const int bins = 40;
    NoiseFloorCalibration cal;
    cal.configure(bins, 10);
    cal.setMinimumMarginDb(10.0f);
    cal.begin();
    std::vector<float> calFrame(bins);
    for (int b = 0; b < bins; b++) { calFrame[b] = -110.0f + (b * 40.0f / (bins - 1)); }
    for (int i = 0; i < 10; i++) { cal.addFrame(calFrame.data(), bins); }
    REQUIRE(cal.isReady());

    // -85 dB would be far above the floor at the low end and far below it at the high end.
    std::vector<float> frame = calFrame;
    for (int b = 2; b <= 5; b++) { frame[b] = -85.0f; }
    for (int b = 34; b <= 37; b++) { frame[b] = -85.0f; }

    auto signals = detectSignals(frame.data(), bins, cal, CENTER, SPAN, DetectionParams{});
    REQUIRE(signals.size() == 1);
    // Only the low-end one clears its local threshold.
    CHECK(signals[0].centerFrequency < CENTER);
    CHECK(signals[0].snrDb > 15.0f);
}
