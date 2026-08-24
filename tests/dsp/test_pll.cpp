// The phase control loop and the three blocks built on it: PLL,
// CarrierTrackingPLL and Costas.
//
// The loop is shared by clock recovery, PSK/GFSK demodulation and the SSB/AM
// carrier trackers, so a regression here is felt everywhere. The PCL tests are
// exact (it is pure arithmetic); the loop-level tests assert convergence
// properties rather than sample values, so they survive retuning.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/loop/carrier_tracking_pll.h>
#include <dsp/math/constants.h>
#include <dsp/loop/costas.h>
#include <dsp/loop/phase_control_loop.h>
#include <dsp/loop/pll.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    // pcl is protected on PLL; these expose it so the tests can check what the
    // loop actually converged to instead of guessing from the output.
    struct ProbedPLL : public dsp::loop::PLL {
        float loopFreq() { return pcl.freq; }
        float loopPhase() { return pcl.phase; }
    };

    struct ProbedCTPLL : public dsp::loop::CarrierTrackingPLL {
        float loopFreq() { return pcl.freq; }
    };

    template <int ORDER>
    struct ProbedCostas : public dsp::loop::Costas<ORDER> {
        float loopFreq() { return this->pcl.freq; }
    };

    // Mean |im| / mean |re| of the tail of a buffer: how well a Costas loop
    // pushed the constellation onto the real axis.
    double quadratureLeakage(const std::vector<dsp::complex_t>& v, size_t from) {
        double re = 0.0, im = 0.0;
        for (size_t i = from; i < v.size(); i++) {
            re += std::fabs(v[i].re);
            im += std::fabs(v[i].im);
        }
        if (re == 0.0) { return 1e9; }
        return im / re;
    }
}

// ------------------------------------------------------- PhaseControlLoop

TEST_CASE("PhaseControlLoop criticallyDamped produces stable coefficients", "[dsp][loop][pcl]") {
    float alpha = 0.0f, beta = 0.0f;
    dsp::loop::PhaseControlLoop<float>::criticallyDamped(0.01f, alpha, beta);

    // Closed form from the implementation, restated independently here so a
    // rewrite that changes the damping factor is caught.
    const double damp = std::sqrt(2.0) / 2.0;
    const double bw = 0.01;
    const double denom = 1.0 + 2.0 * damp * bw + bw * bw;
    REQUIRE(alpha == Approx(4.0 * damp * bw / denom).epsilon(1e-4));
    REQUIRE(beta == Approx(4.0 * bw * bw / denom).epsilon(1e-4));

    // Both gains must be positive and small for a narrow loop.
    REQUIRE(alpha > 0.0f);
    REQUIRE(beta > 0.0f);
    REQUIRE(alpha < 1.0f);
    REQUIRE(beta < alpha);
}

TEST_CASE("PhaseControlLoop criticallyDamped gains grow with bandwidth", "[dsp][loop][pcl]") {
    float a1, b1, a2, b2;
    dsp::loop::PhaseControlLoop<float>::criticallyDamped(0.001f, a1, b1);
    dsp::loop::PhaseControlLoop<float>::criticallyDamped(0.1f, a2, b2);
    REQUIRE(a2 > a1);
    REQUIRE(b2 > b1);
}

TEST_CASE("PhaseControlLoop advance applies the PI update", "[dsp][loop][pcl]") {
    dsp::loop::PhaseControlLoop<float> pcl;
    // Wide phase limits so no wrapping interferes with the arithmetic check.
    pcl.init(0.5f, 0.25f, 0.0f, -1000.0f, 1000.0f, 0.0f, -10.0f, 10.0f);

    pcl.advance(1.0f);
    // freq += beta*err = 0.25 ; phase += freq + alpha*err = 0.25 + 0.5
    REQUIRE(pcl.freq == Approx(0.25f));
    REQUIRE(pcl.phase == Approx(0.75f));

    pcl.advance(-1.0f);
    // freq = 0.25 - 0.25 = 0 ; phase = 0.75 + 0 - 0.5 = 0.25
    REQUIRE(pcl.freq == Approx(0.0f).margin(1e-6));
    REQUIRE(pcl.phase == Approx(0.25f));
}

TEST_CASE("PhaseControlLoop advancePhase only integrates the frequency", "[dsp][loop][pcl]") {
    dsp::loop::PhaseControlLoop<float> pcl;
    pcl.init(0.5f, 0.25f, 0.0f, -1000.0f, 1000.0f, 0.3f, -10.0f, 10.0f);

    pcl.advancePhase();
    pcl.advancePhase();
    REQUIRE(pcl.freq == Approx(0.3f));
    REQUIRE(pcl.phase == Approx(0.6f));
}

