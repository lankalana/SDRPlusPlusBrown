// SpectrumIntegrator: prefix sums of excess and noise power.
//
// The contract that matters downstream is that a range sum is exact and that excised bins look
// exactly like bins holding nothing but noise.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <dsp/detector/noise_floor.h>
#include <dsp/detector/spectrum_integrator.h>

#include <cmath>
#include <vector>

using namespace dsp::detector;

namespace {
    const int BINS = 64;
    const float FLOOR_DB = -100.0f;

    // A manual floor is flat, so every bin's noise power is the same and easy to reason about.
    NoiseFloorModel flatFloor() {
        NoiseFloorModel floor;
        floor.configure(BINS, 1000.0);
        floor.setMode(NoiseFloorMode::MANUAL);
        floor.setManualFloorDb(FLOOR_DB);
        return floor;
    }

    double noiseLinPerBin() {
        NoiseFloorModel floor = flatFloor();
        return std::pow(10.0, (double)floor.getNoisePowerDb(0) / 10.0);
    }

    std::vector<float> flatPower(double lin) { return std::vector<float>(BINS, (float)lin); }
}

TEST_CASE("range sums match a naive loop", "[detector][integrator]") {
    NoiseFloorModel floor = flatFloor();
    double noise = noiseLinPerBin();

    std::vector<float> power(BINS);
    for (int b = 0; b < BINS; b++) {
        // Deterministic but uneven, including bins below the floor.
        power[b] = (float)(noise * (0.5 + 0.37 * ((b * 7919) % 13)));
    }

    SpectrumIntegrator integ;
    integ.build(power.data(), BINS, floor, {});

    for (int lo = 0; lo < BINS; lo += 5) {
        for (int hi = lo; hi < BINS; hi += 7) {
            double expectedExcess = 0.0;
            double expectedNoise = 0.0;
            for (int b = lo; b <= hi; b++) {
                expectedExcess += std::max(0.0, (double)power[b] - noise);
                expectedNoise += noise;
            }
            CHECK(integ.excessPower(lo, hi) == Catch::Approx(expectedExcess).epsilon(1e-9));
            CHECK(integ.noisePower(lo, hi) == Catch::Approx(expectedNoise).epsilon(1e-9));
        }
    }
}

TEST_CASE("a range at the floor has unit SNR ratio", "[detector][integrator]") {
    NoiseFloorModel floor = flatFloor();
    auto power = flatPower(noiseLinPerBin());

    SpectrumIntegrator integ;
    integ.build(power.data(), BINS, floor, {});

    CHECK(integ.snrRatio(0, BINS - 1) == Catch::Approx(1.0));
    CHECK(integ.snrDb(0, BINS - 1) == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("doubling the power doubles the SNR ratio", "[detector][integrator]") {
    NoiseFloorModel floor = flatFloor();
    auto power = flatPower(noiseLinPerBin() * 2.0);

    SpectrumIntegrator integ;
    integ.build(power.data(), BINS, floor, {});

    CHECK(integ.snrRatio(10, 20) == Catch::Approx(2.0));
    CHECK(integ.snrDb(10, 20) == Catch::Approx(3.0103).margin(1e-3));
}

TEST_CASE("an excised range contributes noise and no excess", "[detector][integrator]") {
    NoiseFloorModel floor = flatFloor();
    double noise = noiseLinPerBin();

    // A huge spike, as LO leakage at the capture centre would be.
    auto power = flatPower(noise);
    for (int b = 30; b <= 33; b++) { power[b] = (float)(noise * 1e6); }

    SpectrumIntegrator integ;
    integ.build(power.data(), BINS, floor, { { 30, 33 } });

    CHECK(integ.excessPower(30, 33) == Catch::Approx(0.0).margin(1e-30));
    CHECK(integ.noisePower(30, 33) == Catch::Approx(noise * 4).epsilon(1e-9));
    // And, critically, a wide kernel straddling it sees nothing either.
    CHECK(integ.snrRatio(0, BINS - 1) == Catch::Approx(1.0));
}

TEST_CASE("excision does not disturb neighbouring sums", "[detector][integrator]") {
    NoiseFloorModel floor = flatFloor();
    double noise = noiseLinPerBin();

    auto power = flatPower(noise);
    for (int b = 30; b <= 33; b++) { power[b] = (float)(noise * 1e6); }
    for (int b = 40; b <= 42; b++) { power[b] = (float)(noise * 10.0); }

    SpectrumIntegrator integ;
    integ.build(power.data(), BINS, floor, { { 30, 33 } });

    // Power arrives as float, so the tolerance is float precision, not double.
    CHECK(integ.excessPower(40, 42) == Catch::Approx(noise * 9.0 * 3.0).epsilon(1e-5));
    CHECK(integ.excessPower(0, 29) == Catch::Approx(0.0).margin(1e-30));
}

TEST_CASE("rebuilding at a different bin count resizes", "[detector][integrator]") {
    NoiseFloorModel floor = flatFloor();
    auto power = flatPower(noiseLinPerBin());

    SpectrumIntegrator integ;
    integ.build(power.data(), BINS, floor, {});
    REQUIRE(integ.binCount() == BINS);

    NoiseFloorModel wider;
    wider.configure(16, 1000.0);
    wider.setMode(NoiseFloorMode::MANUAL);
    wider.setManualFloorDb(FLOOR_DB);
    std::vector<float> small(16, (float)noiseLinPerBin());
    integ.build(small.data(), 16, wider, {});
    CHECK(integ.binCount() == 16);
    CHECK(integ.excessPower(0, 15) == Catch::Approx(0.0).margin(1e-30));
}

TEST_CASE("an empty integrator yields nothing", "[detector][integrator]") {
    SpectrumIntegrator integ;
    CHECK(integ.empty());
    CHECK(integ.binCount() == 0);
    CHECK(integ.excessPower(0, 10) == 0.0);
    CHECK(integ.snrRatio(0, 10) == 1.0);

    NoiseFloorModel floor = flatFloor();
    integ.build(nullptr, 0, floor, {});
    CHECK(integ.empty());
}

TEST_CASE("out of range queries are clamped, not undefined", "[detector][integrator]") {
    NoiseFloorModel floor = flatFloor();
    auto power = flatPower(noiseLinPerBin() * 3.0);

    SpectrumIntegrator integ;
    integ.build(power.data(), BINS, floor, {});

    CHECK(integ.excessPower(-100, 1000) == Catch::Approx(integ.excessPower(0, BINS - 1)));
    CHECK(integ.excessPower(50, 10) == 0.0);
    CHECK(integ.binExcess(-1) == 0.0);
    CHECK(integ.binExcess(BINS) == 0.0);
}
