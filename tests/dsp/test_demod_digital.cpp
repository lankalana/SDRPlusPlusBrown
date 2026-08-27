// The digital demodulators: dsp::demod::PSK and dsp::demod::GFSK.
//
// Both are compositions of RRC filtering, AGC/Costas and clock recovery, so the
// only meaningful assertion is end to end: modulate a known symbol sequence,
// demodulate it, and require that most of the symbols come back. Sample-exact
// expectations would just re-encode today's tuning constants.
//
// These allocate several megabyte-sized work buffers per instance (the clock
// recovery blocks size theirs off STREAM_BUFFER_SIZE), so the test cases keep
// the number of live demodulators to one at a time.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <dsp/demod/gfsk.h>
#include <dsp/demod/psk.h>
#include <dsp/mod/gfsk.h>
#include <dsp/mod/psk.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    std::vector<float> randomBits(int count, unsigned seed) {
        auto n = noise(count, seed);
        std::vector<float> out(count);
        for (int i = 0; i < count; i++) { out[i] = n[i] >= 0.0f ? 1.0f : -1.0f; }
        return out;
    }

    // Fraction of matching signs, searching over a symbol lag. The lag has to
    // cover the whole chain's group delay: the modulator's RRC interpolator
    // alone is 31 symbols of taps, so about 15 symbols, and the demodulator's
    // RRC and interpolating clock recovery add a few more. BPSK and Costas
    // loops both have a 180 degree ambiguity, so an inverted match counts just
    // as much as a direct one.
    double bestAgreement(const std::vector<float>& got, const std::vector<float>& want, int maxLag = 48) {
        double best = 0.0;
        for (int lag = 0; lag <= maxLag; lag++) {
            if ((size_t)lag >= got.size()) { break; }
            int n = (int)std::min(got.size() - lag, want.size());
            if (n < 16) { continue; }
            int hits = 0;
            for (int i = 0; i < n; i++) {
                if ((got[i + lag] >= 0.0f) == (want[i] >= 0.0f)) { hits++; }
            }
            double frac = (double)hits / (double)n;
            best = std::max(best, std::max(frac, 1.0 - frac));
        }
        return best;
    }
}

TEST_CASE("BPSK modulates and demodulates back to the same symbols", "[dsp][demod][psk][roundtrip]") {
    const double symbolrate = 1200.0;
    const double samplerate = 9600.0;
    const int symbols = 1024;

    auto bits = randomBits(symbols, 8080);

    // Modulate.
    std::vector<dsp::complex_t> syms(symbols);
    for (int i = 0; i < symbols; i++) { syms[i] = { bits[i], 0.0f }; }

    std::vector<dsp::complex_t> iq(symbols * 16);
    int iqCount = 0;
    {
        dsp::mod::PSK mod;
        mod.init(nullptr, symbolrate, samplerate, 0.35, 31);
        mod.out.setBufferSize(1024);
        iqCount = mod.process(symbols, syms.data(), iq.data());
    }
    REQUIRE(iqCount == Approx(symbols * 8).epsilon(0.05));

    // Demodulate.
    dsp::demod::PSK<2> demod;
    demod.init(nullptr, symbolrate, samplerate, 31, 0.35, 1e-3, 0.01, 1e-4, 0.01);
    demod.out.setBufferSize(4096);

    std::vector<dsp::complex_t> out(iqCount);
    int produced = demod.process(iqCount, iq.data(), out.data());
    REQUIRE(produced == Approx(symbols).epsilon(0.15));

    // Skip the acquisition transient.
    const int skip = 64;
    std::vector<float> re;
    for (int i = skip; i < produced; i++) { re.push_back(out[i].re); }
    std::vector<float> want(bits.begin() + skip, bits.end());

    REQUIRE(bestAgreement(re, want) > 0.9);
}

