// Squelch gating.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <dsp/noise_reduction/squelch.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

TEST_CASE("Squelch passes a signal above the threshold", "[dsp][squelch]") {
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(4096);

    dsp::noise_reduction::Squelch sq;
    sq.init(&in, -50.0);
    sq.out.setBufferSize(4096);

    auto input = complexTone(1024, 1000.0, 48000.0, 1.0);
    std::vector<dsp::complex_t> out(1024);
    REQUIRE(sq.process(1024, input.data(), out.data()) == 1024);

    for (int i = 0; i < 1024; i++) {
        REQUIRE(out[i].re == input[i].re);
        REQUIRE(out[i].im == input[i].im);
    }
}

TEST_CASE("Squelch mutes a signal below the threshold", "[dsp][squelch]") {
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(4096);

    dsp::noise_reduction::Squelch sq;
    sq.init(&in, -20.0);
    sq.out.setBufferSize(4096);

    auto input = complexTone(1024, 1000.0, 48000.0, 0.001); // -60 dB
    std::vector<dsp::complex_t> out(1024);
    sq.process(1024, input.data(), out.data());

    for (int i = 0; i < 1024; i++) {
        REQUIRE(out[i].re == 0.0f);
        REQUIRE(out[i].im == 0.0f);
    }
}

TEST_CASE("Squelch setLevel moves the threshold", "[dsp][squelch]") {
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(4096);

    dsp::noise_reduction::Squelch sq;
    sq.init(&in, -80.0);
    sq.out.setBufferSize(4096);

    // A 0.01 amplitude tone is at -40 dB on the block's mean-magnitude scale.
    auto input = complexTone(1024, 1000.0, 48000.0, 0.01);
    std::vector<dsp::complex_t> out(1024);

    sq.process(1024, input.data(), out.data());
    REQUIRE(out[0].re != 0.0f);

    sq.setLevel(0.0);
    sq.process(1024, input.data(), out.data());
    REQUIRE(out[0].re == 0.0f);
    REQUIRE(out[0].im == 0.0f);
}

TEST_CASE("Squelch decision uses the mean magnitude of the block", "[dsp][squelch]") {
    // The gate is all-or-nothing per block: a short loud burst inside an
    // otherwise quiet block does not open it.
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(4096);

    dsp::noise_reduction::Squelch sq;
    sq.init(&in, -10.0);
    sq.out.setBufferSize(4096);

    std::vector<dsp::complex_t> input(1024, dsp::complex_t{ 0.0f, 0.0f });
    for (int i = 0; i < 8; i++) { input[i] = { 10.0f, 0.0f }; }

    std::vector<dsp::complex_t> out(1024);
    sq.process(1024, input.data(), out.data());

    // Mean magnitude is 80/1024 ~= 0.078, i.e. about -11 dB: below the gate.
    REQUIRE(out[0].re == 0.0f);
}

TEST_CASE("Squelch stereo overload gates on RMS", "[dsp][squelch]") {
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(4096);

    dsp::noise_reduction::Squelch sq;
    sq.init(&in, -20.0);
    sq.out.setBufferSize(4096);

    std::vector<dsp::stereo_t> loud(256, dsp::stereo_t{ 1.0f, 1.0f });
    std::vector<dsp::stereo_t> quiet(256, dsp::stereo_t{ 0.001f, 0.001f });
    std::vector<dsp::stereo_t> out(256);

    sq.process(256, loud.data(), out.data());
    REQUIRE(out[0].l == 1.0f);

    sq.process(256, quiet.data(), out.data());
    REQUIRE(out[0].l == 0.0f);
    REQUIRE(out[0].r == 0.0f);

    // Zero-length blocks are explicitly tolerated.
    REQUIRE(sq.process(0, quiet.data(), out.data()) == 0);
}
