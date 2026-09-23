// Per-bin noise floor calibration.
//
// The point of the model is that it is per-bin and robust: a spectrum whose floor slopes across
// the capture must still be thresholded correctly, and a transmission that happens to be on the
// air during calibration must not drag its bins' baseline up with it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <dsp/detector/noise_calibration.h>

#include <cmath>
#include <limits>
#include <vector>

using namespace dsp::detector;

namespace {
    // Deterministic pseudo-noise, so a failure is always reproducible.
    struct Lcg {
        uint32_t s = 12345;
        float next() {
            s = s * 1664525u + 1013904223u;
            return ((float)(s >> 8) / (float)(1 << 24)) - 0.5f; // -0.5 .. 0.5
        }
    };

    // Feed `frames` frames whose bin b sits at floorDb(b) plus a little noise.
    template <typename FloorFn>
    void feedNoise(NoiseFloorCalibration& cal, int bins, int frames, FloorFn floorDb,
                   float noiseAmplitudeDb = 2.0f) {
        Lcg rng;
        std::vector<float> frame(bins);
        for (int f = 0; f < frames; f++) {
            for (int b = 0; b < bins; b++) {
                frame[b] = floorDb(b) + (rng.next() * noiseAmplitudeDb);
            }
            cal.addFrame(frame.data(), bins);
        }
    }
}

TEST_CASE("calibration starts uncalibrated and ignores frames", "[detector][calibration]") {
    NoiseFloorCalibration cal;
    CHECK(cal.getState() == CalibrationState::UNCALIBRATED);

    std::vector<float> frame(16, -90.0f);
    CHECK_FALSE(cal.addFrame(frame.data(), 16));
    CHECK(cal.getState() == CalibrationState::UNCALIBRATED);

    // An uncalibrated model must never let anything through.
    CHECK(cal.getThresholdDb(0) == std::numeric_limits<float>::infinity());
}

TEST_CASE("calibration reaches READY after exactly the required frames", "[detector][calibration]") {
    NoiseFloorCalibration cal;
    cal.configure(8, 10);
    cal.begin();
    CHECK(cal.getState() == CalibrationState::CALIBRATING);

    std::vector<float> frame(8, -100.0f);
    for (int i = 0; i < 9; i++) {
        CHECK_FALSE(cal.addFrame(frame.data(), 8));
        CHECK(cal.getState() == CalibrationState::CALIBRATING);
    }

    // The tenth frame is the one that completes it.
    CHECK(cal.addFrame(frame.data(), 8));
    CHECK(cal.getState() == CalibrationState::READY);
    CHECK(cal.getProgress() == 1.0f);

    // Further frames are not consumed by a finished calibration.
    CHECK_FALSE(cal.addFrame(frame.data(), 8));
    CHECK(cal.getState() == CalibrationState::READY);
}

TEST_CASE("calibration progress tracks frames seen", "[detector][calibration]") {
    NoiseFloorCalibration cal;
    cal.configure(4, 100);
    cal.begin();
    CHECK(cal.getProgress() == 0.0f);

    std::vector<float> frame(4, -100.0f);
    for (int i = 0; i < 25; i++) { cal.addFrame(frame.data(), 4); }
    CHECK(cal.getProgress() == Catch::Approx(0.25f));
}

TEST_CASE("baseline follows a sloping noise floor per bin", "[detector][calibration]") {
    const int BINS = 64;
    NoiseFloorCalibration cal;
    cal.configure(BINS, 100);
    cal.begin();

    // A floor sloping from -100 dB to -60 dB across the capture.
    auto floorDb = [](int b) { return -100.0f + (b * 40.0f / 63.0f); };
    feedNoise(cal, BINS, 100, floorDb);

    REQUIRE(cal.isReady());
    for (int b = 0; b < BINS; b++) {
        CHECK(cal.getBaselineDb(b) == Catch::Approx(floorDb(b)).margin(1.0));
    }

    // A single global threshold would be wrong at one end or the other; a per-bin one is not.
    CHECK(cal.getThresholdDb(0) < cal.getThresholdDb(BINS - 1) - 30.0f);
}

TEST_CASE("a transmission during calibration does not raise its baseline", "[detector][calibration]") {
    const int BINS = 32;
    const int FRAMES = 100;
    NoiseFloorCalibration cal;
    cal.configure(BINS, FRAMES);
    cal.begin();

    Lcg rng;
    std::vector<float> frame(BINS);
    for (int f = 0; f < FRAMES; f++) {
        for (int b = 0; b < BINS; b++) { frame[b] = -100.0f + (rng.next() * 2.0f); }
        // Bin 10 is transmitting hard for the first third of the calibration.
        if (f < FRAMES / 3) { frame[10] = -20.0f; }
        cal.addFrame(frame.data(), BINS);
    }

    REQUIRE(cal.isReady());
    // The median ignores the minority of loud frames; a mean would sit near -73 dB.
    CHECK(cal.getBaselineDb(10) == Catch::Approx(-100.0f).margin(2.0));
    // ...and the signal therefore still clears the threshold.
    CHECK(cal.getThresholdDb(10) < -40.0f);
}

