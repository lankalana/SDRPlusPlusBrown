// Demodulators. Each test builds a modulated signal, demodulates it and checks
// the recovered baseband, which is the only property that survives a rewrite of
// the internals.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/demod/am.h>
#include <dsp/demod/quadrature.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    // FM-modulates `message` onto a complex carrier at baseband.
    std::vector<dsp::complex_t> fmModulate(const std::vector<float>& message, double deviation, double sampleRate) {
        std::vector<dsp::complex_t> out(message.size());
        double phase = 0.0;
        for (size_t i = 0; i < message.size(); i++) {
            phase += 2.0 * PI * deviation * (double)message[i] / sampleRate;
            out[i] = { (float)std::cos(phase), (float)std::sin(phase) };
        }
        return out;
    }
}

TEST_CASE("Quadrature recovers a constant frequency offset", "[dsp][demod][fm]") {
    const double sr = 48000.0;
    const double deviation = 5000.0;

    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(16384);

    dsp::demod::Quadrature demod;
    demod.init(&in, deviation, sr);
    demod.out.setBufferSize(16384);

    // A pure tone at +2500 Hz is a constant frequency of half the deviation.
    const int N = 8192;
    auto input = complexTone(N, 2500.0, sr);
    std::vector<float> out(N);
    REQUIRE(demod.process(N, input.data(), out.data()) == N);

    // Skip the first sample: the phase memory starts at zero.
    std::vector<float> tail(out.begin() + 1, out.end());
    REQUIRE(mean(tail) == Approx(0.5).epsilon(0.01));
}

TEST_CASE("Quadrature output is symmetric in the offset sign", "[dsp][demod][fm]") {
    const double sr = 48000.0;
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(16384);

    dsp::demod::Quadrature demod;
    demod.init(&in, 5000.0, sr);
    demod.out.setBufferSize(16384);

    const int N = 8192;
    std::vector<float> out(N);

    auto pos = complexTone(N, 2500.0, sr);
    demod.process(N, pos.data(), out.data());
    double up = mean(std::vector<float>(out.begin() + 1, out.end()));

    demod.reset();
    auto neg = complexTone(N, -2500.0, sr);
    demod.process(N, neg.data(), out.data());
    double down = mean(std::vector<float>(out.begin() + 1, out.end()));

    REQUIRE(up == Approx(-down).epsilon(0.01));
}

TEST_CASE("Quadrature demodulates an FM message", "[dsp][demod][fm]") {
    const double sr = 48000.0;
    const double deviation = 5000.0;
    const double toneFreq = 1000.0;

    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(1 << 16);

    dsp::demod::Quadrature demod;
    demod.init(&in, deviation, sr);
    demod.out.setBufferSize(1 << 16);

    const int N = 1 << 14;
    auto message = cosine(N, toneFreq, sr, 0.5);
    auto modulated = fmModulate(message, deviation, sr);

    std::vector<float> out(N);
    demod.process(N, modulated.data(), out.data());

    std::vector<float> tail(out.begin() + 64, out.end());
    // The recovered message is the original, at the modulation level.
    REQUIRE(goertzelMag(tail, toneFreq, sr) == Approx(0.25).epsilon(0.05));
    REQUIRE(rms(tail) == Approx(0.5 / std::sqrt(2.0)).epsilon(0.05));
}

TEST_CASE("Quadrature setDeviation rescales the output", "[dsp][demod][fm]") {
    const double sr = 48000.0;
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(16384);

    dsp::demod::Quadrature demod;
    demod.init(&in, 5000.0, sr);
    demod.out.setBufferSize(16384);

    const int N = 8192;
    auto input = complexTone(N, 2500.0, sr);
    std::vector<float> out(N);

    demod.process(N, input.data(), out.data());
    double wide = mean(std::vector<float>(out.begin() + 1, out.end()));

    demod.reset();
    demod.setDeviation(2500.0, sr);
    demod.process(N, input.data(), out.data());
    double narrow = mean(std::vector<float>(out.begin() + 1, out.end()));

    REQUIRE(narrow == Approx(wide * 2.0).epsilon(0.01));
}

TEST_CASE("Quadrature reset clears the phase memory", "[dsp][demod][fm]") {
    const double sr = 48000.0;
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(16384);

    dsp::demod::Quadrature demod;
    demod.init(&in, 5000.0, sr);
    demod.out.setBufferSize(16384);

    auto input = complexTone(256, 2500.0, sr);
    std::vector<float> first(256), second(256);

    demod.process(256, input.data(), first.data());
    demod.reset();
    demod.process(256, input.data(), second.data());

    for (int i = 0; i < 256; i++) { REQUIRE(second[i] == Approx(first[i]).margin(1e-6)); }
}

