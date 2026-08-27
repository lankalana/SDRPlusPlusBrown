// Modulators: dsp::mod::Quadrature, the RRC interpolator that dsp::mod::PSK is
// a typedef of, and dsp::mod::GFSK.
//
// The strongest tests here are round trips against the matching demodulator:
// they pin the two halves against each other, so a rewrite of either one has to
// keep the pair consistent.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/demod/quadrature.h>
#include <dsp/mod/gfsk.h>
#include <dsp/mod/psk.h>
#include <dsp/mod/quadrature.h>
#include <dsp/multirate/rrc_interpolator.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

// ------------------------------------------------------------ mod::Quadrature

TEST_CASE("mod::Quadrature turns a DC input into a tone", "[dsp][mod][quadrature]") {
    const double sr = 48000.0;
    const double dev = 2500.0;
    const int n = 8192;

    dsp::mod::Quadrature mod;
    mod.init(nullptr, dev, sr);
    mod.out.setBufferSize(1024);

    // Full-scale input maps to the full deviation.
    auto in = constant(n, 1.0f);
    std::vector<dsp::complex_t> out(n);
    REQUIRE(mod.process(n, in.data(), out.data()) == n);

    REQUIRE(goertzelMag(out, dev, sr) == Approx(1.0).margin(0.02));
    for (const auto& s : out) { REQUIRE(s.amplitude() == Approx(1.0f).margin(1e-4)); }
}

TEST_CASE("mod::Quadrature deviation is signed", "[dsp][mod][quadrature]") {
    const double sr = 48000.0;
    const double dev = 2000.0;
    const int n = 8192;

    dsp::mod::Quadrature mod;
    mod.init(nullptr, dev, sr);
    mod.out.setBufferSize(1024);

    auto in = constant(n, -0.5f);
    std::vector<dsp::complex_t> out(n);
    mod.process(n, in.data(), out.data());

    REQUIRE(goertzelMag(out, -dev * 0.5, sr) == Approx(1.0).margin(0.02));
}

TEST_CASE("mod::Quadrature phase is continuous across calls", "[dsp][mod][quadrature]") {
    const double sr = 48000.0;
    const int n = 1024;

    auto in = noise(n, 4711, 0.8f);

    dsp::mod::Quadrature whole;
    whole.init(nullptr, 3000.0, sr);
    whole.out.setBufferSize(64);
    std::vector<dsp::complex_t> ref(n);
    whole.process(n, in.data(), ref.data());

    dsp::mod::Quadrature chunked;
    chunked.init(nullptr, 3000.0, sr);
    chunked.out.setBufferSize(64);
    std::vector<dsp::complex_t> got(n);
    int off = 0;
    for (int step : { 13, 500, 1, 510 }) {
        chunked.process(step, &in[off], &got[off]);
        off += step;
    }
    REQUIRE(off == n);

    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(got[i].re == Approx(ref[i].re).margin(1e-4));
        REQUIRE(got[i].im == Approx(ref[i].im).margin(1e-4));
    }
}

TEST_CASE("mod::Quadrature reset restarts the phase at zero", "[dsp][mod][quadrature]") {
    dsp::mod::Quadrature mod;
    mod.init(nullptr, 1000.0, 48000.0);
    mod.out.setBufferSize(64);

    auto in = constant(32, 1.0f);
    std::vector<dsp::complex_t> a(32), b(32);
    mod.process(32, in.data(), a.data());
    mod.reset();
    mod.process(32, in.data(), b.data());

    for (int i = 0; i < 32; i++) {
        REQUIRE(b[i].re == Approx(a[i].re).margin(1e-5));
        REQUIRE(b[i].im == Approx(a[i].im).margin(1e-5));
    }
}

TEST_CASE("quadrature modulation and demodulation round-trip", "[dsp][mod][demod][roundtrip]") {
    // This is the contract that matters: whatever the internals, mod followed by
    // demod at the same deviation must return the original signal.
    const double sr = 48000.0;
    const double dev = 4000.0;
    const int n = 8192;

    // A slow tone, well inside the deviation, so no wrapping ambiguity.
    auto in = cosine(n, 300.0, sr, 0.7);

    dsp::mod::Quadrature mod;
    mod.init(nullptr, dev, sr);
    mod.out.setBufferSize(64);
    std::vector<dsp::complex_t> iq(n);
    mod.process(n, in.data(), iq.data());

    dsp::demod::Quadrature demod;
    demod.init(nullptr, dev, sr);
    demod.out.setBufferSize(64);
    std::vector<float> back(n);
    demod.process(n, iq.data(), back.data());

    // The demodulator has a one sample memory, so compare from sample 1 on.
    double err = 0.0;
    for (int i = 2; i < n; i++) { err += std::fabs(back[i] - in[i - 1]); }
    REQUIRE(err / (double)(n - 2) < 0.02);
}