TEST_CASE("threshold honours the minimum margin on a quiet bin", "[detector][calibration]") {
    NoiseFloorCalibration cal;
    cal.configure(4, 20);
    cal.setMinimumMarginDb(10.0f);
    cal.begin();

    // Perfectly flat: deviation is zero, so only the minimum margin applies.
    std::vector<float> frame(4, -100.0f);
    for (int i = 0; i < 20; i++) { cal.addFrame(frame.data(), 4); }

    REQUIRE(cal.isReady());
    CHECK(cal.getThresholdDb(0) == Catch::Approx(-90.0f));
}

TEST_CASE("threshold widens on a noisy bin", "[detector][calibration]") {
    const int BINS = 2;
    NoiseFloorCalibration cal;
    cal.configure(BINS, 200);
    cal.setMinimumMarginDb(1.0f); // out of the way, so the deviation term dominates
    cal.setDeviationMultiplier(3.0f);
    cal.begin();

    Lcg rng;
    std::vector<float> frame(BINS);
    for (int f = 0; f < 200; f++) {
        frame[0] = -100.0f;                        // dead quiet
        frame[1] = -100.0f + (rng.next() * 20.0f); // +/- 10 dB
        cal.addFrame(frame.data(), BINS);
    }

    REQUIRE(cal.isReady());
    CHECK(cal.getThresholdDb(0) == Catch::Approx(-99.0f));
    CHECK(cal.getThresholdDb(1) > cal.getThresholdDb(0) + 10.0f);
}

TEST_CASE("changing the margin re-derives thresholds without recalibrating", "[detector][calibration]") {
    NoiseFloorCalibration cal;
    cal.configure(2, 10);
    cal.setMinimumMarginDb(10.0f);
    cal.begin();
    std::vector<float> frame(2, -100.0f);
    for (int i = 0; i < 10; i++) { cal.addFrame(frame.data(), 2); }
    REQUIRE(cal.isReady());
    REQUIRE(cal.getThresholdDb(0) == Catch::Approx(-90.0f));

    cal.setMinimumMarginDb(20.0f);
    CHECK(cal.isReady());
    CHECK(cal.getThresholdDb(0) == Catch::Approx(-80.0f));
}

TEST_CASE("a frame of the wrong width aborts the calibration", "[detector][calibration]") {
    NoiseFloorCalibration cal;
    cal.configure(8, 10);
    cal.begin();

    std::vector<float> good(8, -100.0f);
    cal.addFrame(good.data(), 8);
    REQUIRE(cal.getState() == CalibrationState::CALIBRATING);

    // The FFT size changed underneath us: the partial model maps to nothing.
    std::vector<float> bad(16, -100.0f);
    CHECK_FALSE(cal.addFrame(bad.data(), 16));
    CHECK(cal.getState() == CalibrationState::UNCALIBRATED);
}

TEST_CASE("reset discards a finished model", "[detector][calibration]") {
    NoiseFloorCalibration cal;
    cal.configure(4, 5);
    cal.begin();
    std::vector<float> frame(4, -100.0f);
    for (int i = 0; i < 5; i++) { cal.addFrame(frame.data(), 4); }
    REQUIRE(cal.isReady());

    cal.reset();
    CHECK(cal.getState() == CalibrationState::UNCALIBRATED);
    CHECK(cal.getBins().empty());
    CHECK(cal.getThresholdDb(0) == std::numeric_limits<float>::infinity());
}

TEST_CASE("long calibrations decimate frames instead of growing without bound",
          "[detector][calibration]") {
    const int BINS = 8;
    NoiseFloorCalibration cal;
    // 10000 frames with room for only 16: the retained sample must still span the window.
    cal.configure(BINS, 10000, 16);
    cal.begin();

    std::vector<float> frame(BINS);
    for (int f = 0; f < 10000; f++) {
        // Ramp the floor over the window. If only the first frames were kept, the baseline would
        // come out near -100 rather than mid-ramp.
        float level = -100.0f + (f * 20.0f / 9999.0f);
        for (int b = 0; b < BINS; b++) { frame[b] = level; }
        cal.addFrame(frame.data(), BINS);
    }

    REQUIRE(cal.isReady());
    CHECK(cal.getBaselineDb(0) == Catch::Approx(-90.0f).margin(2.0));
}
