// dsp::noise_reduction::NoiseBlanker and dsp::noise_reduction::FMIF.
//
// The blanker is pure arithmetic and is tested exactly. FMIF runs a forward and
// a reverse FFT per input sample, so it is only exercised on short buffers and
// only for the properties that must hold regardless of the FFT size.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/noise_reduction/fm_if.h>
#include <dsp/noise_reduction/noise_blanker.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

// ------------------------------------------------------------- NoiseBlanker

TEST_CASE("NoiseBlanker passes a steady signal through", "[dsp][noise][blanker]") {
    const int n = 4096;

    dsp::noise_reduction::NoiseBlanker nb;
    nb.init(nullptr, 0.01, 2.0);
    nb.out.setBufferSize(64);

    auto in = complexTone(n, 1000.0, 48000.0, 0.5);
    std::vector<dsp::complex_t> out(n);
    REQUIRE(nb.process(n, in.data(), out.data()) == n);

    // Once the envelope tracker has settled, a constant-amplitude signal never
    // exceeds the threshold, so the gain stays at 1.
    for (int i = n / 2; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(out[i].re == Approx(in[i].re).margin(1e-5));
        REQUIRE(out[i].im == Approx(in[i].im).margin(1e-5));
    }
}

TEST_CASE("NoiseBlanker clips an impulse to the threshold", "[dsp][noise][blanker]") {
    const int n = 2048;
    const float level = 3.0f;

    dsp::noise_reduction::NoiseBlanker nb;
    nb.init(nullptr, 0.001, level);
    nb.out.setBufferSize(64);

    // Steady 0.1 amplitude with a single huge spike in the middle.
    std::vector<dsp::complex_t> in(n, { 0.1f, 0.0f });
    in[n / 2] = { 50.0f, 0.0f };

    std::vector<dsp::complex_t> out(n);
    nb.process(n, in.data(), out.data());

    // The spike is scaled down to roughly the running average times the level.
    REQUIRE(out[n / 2].amplitude() < 1.0f);
    REQUIRE(out[n / 2].amplitude() > 0.0f);
    // Its neighbours are untouched.
    REQUIRE(out[n / 2 - 1].amplitude() == Approx(0.1f).margin(1e-3));
}

TEST_CASE("NoiseBlanker leaves zero samples alone", "[dsp][noise][blanker]") {
    // A zero sample must not divide by zero or poison the envelope.
    dsp::noise_reduction::NoiseBlanker nb;
    nb.init(nullptr, 0.01, 2.0);
    nb.out.setBufferSize(64);

    std::vector<dsp::complex_t> in(16, { 0.0f, 0.0f });
    std::vector<dsp::complex_t> out(16);
    nb.process(16, in.data(), out.data());

    for (const auto& s : out) {
        REQUIRE(s.re == 0.0f);
        REQUIRE(s.im == 0.0f);
        REQUIRE(std::isfinite(s.re));
    }
}

TEST_CASE("NoiseBlanker with a high level is a pass-through", "[dsp][noise][blanker]") {
    const int n = 1024;

    dsp::noise_reduction::NoiseBlanker nb;
    nb.init(nullptr, 0.01, 1e9);
    nb.out.setBufferSize(64);

    auto real = noise(n, 4242, 1.0f);
    std::vector<dsp::complex_t> in(n);
    for (int i = 0; i < n; i++) { in[i] = { real[i], -real[i] }; }

    std::vector<dsp::complex_t> out(n);
    nb.process(n, in.data(), out.data());

    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(out[i].re == Approx(in[i].re).margin(1e-6));
        REQUIRE(out[i].im == Approx(in[i].im).margin(1e-6));
    }
}