TEST_CASE("PhaseControlLoop clamps the frequency to its limits", "[dsp][loop][pcl]") {
    dsp::loop::PhaseControlLoop<float> pcl;
    pcl.init(0.0f, 1.0f, 0.0f, -1000.0f, 1000.0f, 0.0f, -0.5f, 0.5f);

    for (int i = 0; i < 10; i++) { pcl.advance(1.0f); }
    REQUIRE(pcl.freq == Approx(0.5f));

    for (int i = 0; i < 20; i++) { pcl.advance(-1.0f); }
    REQUIRE(pcl.freq == Approx(-0.5f));
}

TEST_CASE("PhaseControlLoop wraps the phase into its range", "[dsp][loop][pcl]") {
    dsp::loop::PhaseControlLoop<float> pcl;
    pcl.init(0.0f, 0.0f, 0.0f, -FL_M_PI, FL_M_PI, 1.0f, -10.0f, 10.0f);

    // Integrating 1 rad per sample, the phase must stay in [-pi, pi].
    for (int i = 0; i < 100; i++) {
        pcl.advancePhase();
        REQUIRE(pcl.phase >= -FL_M_PI - 1e-4f);
        REQUIRE(pcl.phase <= FL_M_PI + 1e-4f);
    }
}

TEST_CASE("PhaseControlLoop with CLAMP_PHASE off lets the phase run free", "[dsp][loop][pcl]") {
    // This is the variant the clock recovery blocks use: they need the integer
    // part of the phase as a sample offset, so wrapping would break them.
    dsp::loop::PhaseControlLoop<float, false> pcl;
    pcl.init(0.0f, 0.0f, 0.0f, -FL_M_PI, FL_M_PI, 1.0f, -10.0f, 10.0f);

    for (int i = 0; i < 100; i++) { pcl.advancePhase(); }
    REQUIRE(pcl.phase == Approx(100.0f));
}

TEST_CASE("PhaseControlLoop setFreqLimits clamps the current frequency", "[dsp][loop][pcl]") {
    dsp::loop::PhaseControlLoop<float> pcl;
    pcl.init(0.0f, 0.0f, 0.0f, -FL_M_PI, FL_M_PI, 5.0f, -10.0f, 10.0f);
    REQUIRE(pcl.freq == Approx(5.0f));

    pcl.setFreqLimits(-1.0f, 1.0f);
    REQUIRE(pcl.freq == Approx(1.0f));
}

TEST_CASE("PhaseControlLoop setPhaseLimits rewraps the current phase", "[dsp][loop][pcl]") {
    dsp::loop::PhaseControlLoop<float> pcl;
    pcl.init(0.0f, 0.0f, 3.0f, -FL_M_PI, FL_M_PI, 0.0f, -1.0f, 1.0f);

    pcl.setPhaseLimits(0.0f, 1.0f);
    REQUIRE(pcl.phase >= 0.0f);
    REQUIRE(pcl.phase <= 1.0f);
    // The wrap subtracts whole periods while phase is strictly above the
    // maximum, so 3.0 comes to rest on the maximum itself rather than on 0.0.
    REQUIRE(pcl.phase == Approx(1.0f).margin(1e-5));
}

// ------------------------------------------------------------------- PLL

TEST_CASE("PLL locks onto a complex tone", "[dsp][loop][pll]") {
    const double sr = 48000.0;
    const double f = 500.0;
    const int n = 32768;

    ProbedPLL pll;
    pll.init(nullptr, 0.01, 0.0, 0.0);
    pll.out.setBufferSize(1024);

    auto in = complexTone(n, f, sr);
    std::vector<dsp::complex_t> out(n);
    REQUIRE(pll.process(n, in.data(), out.data()) == n);

    // The loop frequency is in radians per sample.
    const double expected = 2.0 * PI * f / sr;
    REQUIRE(pll.loopFreq() == Approx(expected).epsilon(0.05));

    // The output is the recovered carrier: a unit phasor at the input frequency.
    std::vector<dsp::complex_t> tail(out.begin() + n / 2, out.end());
    REQUIRE(goertzelMag(tail, f, sr) == Approx(1.0).margin(0.1));
    for (const auto& s : tail) { REQUIRE(s.amplitude() == Approx(1.0f).margin(1e-3)); }
}

TEST_CASE("PLL tracks a negative frequency too", "[dsp][loop][pll]") {
    const double sr = 48000.0;
    const double f = -700.0;
    const int n = 32768;

    ProbedPLL pll;
    pll.init(nullptr, 0.01, 0.0, 0.0);
    pll.out.setBufferSize(1024);

    auto in = complexTone(n, f, sr);
    std::vector<dsp::complex_t> out(n);
    pll.process(n, in.data(), out.data());

    REQUIRE(pll.loopFreq() == Approx(2.0 * PI * f / sr).epsilon(0.05));
}