TEST_CASE("demod::PSK setSymbolrate changes the output rate", "[dsp][demod][psk]") {
    const double samplerate = 9600.0;
    const int n = 8192;

    dsp::demod::PSK<2> demod;
    demod.init(nullptr, 1200.0, samplerate, 31, 0.35, 1e-3, 0.01, 1e-4, 0.01);
    demod.out.setBufferSize(4096);

    std::vector<dsp::complex_t> in(n);
    for (int i = 0; i < n; i++) { in[i] = { (i / 8) % 2 ? 1.0f : -1.0f, 0.0f }; }
    std::vector<dsp::complex_t> out(n);

    int at1200 = demod.process(n, in.data(), out.data());
    REQUIRE(at1200 == Approx(n / 8).epsilon(0.15));

    demod.setSymbolrate(2400.0);
    demod.reset();
    int at2400 = demod.process(n, in.data(), out.data());
    REQUIRE(at2400 == Approx(n / 4).epsilon(0.15));
}

TEST_CASE("demod::PSK reset makes the block reproducible", "[dsp][demod][psk]") {
    const int n = 4096;
    dsp::demod::PSK<2> demod;
    demod.init(nullptr, 1200.0, 9600.0, 31, 0.35, 1e-3, 0.01, 1e-4, 0.01);
    demod.out.setBufferSize(4096);

    std::vector<dsp::complex_t> in(n);
    auto bits = randomBits(n / 8 + 1, 4242);
    for (int i = 0; i < n; i++) { in[i] = { bits[i / 8], 0.0f }; }

    std::vector<dsp::complex_t> a(n), b(n);
    int ca = demod.process(n, in.data(), a.data());
    demod.reset();
    int cb = demod.process(n, in.data(), b.data());

    REQUIRE(ca == cb);
    for (int i = 0; i < ca; i++) {
        INFO("symbol " << i);
        REQUIRE(b[i].re == Approx(a[i].re).margin(1e-4));
        REQUIRE(b[i].im == Approx(a[i].im).margin(1e-4));
    }
}

TEST_CASE("QPSK demodulation produces four constellation points", "[dsp][demod][psk]") {
    const double symbolrate = 1200.0;
    const double samplerate = 9600.0;
    const int symbols = 1024;

    auto a = randomBits(symbols, 11);
    auto b = randomBits(symbols, 22);
    const float k = 0.70710678f;

    std::vector<dsp::complex_t> syms(symbols);
    for (int i = 0; i < symbols; i++) { syms[i] = { a[i] * k, b[i] * k }; }

    std::vector<dsp::complex_t> iq(symbols * 16);
    int iqCount = 0;
    {
        dsp::mod::PSK mod;
        mod.init(nullptr, symbolrate, samplerate, 0.35, 31);
        mod.out.setBufferSize(1024);
        iqCount = mod.process(symbols, syms.data(), iq.data());
    }

    dsp::demod::PSK<4> demod;
    demod.init(nullptr, symbolrate, samplerate, 31, 0.35, 1e-3, 0.01, 1e-4, 0.01);
    demod.out.setBufferSize(4096);

    std::vector<dsp::complex_t> out(iqCount);
    int produced = demod.process(iqCount, iq.data(), out.data());
    REQUIRE(produced == Approx(symbols).epsilon(0.15));

    // Most recovered symbols must sit near a corner rather than on an axis or at
    // the origin: |re| and |im| both well away from zero.
    int good = 0, total = 0;
    for (int i = produced / 4; i < produced; i++) {
        total++;
        float m = std::min(std::fabs(out[i].re), std::fabs(out[i].im));
        float M = std::max(std::fabs(out[i].re), std::fabs(out[i].im));
        if (M > 1e-6f && m / M > 0.4f) { good++; }
    }
    REQUIRE(total > 0);
    REQUIRE((double)good / (double)total > 0.75);
}

