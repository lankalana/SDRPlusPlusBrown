// DC blocker: the leaky-integrator high pass sitting in the IQ front end.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <dsp/correction/dc_blocker.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

TEST_CASE("DCBlocker removes a constant offset", "[dsp][correction]") {
    dsp::stream<float> in;
    in.setBufferSize(8192);

    dsp::correction::DCBlocker<float> dc;
    dc.init(&in, 0.01);
    dc.out.setBufferSize(8192);

    auto input = constant(4096, 0.5f);
    std::vector<float> out(4096);
    dc.process(4096, input.data(), out.data());

    // The first sample passes through untouched, then the offset is tracked out.
    REQUIRE(out[0] == Approx(0.5f));
    REQUIRE(std::fabs(out[4095]) < 1e-3f);
}

TEST_CASE("DCBlocker leaves an AC signal mostly intact", "[dsp][correction]") {
    const double sr = 48000.0;
    dsp::stream<float> in;
    in.setBufferSize(16384);

    dsp::correction::DCBlocker<float> dc;
    dc.init(&in, 100.0, sr); // corner well below the test tone
    dc.out.setBufferSize(16384);

    const int N = 16384;
    auto input = cosine(N, 5000.0, sr);
    std::vector<float> out(N);
    dc.process(N, input.data(), out.data());

    std::vector<float> tail(out.begin() + 2048, out.end());
    REQUIRE(rms(tail) == Approx(rms(input)).epsilon(0.05));
}

TEST_CASE("DCBlocker removes the offset while passing the tone", "[dsp][correction]") {
    const double sr = 48000.0;
    dsp::stream<float> in;
    in.setBufferSize(16384);

    dsp::correction::DCBlocker<float> dc;
    dc.init(&in, 100.0, sr);
    dc.out.setBufferSize(16384);

    const int N = 16384;
    auto input = cosine(N, 5000.0, sr);
    for (auto& s : input) { s += 2.0f; }

    std::vector<float> out(N);
    dc.process(N, input.data(), out.data());

    std::vector<float> tail(out.begin() + 4096, out.end());
    REQUIRE(std::fabs(mean(tail)) < 0.01);
    REQUIRE(rms(tail) == Approx(1.0 / std::sqrt(2.0)).epsilon(0.05));
}

TEST_CASE("DCBlocker rate controls how fast the offset is removed", "[dsp][correction]") {
    dsp::stream<float> inA, inB;
    inA.setBufferSize(4096);
    inB.setBufferSize(4096);

    dsp::correction::DCBlocker<float> slow, fast;
    slow.init(&inA, 0.0005);
    fast.init(&inB, 0.05);
    slow.out.setBufferSize(4096);
    fast.out.setBufferSize(4096);

    auto input = constant(512, 1.0f);
    std::vector<float> outSlow(512), outFast(512);
    slow.process(512, input.data(), outSlow.data());
    fast.process(512, input.data(), outFast.data());

    REQUIRE(std::fabs(outFast[511]) < std::fabs(outSlow[511]));
}

TEST_CASE("DCBlocker reset clears the tracked offset", "[dsp][correction]") {
    dsp::stream<float> in;
    in.setBufferSize(4096);

    dsp::correction::DCBlocker<float> dc;
    dc.init(&in, 0.01);
    dc.out.setBufferSize(4096);

    auto input = constant(1024, 1.0f);
    std::vector<float> first(1024), second(1024);
    dc.process(1024, input.data(), first.data());

    dc.reset();
    dc.process(1024, input.data(), second.data());

    for (int i = 0; i < 1024; i++) { REQUIRE(second[i] == Approx(first[i]).margin(1e-6)); }
}

TEST_CASE("DCBlocker works on complex samples", "[dsp][correction]") {
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(8192);

    dsp::correction::DCBlocker<dsp::complex_t> dc;
    dc.init(&in, 0.01);
    dc.out.setBufferSize(8192);

    std::vector<dsp::complex_t> input(4096, dsp::complex_t{ 0.5f, -0.25f });
    std::vector<dsp::complex_t> out(4096);
    dc.process(4096, input.data(), out.data());

    REQUIRE(std::fabs(out[4095].re) < 1e-3f);
    REQUIRE(std::fabs(out[4095].im) < 1e-3f);
}

TEST_CASE("DCBlocker setRate takes effect immediately", "[dsp][correction]") {
    dsp::stream<float> in;
    in.setBufferSize(4096);

    dsp::correction::DCBlocker<float> dc;
    dc.init(&in, 0.0);
    dc.out.setBufferSize(4096);

    // Rate 0 means the offset never adapts, so the block is a pass-through.
    auto input = constant(256, 1.0f);
    std::vector<float> out(256);
    dc.process(256, input.data(), out.data());
    REQUIRE(out[255] == Approx(1.0f));

    dc.setRate(0.05);
    dc.process(256, input.data(), out.data());
    REQUIRE(std::fabs(out[255]) < 0.5f);
}