// ----------------------------------------------------------- RRCInterpolator

TEST_CASE("RRCInterpolator upsamples by the rate ratio", "[dsp][mod][rrc]") {
    const double symbolrate = 1000.0;
    const double samplerate = 8000.0;
    const int symbols = 512;

    dsp::multirate::RRCInterpolator<float> interp;
    interp.init(nullptr, symbolrate, samplerate, 0.35, 31);
    interp.out.setBufferSize(symbols * 16);

    auto in = noise(symbols, 5150);
    std::vector<float> out(symbols * 16);
    int produced = interp.process(symbols, in.data(), out.data());

    REQUIRE(produced == Approx(symbols * 8).epsilon(0.02));
}

TEST_CASE("RRCInterpolator setRates changes the ratio", "[dsp][mod][rrc]") {
    dsp::multirate::RRCInterpolator<float> interp;
    interp.init(nullptr, 1000.0, 8000.0, 0.35, 31);
    interp.out.setBufferSize(1 << 16);

    auto in = noise(256, 1);
    std::vector<float> out(1 << 16);

    int at8 = interp.process(256, in.data(), out.data());
    REQUIRE(at8 == Approx(2048).epsilon(0.05));

    interp.setRates(1000.0, 4000.0);
    interp.reset();
    int at4 = interp.process(256, in.data(), out.data());
    REQUIRE(at4 == Approx(1024).epsilon(0.05));
}

TEST_CASE("RRCInterpolator getMaxInputCount bounds the output", "[dsp][mod][rrc]") {
    // The run() path relies on this to size its reads. If it over-reports, the
    // block writes past the end of the output stream buffer.
    dsp::multirate::RRCInterpolator<dsp::complex_t> interp;
    interp.init(nullptr, 1000.0, 8000.0, 0.35, 31);
    const int outCap = 512;
    interp.out.setBufferSize(outCap);

    std::vector<dsp::complex_t> in(4096);
    for (int i = 0; i < 4096; i++) { in[i] = { 1.0f, 0.0f }; }
    std::vector<dsp::complex_t> out(outCap);

    // Deliberately feed exactly outCap/8 inputs, the most that can fit.
    int produced = interp.process(outCap / 8, in.data(), out.data());
    REQUIRE(produced <= outCap);
}

TEST_CASE("RRCInterpolator complex and real paths agree on the real part", "[dsp][mod][rrc]") {
    const int symbols = 256;
    auto in = noise(symbols, 313);

    dsp::multirate::RRCInterpolator<float> real;
    real.init(nullptr, 1000.0, 4000.0, 0.35, 31);
    real.out.setBufferSize(4096);
    std::vector<float> realOut(4096);
    int realCount = real.process(symbols, in.data(), realOut.data());

    std::vector<dsp::complex_t> cin(symbols);
    for (int i = 0; i < symbols; i++) { cin[i] = { in[i], 0.0f }; }
    dsp::multirate::RRCInterpolator<dsp::complex_t> cplx;
    cplx.init(nullptr, 1000.0, 4000.0, 0.35, 31);
    cplx.out.setBufferSize(4096);
    std::vector<dsp::complex_t> cplxOut(4096);
    int cplxCount = cplx.process(symbols, cin.data(), cplxOut.data());

    REQUIRE(realCount == cplxCount);
    for (int i = 0; i < realCount; i++) {
        INFO("sample " << i);
        REQUIRE(cplxOut[i].re == Approx(realOut[i]).margin(1e-4));
        REQUIRE(cplxOut[i].im == Approx(0.0f).margin(1e-6));
    }
}

TEST_CASE("mod::PSK is the complex RRC interpolator", "[dsp][mod][psk]") {
    // A typedef today; the test documents the intent so a real PSK modulator
    // replacing it still has to interpolate to the sample rate.
    dsp::mod::PSK psk;
    psk.init(nullptr, 1200.0, 9600.0, 0.35, 31);
    psk.out.setBufferSize(8192);

    std::vector<dsp::complex_t> syms(128);
    for (int i = 0; i < 128; i++) { syms[i] = { (i % 2) ? 1.0f : -1.0f, 0.0f }; }

    std::vector<dsp::complex_t> out(8192);
    int produced = psk.process(128, syms.data(), out.data());
    REQUIRE(produced == Approx(128 * 8).epsilon(0.05));
}

