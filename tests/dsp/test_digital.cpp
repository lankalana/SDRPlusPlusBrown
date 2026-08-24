// Bit-level decoders.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <dsp/digital/binary_slicer.h>
#include <dsp/digital/differential_decoder.h>
#include <dsp/digital/manchester_decoder.h>

#include "support/dsp_test_helpers.h"

using namespace sdrpp_test;

TEST_CASE("BinarySlicer thresholds at zero", "[dsp][digital]") {
    std::vector<float> in = { 1.0f, -1.0f, 0.0f, 0.001f, -0.001f, 1e30f, -1e30f };
    std::vector<uint8_t> out(in.size());

    REQUIRE(dsp::digital::BinarySlicer::process((int)in.size(), in.data(), out.data()) == (int)in.size());
    REQUIRE(out[0] == 1);
    REQUIRE(out[1] == 0);
    REQUIRE(out[2] == 0); // exactly zero slices low
    REQUIRE(out[3] == 1);
    REQUIRE(out[4] == 0);
    REQUIRE(out[5] == 1);
    REQUIRE(out[6] == 0);
}

TEST_CASE("DifferentialDecoder decodes symbol differences", "[dsp][digital]") {
    dsp::stream<uint8_t> in;
    in.setBufferSize(64);

    dsp::digital::DifferentialDecoder dec;
    dec.init(&in, 4, 0);
    dec.out.setBufferSize(64);

    std::vector<uint8_t> input = { 0, 1, 3, 3, 2 };
    std::vector<uint8_t> out(input.size());
    dec.process((int)input.size(), input.data(), out.data());

    // (in - previous) mod 4, starting from initSym 0.
    REQUIRE(out[0] == 0); // 0-0
    REQUIRE(out[1] == 1); // 1-0
    REQUIRE(out[2] == 2); // 3-1
    REQUIRE(out[3] == 0); // 3-3
    REQUIRE(out[4] == 3); // 2-3 mod 4
}

TEST_CASE("DifferentialDecoder carries state between calls", "[dsp][digital]") {
    dsp::stream<uint8_t> in;
    in.setBufferSize(64);

    dsp::digital::DifferentialDecoder dec;
    dec.init(&in, 2, 0);
    dec.out.setBufferSize(64);

    std::vector<uint8_t> a = { 1 };
    std::vector<uint8_t> b = { 1 };
    uint8_t out = 9;

    dec.process(1, a.data(), &out);
    REQUIRE(out == 1); // 1-0
    dec.process(1, b.data(), &out);
    REQUIRE(out == 0); // 1-1

    dec.reset();
    dec.process(1, b.data(), &out);
    REQUIRE(out == 1); // reset restored the init symbol
}

TEST_CASE("DifferentialDecoder honours initSym", "[dsp][digital]") {
    dsp::stream<uint8_t> in;
    in.setBufferSize(64);

    dsp::digital::DifferentialDecoder dec;
    dec.init(&in, 4, 2);
    dec.out.setBufferSize(64);

    std::vector<uint8_t> input = { 3 };
    uint8_t out = 0;
    dec.process(1, input.data(), &out);
    REQUIRE(out == 1); // 3-2
}

TEST_CASE("ManchesterDecoder keeps every other symbol", "[dsp][digital]") {
    dsp::stream<uint8_t> in;
    in.setBufferSize(64);

    dsp::digital::ManchesterDecoder dec;
    dec.init(&in);
    dec.out.setBufferSize(64);

    std::vector<uint8_t> input = { 1, 0, 1, 1, 0, 0 };
    std::vector<uint8_t> out(input.size());
    int n = dec.process((int)input.size(), input.data(), out.data());

    REQUIRE(n == 3);
    REQUIRE(out[0] == 1);
    REQUIRE(out[1] == 1);
    REQUIRE(out[2] == 0);
}

TEST_CASE("ManchesterDecoder keeps its phase across odd-sized chunks", "[dsp][digital]") {
    dsp::stream<uint8_t> in;
    in.setBufferSize(64);

    dsp::digital::ManchesterDecoder dec;
    dec.init(&in);
    dec.out.setBufferSize(64);

    std::vector<uint8_t> first = { 1, 2, 3 };  // consumes index 0 and 2
    std::vector<uint8_t> second = { 4, 5, 6 }; // must resume on index 1
    std::vector<uint8_t> out(8);

    int n1 = dec.process(3, first.data(), out.data());
    REQUIRE(n1 == 2);
    REQUIRE(out[0] == 1);
    REQUIRE(out[1] == 3);

    int n2 = dec.process(3, second.data(), out.data());
    REQUIRE(n2 == 1);
    REQUIRE(out[0] == 5);

    dec.reset();
    int n3 = dec.process(3, second.data(), out.data());
    REQUIRE(n3 == 2);
    REQUIRE(out[0] == 4);
}