TEST_CASE("NoiseBlanker setLevel changes how aggressive it is", "[dsp][noise][blanker]") {
    const int n = 1024;
    std::vector<dsp::complex_t> in(n, { 0.1f, 0.0f });
    for (int i = 100; i < n; i += 100) { in[i] = { 2.0f, 0.0f }; }

    dsp::noise_reduction::NoiseBlanker gentle;
    gentle.init(nullptr, 0.001, 10.0);
    gentle.out.setBufferSize(64);
    std::vector<dsp::complex_t> gentleOut(n);
    gentle.process(n, in.data(), gentleOut.data());

    dsp::noise_reduction::NoiseBlanker harsh;
    harsh.init(nullptr, 0.001, 1.5);
    harsh.out.setBufferSize(64);
    std::vector<dsp::complex_t> harshOut(n);
    harsh.process(n, in.data(), harshOut.data());

    REQUIRE(harshOut[500].amplitude() < gentleOut[500].amplitude());
}

TEST_CASE("NoiseBlanker reset clears the envelope", "[dsp][noise][blanker]") {
    const int n = 512;
    dsp::noise_reduction::NoiseBlanker nb;
    nb.init(nullptr, 0.01, 2.0);
    nb.out.setBufferSize(64);

    std::vector<dsp::complex_t> in(n, { 0.3f, 0.4f });
    std::vector<dsp::complex_t> a(n), b(n);

    nb.process(n, in.data(), a.data());
    nb.reset();
    nb.process(n, in.data(), b.data());

    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(b[i].re == Approx(a[i].re).margin(1e-5));
        REQUIRE(b[i].im == Approx(a[i].im).margin(1e-5));
    }
}

// -------------------------------------------------------------------- FMIF

TEST_CASE("FMIF keeps the dominant tone", "[dsp][noise][fmif]") {
    // FMIF keeps only the strongest FFT bin per sample, so a clean tone should
    // survive essentially intact (up to the FFT's own gain and group delay).
    const double sr = 48000.0;
    const int bins = 64;
    const int n = 2048;

    dsp::noise_reduction::FMIF fmif;
    fmif.init(nullptr, bins);
    fmif.out.setBufferSize(64);

    auto in = complexTone(n, 3000.0, sr, 1.0);
    std::vector<dsp::complex_t> out(n);
    REQUIRE(fmif.process(n, in.data(), out.data()) == n);

    // Skip the fill-in of the delay line.
    std::vector<dsp::complex_t> tail(out.begin() + bins * 2, out.end());
    double onTone = goertzelMag(tail, 3000.0, sr);
    REQUIRE(onTone > 0.0);
    // The tone must dominate: an unrelated frequency has far less energy.
    REQUIRE(goertzelMag(tail, 9000.0, sr) < onTone * 0.1);
}

TEST_CASE("FMIF output is finite and constant-length", "[dsp][noise][fmif]") {
    const int bins = 32;
    const int n = 512;

    dsp::noise_reduction::FMIF fmif;
    fmif.init(nullptr, bins);
    fmif.out.setBufferSize(64);

    auto re = noise(n, 17);
    auto im = noise(n, 71);
    std::vector<dsp::complex_t> in(n);
    for (int i = 0; i < n; i++) { in[i] = { re[i], im[i] }; }

    std::vector<dsp::complex_t> out(n);
    REQUIRE(fmif.process(n, in.data(), out.data()) == n);
    for (const auto& s : out) {
        REQUIRE(std::isfinite(s.re));
        REQUIRE(std::isfinite(s.im));
    }
}

TEST_CASE("FMIF setBins reconfigures without leaking", "[dsp][noise][fmif]") {
    const int n = 256;
    dsp::noise_reduction::FMIF fmif;
    fmif.init(nullptr, 32);
    fmif.out.setBufferSize(64);

    auto in = complexTone(n, 1000.0, 48000.0);
    std::vector<dsp::complex_t> out(n);
    fmif.process(n, in.data(), out.data());

    fmif.setBins(64);
    REQUIRE(fmif.process(n, in.data(), out.data()) == n);

    fmif.reset();
    REQUIRE(fmif.process(n, in.data(), out.data()) == n);
}
