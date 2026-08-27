// Format conversion blocks. They are pure glue, so the tests are exhaustive
// about layout: these are the places where a wrong volk call silently swaps
// channels or halves the sample count.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <dsp/convert/complex_to_real.h>
#include <dsp/convert/complex_to_stereo.h>
#include <dsp/convert/l_r_to_stereo.h>
#include <dsp/convert/mono_to_stereo.h>
#include <dsp/convert/real_to_complex.h>
#include <dsp/convert/stereo_to_mono.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

TEST_CASE("ComplexToReal keeps the real part", "[dsp][convert]") {
    std::vector<dsp::complex_t> in = { { 1.0f, 9.0f }, { -2.0f, 8.0f }, { 0.5f, -7.0f } };
    std::vector<float> out(in.size());

    REQUIRE(dsp::convert::ComplexToReal::process((int)in.size(), in.data(), out.data()) == 3);
    REQUIRE(out[0] == 1.0f);
    REQUIRE(out[1] == -2.0f);
    REQUIRE(out[2] == 0.5f);
}

TEST_CASE("RealToComplex puts the signal on the real axis", "[dsp][convert]") {
    std::vector<float> in = { 1.0f, -2.0f, 0.5f };
    std::vector<dsp::complex_t> out(in.size());

    REQUIRE(dsp::convert::RealToComplex::process((int)in.size(), in.data(), out.data()) == 3);
    for (size_t i = 0; i < in.size(); i++) {
        REQUIRE(out[i].re == in[i]);
        REQUIRE(out[i].im == 0.0f);
    }
}

TEST_CASE("RealToComplex round-trips through ComplexToReal", "[dsp][convert]") {
    auto in = noise(256, 3);
    std::vector<dsp::complex_t> mid(in.size());
    std::vector<float> out(in.size());

    dsp::convert::RealToComplex::process((int)in.size(), in.data(), mid.data());
    dsp::convert::ComplexToReal::process((int)mid.size(), mid.data(), out.data());

    for (size_t i = 0; i < in.size(); i++) { REQUIRE(out[i] == in[i]); }
}

TEST_CASE("MonoToStereo duplicates the channel", "[dsp][convert]") {
    std::vector<float> in = { 1.0f, -2.0f, 0.5f };
    std::vector<dsp::stereo_t> out(in.size());

    REQUIRE(dsp::convert::MonoToStereo::process((int)in.size(), in.data(), out.data()) == 3);
    for (size_t i = 0; i < in.size(); i++) {
        REQUIRE(out[i].l == in[i]);
        REQUIRE(out[i].r == in[i]);
    }
}

TEST_CASE("StereoToMono averages the two channels", "[dsp][convert]") {
    std::vector<dsp::stereo_t> in = { { 1.0f, 3.0f }, { -1.0f, 1.0f }, { 2.0f, 2.0f } };
    std::vector<float> out(in.size());

    REQUIRE(dsp::convert::StereoToMono::process((int)in.size(), in.data(), out.data()) == 3);
    REQUIRE(out[0] == Approx(2.0f));
    REQUIRE(out[1] == Approx(0.0f));
    REQUIRE(out[2] == Approx(2.0f));
}

TEST_CASE("MonoToStereo then StereoToMono is the identity", "[dsp][convert]") {
    auto in = noise(128, 11);
    std::vector<dsp::stereo_t> mid(in.size());
    std::vector<float> out(in.size());

    dsp::convert::MonoToStereo::process((int)in.size(), in.data(), mid.data());
    dsp::convert::StereoToMono::process((int)mid.size(), mid.data(), out.data());

    for (size_t i = 0; i < in.size(); i++) { REQUIRE(out[i] == Approx(in[i]).margin(1e-6)); }
}

TEST_CASE("LRToStereo interleaves left and right", "[dsp][convert]") {
    std::vector<float> l = { 1.0f, 2.0f, 3.0f };
    std::vector<float> r = { -1.0f, -2.0f, -3.0f };
    std::vector<dsp::stereo_t> out(3);

    REQUIRE(dsp::convert::LRToStereo::process(3, l.data(), r.data(), out.data()) == 3);
    for (int i = 0; i < 3; i++) {
        REQUIRE(out[i].l == l[i]);
        REQUIRE(out[i].r == r[i]);
    }
}

TEST_CASE("ComplexToStereo reinterprets I/Q as L/R", "[dsp][convert]") {
    // The block is a straight memcpy: I becomes left, Q becomes right. That is
    // what the IQ-in-audio transport relies on.
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(64);

    dsp::convert::ComplexToStereo block;
    block.init(&in);
    block.out.setBufferSize(64);

    StreamCollector<dsp::stereo_t> collector(&block.out);
    StreamFeeder<dsp::complex_t> feeder(&in);

    block.start();
    std::vector<dsp::complex_t> data = { { 1.0f, 2.0f }, { 3.0f, 4.0f } };
    REQUIRE(feeder.feed(data));
    REQUIRE(collector.waitFor(2));

    auto out = collector.data();
    REQUIRE(out[0].l == 1.0f);
    REQUIRE(out[0].r == 2.0f);
    REQUIRE(out[1].l == 3.0f);
    REQUIRE(out[1].r == 4.0f);

    block.stop();
    collector.stop();
}

TEST_CASE("convert blocks accept a zero sample count", "[dsp][convert]") {
    std::vector<dsp::complex_t> c(1);
    std::vector<float> f(1);
    std::vector<dsp::stereo_t> s(1);

    REQUIRE(dsp::convert::ComplexToReal::process(0, c.data(), f.data()) == 0);
    REQUIRE(dsp::convert::RealToComplex::process(0, f.data(), c.data()) == 0);
    REQUIRE(dsp::convert::MonoToStereo::process(0, f.data(), s.data()) == 0);
    REQUIRE(dsp::convert::StereoToMono::process(0, s.data(), f.data()) == 0);
    REQUIRE(dsp::convert::LRToStereo::process(0, f.data(), f.data(), s.data()) == 0);
}