TEST_CASE("PLL respects its frequency limits", "[dsp][loop][pll]") {
    const double sr = 48000.0;
    const int n = 16384;

    ProbedPLL pll;
    pll.init(nullptr, 0.05, 0.0, 0.0);
    pll.out.setBufferSize(1024);
    pll.setFrequencyLimits(-0.01, 0.01);

    // Ask it to track something well outside the allowed range.
    auto in = complexTone(n, 5000.0, sr);
    std::vector<dsp::complex_t> out(n);
    pll.process(n, in.data(), out.data());

    REQUIRE(pll.loopFreq() <= 0.01f + 1e-6f);
    REQUIRE(pll.loopFreq() >= -0.01f - 1e-6f);
}

TEST_CASE("PLL reset returns the loop to its initial state", "[dsp][loop][pll]") {
    const double sr = 48000.0;
    const int n = 4096;

    ProbedPLL pll;
    pll.init(nullptr, 0.01, 0.0, 0.0);
    pll.out.setBufferSize(1024);

    auto in = complexTone(n, 1000.0, sr);
    std::vector<dsp::complex_t> a(n), b(n);

    pll.process(n, in.data(), a.data());
    REQUIRE(pll.loopFreq() != Approx(0.0f).margin(1e-9));

    pll.reset();
    REQUIRE(pll.loopFreq() == Approx(0.0f).margin(1e-9));
    REQUIRE(pll.loopPhase() == Approx(0.0f).margin(1e-9));

    pll.process(n, in.data(), b.data());
    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(b[i].re == Approx(a[i].re).margin(1e-4));
        REQUIRE(b[i].im == Approx(a[i].im).margin(1e-4));
    }
}

TEST_CASE("PLL constructor swaps initPhase and initFreq", "[dsp][loop][pll][characterization]") {
    // KNOWN BUG, pinned: the convenience constructor forwards
    // (in, bandwidth, initPhase, initFreq) to init() as
    // (in, bandwidth, initFreq, initPhase) — the two are transposed. Any caller
    // that uses the constructor with a non-zero initial phase or frequency gets
    // the other one. Both are zero by default, which is why it has gone
    // unnoticed. A fix should make this test's expectation the other way round.
    ProbedPLL viaInit;
    viaInit.init(nullptr, 0.01, /*initPhase*/ 1.5, /*initFreq*/ 0.25);
    viaInit.out.setBufferSize(64);
    REQUIRE(viaInit.loopPhase() == Approx(1.5f));
    REQUIRE(viaInit.loopFreq() == Approx(0.25f));

    struct Ctor : public dsp::loop::PLL {
        using dsp::loop::PLL::PLL;
        float loopFreq() { return pcl.freq; }
        float loopPhase() { return pcl.phase; }
    };
    Ctor viaCtor(nullptr, 0.01, /*initPhase*/ 1.5, /*initFreq*/ 0.25);
    viaCtor.out.setBufferSize(64);
    REQUIRE(viaCtor.loopPhase() == Approx(0.25f)); // got initFreq
    REQUIRE(viaCtor.loopFreq() == Approx(1.5f));   // got initPhase
}

// -------------------------------------------------------- CarrierTrackingPLL

TEST_CASE("CarrierTrackingPLL moves the carrier to DC", "[dsp][loop][pll][carrier]") {
    const double sr = 48000.0;
    const double f = 300.0;
    const int n = 32768;

    ProbedCTPLL pll;
    pll.init(nullptr, 0.01, 0.0, 0.0);
    pll.out.setBufferSize(1024);

    auto in = complexTone(n, f, sr);
    std::vector<dsp::complex_t> out(n);
    pll.process(n, in.data(), out.data());

    std::vector<dsp::complex_t> tail(out.begin() + n / 2, out.end());
    // After lock the residual is a constant: nearly all the energy at 0 Hz.
    REQUIRE(goertzelMag(tail, 0.0, sr) == Approx(1.0).margin(0.1));
    REQUIRE(goertzelMag(tail, f, sr) < 0.15);
    REQUIRE(pll.loopFreq() == Approx(2.0 * PI * f / sr).epsilon(0.05));
}