// ------------------------------------------------------------------ mod::GFSK

TEST_CASE("mod::GFSK produces a constant envelope at the sample rate", "[dsp][mod][gfsk]") {
    const double symbolrate = 1200.0;
    const double samplerate = 9600.0;
    const double deviation = 2400.0;
    const int symbols = 512;

    dsp::mod::GFSK gfsk;
    gfsk.init(nullptr, symbolrate, samplerate, 0.5, 31, deviation);
    gfsk.out.setBufferSize(1 << 14);

    std::vector<float> syms(symbols);
    for (int i = 0; i < symbols; i++) { syms[i] = (i % 3) ? 1.0f : -1.0f; }

    std::vector<dsp::complex_t> out(1 << 14);
    int produced = gfsk.process(symbols, syms.data(), out.data());

    REQUIRE(produced == Approx(symbols * 8).epsilon(0.02));
    // FSK is constant envelope by construction.
    for (int i = 0; i < produced; i++) {
        INFO("sample " << i);
        REQUIRE(out[i].amplitude() == Approx(1.0f).margin(1e-3));
    }
}

TEST_CASE("mod::GFSK keeps its instantaneous frequency inside the deviation", "[dsp][mod][gfsk]") {
    const double symbolrate = 1200.0;
    const double samplerate = 9600.0;
    const double deviation = 2400.0;
    const int symbols = 256;

    dsp::mod::GFSK gfsk;
    gfsk.init(nullptr, symbolrate, samplerate, 0.5, 31, deviation);
    gfsk.out.setBufferSize(1 << 13);

    std::vector<float> syms(symbols);
    for (int i = 0; i < symbols; i++) { syms[i] = (i % 2) ? 1.0f : -1.0f; }

    std::vector<dsp::complex_t> out(1 << 13);
    int produced = gfsk.process(symbols, syms.data(), out.data());

    // Demodulate and check the excursion. The finite RRC filter rings at
    // symbol transitions, so allow 50% headroom over the steady-state value.
    dsp::demod::Quadrature demod;
    demod.init(nullptr, deviation, samplerate);
    demod.out.setBufferSize(64);
    std::vector<float> freq(produced);
    demod.process(produced, out.data(), freq.data());

    for (int i = 8; i < produced; i++) {
        INFO("sample " << i);
        REQUIRE(std::fabs(freq[i]) < 1.5f);
    }
}

TEST_CASE("mod::GFSK reset makes the block reproducible", "[dsp][mod][gfsk]") {
    dsp::mod::GFSK gfsk;
    gfsk.init(nullptr, 1200.0, 9600.0, 0.5, 31, 2400.0);
    gfsk.out.setBufferSize(1 << 13);

    std::vector<float> syms(128);
    for (int i = 0; i < 128; i++) { syms[i] = (i % 5 < 2) ? 1.0f : -1.0f; }

    std::vector<dsp::complex_t> a(1 << 13), b(1 << 13);
    int ca = gfsk.process(128, syms.data(), a.data());
    gfsk.reset();
    int cb = gfsk.process(128, syms.data(), b.data());

    REQUIRE(ca == cb);
    for (int i = 0; i < ca; i++) {
        INFO("sample " << i);
        REQUIRE(b[i].re == Approx(a[i].re).margin(1e-4));
        REQUIRE(b[i].im == Approx(a[i].im).margin(1e-4));
    }
}

TEST_CASE("mod::GFSK setDeviation scales the frequency excursion", "[dsp][mod][gfsk]") {
    const double samplerate = 9600.0;
    dsp::mod::GFSK gfsk;
    gfsk.init(nullptr, 1200.0, samplerate, 0.5, 31, 1200.0);
    gfsk.out.setBufferSize(1 << 13);

    std::vector<float> syms(256, 1.0f);
    std::vector<dsp::complex_t> narrow(1 << 13), wide(1 << 13);

    int cn = gfsk.process(256, syms.data(), narrow.data());
    gfsk.reset();
    gfsk.setDeviation(2400.0);
    int cw = gfsk.process(256, syms.data(), wide.data());
    REQUIRE(cn == cw);

    // Steady +1 symbols: the output is a tone at the deviation.
    std::vector<dsp::complex_t> nTail(narrow.begin() + cn / 2, narrow.begin() + cn);
    std::vector<dsp::complex_t> wTail(wide.begin() + cw / 2, wide.begin() + cw);
    REQUIRE(goertzelMag(nTail, 1200.0, samplerate) > 0.8);
    REQUIRE(goertzelMag(wTail, 2400.0, samplerate) > 0.8);
}
