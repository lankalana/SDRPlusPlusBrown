// The detection floor: a flat level the user sets, or a spectral estimate measured on demand.
//
// Nothing in here ever starts by itself. That is the point: a floor that re-measures itself
// behind the user's back is impossible to reason about when tuning detection by eye.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <dsp/detector/noise_floor.h>

#include <cmath>
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