TEST_CASE("CarrierTrackingPLL preserves amplitude", "[dsp][loop][pll][carrier]") {
    const double sr = 48000.0;
    const int n = 8192;

    ProbedCTPLL pll;
    pll.init(nullptr, 0.01, 0.0, 0.0);
    pll.out.setBufferSize(1024);

    auto in = complexTone(n, 200.0, sr, 0.4);
    std::vector<dsp::complex_t> out(n);
    pll.process(n, in.data(), out.data());

    for (const auto& s : out) { REQUIRE(s.amplitude() == Approx(0.4f).margin(1e-3)); }
}

// ---------------------------------------------------------------- Costas

TEST_CASE("Costas order 2 removes a carrier offset from BPSK", "[dsp][loop][costas]") {
    const double sr = 48000.0;
    const double offset = 200.0;
    const int sps = 8;
    const int symbols = 2048;
    const int n = sps * symbols;

    // BPSK: +-1 symbols held for sps samples, rotated by a carrier offset.
    auto bits = noise(symbols, 4242);
    std::vector<dsp::complex_t> in(n);
    auto carrier = complexTone(n, offset, sr);
    for (int s = 0; s < symbols; s++) {
        float sym = bits[s] >= 0.0f ? 1.0f : -1.0f;
        for (int k = 0; k < sps; k++) {
            in[s * sps + k] = carrier[s * sps + k] * sym;
        }
    }

    ProbedCostas<2> costas;
    costas.init(nullptr, 0.005, 0.0, 0.0);
    costas.out.setBufferSize(1024);

    std::vector<dsp::complex_t> out(n);
    REQUIRE(costas.process(n, in.data(), out.data()) == n);

    REQUIRE(costas.loopFreq() == Approx(2.0 * PI * offset / sr).epsilon(0.1));
    // The constellation must end up on the real axis.
    REQUIRE(quadratureLeakage(out, n / 2) < 0.2);
}

TEST_CASE("Costas order 4 removes a carrier offset from QPSK", "[dsp][loop][costas]") {
    const double sr = 48000.0;
    const double offset = 150.0;
    const int sps = 8;
    const int symbols = 2048;
    const int n = sps * symbols;

    auto a = noise(symbols, 77);
    auto b = noise(symbols, 991);
    auto carrier = complexTone(n, offset, sr);
    std::vector<dsp::complex_t> in(n);
    const float k = 0.70710678f;
    for (int s = 0; s < symbols; s++) {
        dsp::complex_t sym{ a[s] >= 0.0f ? k : -k, b[s] >= 0.0f ? k : -k };
        for (int j = 0; j < sps; j++) { in[s * sps + j] = carrier[s * sps + j] * sym; }
    }

    ProbedCostas<4> costas;
    costas.init(nullptr, 0.005, 0.0, 0.0);
    costas.out.setBufferSize(1024);

    std::vector<dsp::complex_t> out(n);
    costas.process(n, in.data(), out.data());

    REQUIRE(costas.loopFreq() == Approx(2.0 * PI * offset / sr).epsilon(0.15));

    // A locked order-4 loop puts every symbol near a corner of the unit square:
    // |re| and |im| should both be close to k.
    double err = 0.0;
    for (int i = n / 2; i < n; i++) {
        err += std::fabs(std::fabs(out[i].re) - k) + std::fabs(std::fabs(out[i].im) - k);
    }
    REQUIRE(err / (double)(n / 2) < 0.25);
}

TEST_CASE("Costas order 8 compiles and locks", "[dsp][loop][costas]") {
    const double sr = 48000.0;
    const int n = 16384;

    ProbedCostas<8> costas;
    costas.init(nullptr, 0.005, 0.0, 0.0);
    costas.out.setBufferSize(1024);

    auto in = complexTone(n, 100.0, sr);
    std::vector<dsp::complex_t> out(n);
    REQUIRE(costas.process(n, in.data(), out.data()) == n);
    REQUIRE(costas.loopFreq() == Approx(2.0 * PI * 100.0 / sr).epsilon(0.2));
}

TEST_CASE("Costas leaves an already aligned signal alone", "[dsp][loop][costas]") {
    // Zero frequency offset: the loop should stay near zero and pass the signal
    // through essentially untouched.
    const int n = 8192;
    ProbedCostas<2> costas;
    costas.init(nullptr, 0.005, 0.0, 0.0);
    costas.out.setBufferSize(1024);

    std::vector<dsp::complex_t> in(n);
    auto bits = noise(n / 8, 31337);
    for (int i = 0; i < n; i++) { in[i] = { bits[i / 8] >= 0.0f ? 1.0f : -1.0f, 0.0f }; }

    std::vector<dsp::complex_t> out(n);
    costas.process(n, in.data(), out.data());

    REQUIRE(std::fabs(costas.loopFreq()) < 0.01f);
    REQUIRE(quadratureLeakage(out, n / 2) < 0.1);
}