TEST_CASE("AM recovers the envelope of a modulated carrier", "[dsp][demod][am]") {
    const double sr = 48000.0;
    const double toneFreq = 1000.0;
    const double depth = 0.5;

    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(1 << 16);

    dsp::demod::AM<float> am;
    // Carrier AGC keeps the carrier at unity so the recovered tone level is
    // predictable.
    am.init(&in, dsp::demod::AM<float>::CARRIER, 10000.0, 0.01, 0.01, 0.001, sr);
    am.out.setBufferSize(1 << 16);

    const int N = 1 << 15;
    std::vector<dsp::complex_t> input(N);
    for (int i = 0; i < N; i++) {
        double env = 1.0 + depth * std::cos(2.0 * PI * toneFreq * (double)i / sr);
        input[i] = { (float)env, 0.0f };
    }

    std::vector<float> out(N);
    REQUIRE(am.process(N, input.data(), out.data()) == N);

    // Skip the AGC/DC-blocker transient.
    std::vector<float> tail(out.begin() + N / 2, out.end());
    REQUIRE(std::fabs(mean(tail)) < 0.05);                      // carrier removed
    REQUIRE(goertzelMag(tail, toneFreq, sr) > 0.1);             // tone recovered
    REQUIRE(goertzelMag(tail, 2.0 * toneFreq, sr) < 0.05);      // no strong harmonic
}

TEST_CASE("AM rejects a tone outside the selected bandwidth", "[dsp][demod][am]") {
    const double sr = 48000.0;
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(1 << 16);

    dsp::demod::AM<float> am;
    am.init(&in, dsp::demod::AM<float>::CARRIER, 4000.0, 0.01, 0.01, 0.001, sr);
    am.out.setBufferSize(1 << 16);

    const int N = 1 << 15;

    auto envelopeAt = [&](double toneFreq) {
        am.reset();
        std::vector<dsp::complex_t> input(N);
        for (int i = 0; i < N; i++) {
            double env = 1.0 + 0.5 * std::cos(2.0 * PI * toneFreq * (double)i / sr);
            input[i] = { (float)env, 0.0f };
        }
        std::vector<float> out(N);
        am.process(N, input.data(), out.data());
        std::vector<float> tail(out.begin() + N / 2, out.end());
        return goertzelMag(tail, toneFreq, sr);
    };

    double inBand = envelopeAt(1000.0);
    double outOfBand = envelopeAt(8000.0); // above the 4 kHz filter (2 kHz cutoff)

    REQUIRE(inBand > 0.1);
    REQUIRE(outOfBand < inBand * 0.05);
}

TEST_CASE("AM produces stereo output on request", "[dsp][demod][am]") {
    const double sr = 48000.0;
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(1 << 15);

    dsp::demod::AM<dsp::stereo_t> am;
    am.init(&in, dsp::demod::AM<dsp::stereo_t>::AUDIO, 10000.0, 0.01, 0.01, 0.001, sr);
    am.out.setBufferSize(1 << 15);

    const int N = 1 << 13;
    std::vector<dsp::complex_t> input(N);
    for (int i = 0; i < N; i++) {
        double env = 1.0 + 0.5 * std::cos(2.0 * PI * 1000.0 * (double)i / sr);
        input[i] = { (float)env, 0.0f };
    }

    std::vector<dsp::stereo_t> out(N);
    REQUIRE(am.process(N, input.data(), out.data()) == N);

    // Both channels carry the same mono audio.
    for (int i = N / 2; i < N; i++) { REQUIRE(out[i].l == out[i].r); }
}

TEST_CASE("AM setBandwidth reconfigures the low pass", "[dsp][demod][am]") {
    const double sr = 48000.0;
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(1 << 16);

    dsp::demod::AM<float> am;
    am.init(&in, dsp::demod::AM<float>::CARRIER, 10000.0, 0.01, 0.01, 0.001, sr);
    am.out.setBufferSize(1 << 16);

    const int N = 1 << 15;
    std::vector<dsp::complex_t> input(N);
    for (int i = 0; i < N; i++) {
        double env = 1.0 + 0.5 * std::cos(2.0 * PI * 4000.0 * (double)i / sr);
        input[i] = { (float)env, 0.0f };
    }

    std::vector<float> out(N);
    am.process(N, input.data(), out.data());
    double wide = goertzelMag(std::vector<float>(out.begin() + N / 2, out.end()), 4000.0, sr);

    // Setting the same bandwidth again is a no-op and must not free live taps.
    am.setBandwidth(10000.0);

    am.setBandwidth(2000.0); // 1 kHz cutoff: the 4 kHz tone is now rejected
    am.reset();
    am.process(N, input.data(), out.data());
    double narrow = goertzelMag(std::vector<float>(out.begin() + N / 2, out.end()), 4000.0, sr);

    REQUIRE(wide > 0.1);
    REQUIRE(narrow < wide * 0.1);
}