TEST_CASE("GFSK modulates and demodulates back to the same symbols", "[dsp][demod][gfsk][roundtrip]") {
    const double symbolrate = 1200.0;
    const double samplerate = 9600.0;
    const double deviation = 2400.0;
    const int symbols = 1024;

    auto bits = randomBits(symbols, 3141);

    std::vector<dsp::complex_t> iq(symbols * 16);
    int iqCount = 0;
    {
        dsp::mod::GFSK mod;
        mod.init(nullptr, symbolrate, samplerate, 0.5, 31, deviation);
        mod.out.setBufferSize(1024);
        iqCount = mod.process(symbols, bits.data(), iq.data());
    }
    REQUIRE(iqCount == Approx(symbols * 8).epsilon(0.05));

    dsp::demod::GFSK demod;
    demod.init(nullptr, symbolrate, samplerate, deviation, 31, 0.5, 1e-4, 0.01, 0.01);
    demod.out.setBufferSize(4096);

    std::vector<float> out(iqCount);
    int produced = demod.process(iqCount, iq.data(), out.data());
    REQUIRE(produced == Approx(symbols).epsilon(0.15));

    const int skip = 64;
    std::vector<float> got(out.begin() + skip, out.begin() + produced);
    std::vector<float> want(bits.begin() + skip, bits.end());
    REQUIRE(bestAgreement(got, want) > 0.9);
}

TEST_CASE("demod::GFSK setDeviation scales the recovered symbols", "[dsp][demod][gfsk]") {
    const double symbolrate = 1200.0;
    const double samplerate = 9600.0;
    const int symbols = 512;

    auto bits = randomBits(symbols, 271828);

    std::vector<dsp::complex_t> iq(symbols * 16);
    int iqCount = 0;
    {
        dsp::mod::GFSK mod;
        mod.init(nullptr, symbolrate, samplerate, 0.5, 31, 2400.0);
        mod.out.setBufferSize(1024);
        iqCount = mod.process(symbols, bits.data(), iq.data());
    }

    dsp::demod::GFSK demod;
    demod.init(nullptr, symbolrate, samplerate, 2400.0, 31, 0.5, 1e-4, 0.01, 0.01);
    demod.out.setBufferSize(4096);

    std::vector<float> matched(iqCount), halved(iqCount);
    int cm = demod.process(iqCount, iq.data(), matched.data());

    demod.setDeviation(4800.0); // twice the real deviation
    demod.reset();
    int ch = demod.process(iqCount, iq.data(), halved.data());

    REQUIRE(cm > 0);
    REQUIRE(ch > 0);

    auto meanAbs = [](const std::vector<float>& v, int from, int to) {
        double acc = 0.0;
        for (int i = from; i < to; i++) { acc += std::fabs(v[i]); }
        return acc / (double)(to - from);
    };

    double a = meanAbs(matched, cm / 4, cm);
    double b = meanAbs(halved, ch / 4, ch);
    REQUIRE(b == Approx(a / 2.0).epsilon(0.15));
}

TEST_CASE("demod::GFSK reset makes the block reproducible", "[dsp][demod][gfsk]") {
    const int symbols = 256;
    auto bits = randomBits(symbols, 999);

    std::vector<dsp::complex_t> iq(symbols * 16);
    int iqCount = 0;
    {
        dsp::mod::GFSK mod;
        mod.init(nullptr, 1200.0, 9600.0, 0.5, 31, 2400.0);
        mod.out.setBufferSize(1024);
        iqCount = mod.process(symbols, bits.data(), iq.data());
    }

    dsp::demod::GFSK demod;
    demod.init(nullptr, 1200.0, 9600.0, 2400.0, 31, 0.5, 1e-4, 0.01, 0.01);
    demod.out.setBufferSize(4096);

    std::vector<float> a(iqCount), b(iqCount);
    int ca = demod.process(iqCount, iq.data(), a.data());
    demod.reset();
    int cb = demod.process(iqCount, iq.data(), b.data());

    REQUIRE(ca == cb);
    for (int i = 0; i < ca; i++) {
        INFO("symbol " << i);
        REQUIRE(b[i] == Approx(a[i]).margin(1e-4));
    }
}
