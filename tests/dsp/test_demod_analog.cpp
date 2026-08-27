// The analog demodulators that the radio module exposes: FM (with its optional
// de-emphasis-adjacent filtering), SSB and CW.
//
// Each of these is a small hierarchy of blocks glued together in process(), so
// the tests focus on the end-to-end behaviour a user would notice: does the
// right audio come out, is the mono path consistent with the stereo one, and do
// the setters actually retune the internals.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/demod/cw.h>
#include <dsp/demod/fm.h>
#include <dsp/demod/ssb.h>
#include <dsp/mod/quadrature.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    std::vector<float> leftChannel(const std::vector<dsp::stereo_t>& v) {
        std::vector<float> out(v.size());
        for (size_t i = 0; i < v.size(); i++) { out[i] = v[i].l; }
        return out;
    }
}

// -------------------------------------------------------------------- FM

TEST_CASE("demod::FM recovers the modulating tone", "[dsp][demod][fm]") {
    const double sr = 48000.0;
    const double bw = 12000.0;
    const double audioFreq = 1000.0;
    const int n = 16384;

    // Build an FM signal with a deviation of bw/2, which is what the
    // demodulator is configured for.
    auto audio = cosine(n, audioFreq, sr, 0.8);
    dsp::mod::Quadrature mod;
    mod.init(nullptr, bw / 2.0, sr);
    mod.out.setBufferSize(64);
    std::vector<dsp::complex_t> iq(n);
    mod.process(n, audio.data(), iq.data());

    dsp::demod::FM<float> fm;
    fm.init(nullptr, sr, bw, false, false);
    fm.out.setBufferSize(64);

    std::vector<float> out(n);
    REQUIRE(fm.process(n, iq.data(), out.data()) == n);

    std::vector<float> tail(out.begin() + 64, out.end());
    // Amplitude 0.8, and a real cosine splits its energy between +-f.
    REQUIRE(goertzelMag(tail, audioFreq, sr) == Approx(0.4).margin(0.05));
    REQUIRE(mean(tail) == Approx(0.0).margin(0.05));
}

TEST_CASE("demod::FM unfiltered mode leaves the audio band untouched", "[dsp][demod][fm]") {
    const double sr = 48000.0;
    const double bw = 16000.0;
    const int n = 16384;

    // 200 Hz is below the 300 Hz high-pass corner, so the filtered and
    // unfiltered paths must differ.
    auto audio = cosine(n, 200.0, sr, 0.5);
    dsp::mod::Quadrature mod;
    mod.init(nullptr, bw / 2.0, sr);
    mod.out.setBufferSize(64);
    std::vector<dsp::complex_t> iq(n);
    mod.process(n, audio.data(), iq.data());

    dsp::demod::FM<float> plain;
    plain.init(nullptr, sr, bw, false, false);
    plain.out.setBufferSize(64);
    std::vector<float> plainOut(n);
    plain.process(n, iq.data(), plainOut.data());

    dsp::demod::FM<float> highPassed;
    highPassed.init(nullptr, sr, bw, false, true);
    highPassed.out.setBufferSize(64);
    std::vector<float> hpOut(n);
    highPassed.process(n, iq.data(), hpOut.data());

    std::vector<float> plainTail(plainOut.begin() + n / 2, plainOut.end());
    std::vector<float> hpTail(hpOut.begin() + n / 2, hpOut.end());

    REQUIRE(goertzelMag(plainTail, 200.0, sr) > 0.15);
    REQUIRE(goertzelMag(hpTail, 200.0, sr) < goertzelMag(plainTail, 200.0, sr) * 0.5);
}

TEST_CASE("demod::FM low pass rejects tones above the bandwidth", "[dsp][demod][fm]") {
    const double sr = 96000.0;
    const double bw = 8000.0; // low pass at 4 kHz
    const int n = 32768;

    // Modulate with a tone above the low-pass corner.
    auto audio = cosine(n, 12000.0, sr, 0.3);
    dsp::mod::Quadrature mod;
    mod.init(nullptr, bw / 2.0, sr);
    mod.out.setBufferSize(64);
    std::vector<dsp::complex_t> iq(n);
    mod.process(n, audio.data(), iq.data());

    dsp::demod::FM<float> plain;
    plain.init(nullptr, sr, bw, false, false);
    plain.out.setBufferSize(64);
    std::vector<float> plainOut(n);
    plain.process(n, iq.data(), plainOut.data());

    dsp::demod::FM<float> lowPassed;
    lowPassed.init(nullptr, sr, bw, true, false);
    lowPassed.out.setBufferSize(64);
    std::vector<float> lpOut(n);
    lowPassed.process(n, iq.data(), lpOut.data());

    std::vector<float> plainTail(plainOut.begin() + n / 2, plainOut.end());
    std::vector<float> lpTail(lpOut.begin() + n / 2, lpOut.end());

    REQUIRE(goertzelMag(lpTail, 12000.0, sr) < goertzelMag(plainTail, 12000.0, sr) * 0.2);
}

TEST_CASE("demod::FM stereo output duplicates the mono output", "[dsp][demod][fm]") {
    const double sr = 48000.0;
    const double bw = 12000.0;
    const int n = 8192;

    auto audio = cosine(n, 900.0, sr, 0.6);
    dsp::mod::Quadrature mod;
    mod.init(nullptr, bw / 2.0, sr);
    mod.out.setBufferSize(64);
    std::vector<dsp::complex_t> iq(n);
    mod.process(n, audio.data(), iq.data());

    dsp::demod::FM<float> mono;
    mono.init(nullptr, sr, bw, true, true);
    mono.out.setBufferSize(64);
    std::vector<float> monoOut(n);
    mono.process(n, iq.data(), monoOut.data());

    dsp::demod::FM<dsp::stereo_t> stereo;
    stereo.init(nullptr, sr, bw, true, true);
    stereo.out.setBufferSize(64);
    std::vector<dsp::stereo_t> stereoOut(n);
    stereo.process(n, iq.data(), stereoOut.data());

    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(stereoOut[i].l == Approx(monoOut[i]).margin(1e-5));
        REQUIRE(stereoOut[i].r == Approx(monoOut[i]).margin(1e-5));
    }
}

TEST_CASE("demod::FM setBandwidth rescales the recovered amplitude", "[dsp][demod][fm]") {
    // The demodulator normalizes by the deviation, so halving the configured
    // bandwidth doubles the amplitude of a fixed-deviation signal.
    const double sr = 48000.0;
    const int n = 16384;

    auto audio = cosine(n, 1000.0, sr, 0.5);
    dsp::mod::Quadrature mod;
    mod.init(nullptr, 6000.0, sr); // fixed deviation of 6 kHz
    mod.out.setBufferSize(64);
    std::vector<dsp::complex_t> iq(n);
    mod.process(n, audio.data(), iq.data());

    dsp::demod::FM<float> fm;
    fm.init(nullptr, sr, 12000.0, false, false); // matches: deviation 6 kHz
    fm.out.setBufferSize(64);
    std::vector<float> matched(n);
    fm.process(n, iq.data(), matched.data());

    fm.setBandwidth(6000.0); // now expects 3 kHz deviation
    fm.reset();
    std::vector<float> narrow(n);
    fm.process(n, iq.data(), narrow.data());

    std::vector<float> a(matched.begin() + 64, matched.end());
    std::vector<float> b(narrow.begin() + 64, narrow.end());
    REQUIRE(goertzelMag(b, 1000.0, sr) == Approx(2.0 * goertzelMag(a, 1000.0, sr)).epsilon(0.05));
}

TEST_CASE("demod::FM reset makes the block reproducible", "[dsp][demod][fm]") {
    const double sr = 48000.0;
    const int n = 4096;

    auto audio = noise(n, 606, 0.5f);
    dsp::mod::Quadrature mod;
    mod.init(nullptr, 6000.0, sr);
    mod.out.setBufferSize(64);
    std::vector<dsp::complex_t> iq(n);
    mod.process(n, audio.data(), iq.data());

    dsp::demod::FM<float> fm;
    fm.init(nullptr, sr, 12000.0, true, true);
    fm.out.setBufferSize(64);

    std::vector<float> a(n), b(n);
    fm.process(n, iq.data(), a.data());
    fm.reset();
    fm.process(n, iq.data(), b.data());

    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(b[i] == Approx(a[i]).margin(1e-4));
    }
}

// ------------------------------------------------------------------- SSB

TEST_CASE("demod::SSB USB shifts the upper sideband down to audio", "[dsp][demod][ssb]") {
    const double sr = 24000.0;
    const double bw = 3000.0;
    const int n = 32768;

    // A tone 1 kHz above the VFO centre. In USB the demodulator translates by
    // +bw/2, so the tone should land at bw/2 + 1000 = 2500 Hz of audio.
    auto in = complexTone(n, 1000.0, sr, 0.5);

    dsp::demod::SSB<float> ssb;
    ssb.init(nullptr, dsp::demod::SSB<float>::USB, bw, sr, 0.1, 0.1);
    ssb.out.setBufferSize(64);

    std::vector<float> out(n);
    REQUIRE(ssb.process(n, in.data(), out.data()) == n);

    std::vector<float> tail(out.begin() + n / 2, out.end());
    double wanted = goertzelMag(tail, 2500.0, sr);
    REQUIRE(wanted > 0.1);
    // Nothing significant anywhere else in the audio band.
    REQUIRE(goertzelMag(tail, 500.0, sr) < wanted * 0.2);
    REQUIRE(goertzelMag(tail, 5000.0, sr) < wanted * 0.2);
}

TEST_CASE("demod::SSB LSB translates the other way", "[dsp][demod][ssb]") {
    const double sr = 24000.0;
    const double bw = 3000.0;
    const int n = 32768;

    auto in = complexTone(n, -1000.0, sr, 0.5);

    dsp::demod::SSB<float> ssb;
    ssb.init(nullptr, dsp::demod::SSB<float>::LSB, bw, sr, 0.1, 0.1);
    ssb.out.setBufferSize(64);

    std::vector<float> out(n);
    ssb.process(n, in.data(), out.data());

    std::vector<float> tail(out.begin() + n / 2, out.end());
    REQUIRE(goertzelMag(tail, 2500.0, sr) > 0.1);
}

TEST_CASE("demod::SSB DSB does not translate", "[dsp][demod][ssb]") {
    const double sr = 24000.0;
    const int n = 32768;

    auto in = complexTone(n, 1200.0, sr, 0.5);

    dsp::demod::SSB<float> ssb;
    ssb.init(nullptr, dsp::demod::SSB<float>::DSB, 3000.0, sr, 0.1, 0.1);
    ssb.out.setBufferSize(64);

    std::vector<float> out(n);
    ssb.process(n, in.data(), out.data());

    std::vector<float> tail(out.begin() + n / 2, out.end());
    REQUIRE(goertzelMag(tail, 1200.0, sr) > 0.1);
    REQUIRE(goertzelMag(tail, 2700.0, sr) < goertzelMag(tail, 1200.0, sr) * 0.2);
}

TEST_CASE("demod::SSB AGC brings a weak signal up to the set point", "[dsp][demod][ssb]") {
    const double sr = 24000.0;
    const int n = 65536;

    // 40 dB below full scale. The AGC set point is 1.0 with an unlimited max
    // gain, so the output should settle around unity regardless.
    auto in = complexTone(n, 500.0, sr, 0.01);

    dsp::demod::SSB<float> ssb;
    ssb.init(nullptr, dsp::demod::SSB<float>::DSB, 3000.0, sr, 0.01, 0.001);
    ssb.out.setBufferSize(64);

    std::vector<float> out(n);
    ssb.process(n, in.data(), out.data());

    std::vector<float> tail(out.begin() + (3 * n) / 4, out.end());
    REQUIRE(peak(tail) == Approx(1.0f).margin(0.35));
}

TEST_CASE("demod::SSB frozen AGC stops adapting", "[dsp][demod][ssb][characterization]") {
    // Freezing is what the "AGC hold" UI toggle does. See the AGC tests in
    // test_loop.cpp for the pinned behaviour of the underlying block.
    const double sr = 24000.0;
    const int n = 16384;

    auto loud = complexTone(n, 500.0, sr, 1.0);
    auto quiet = complexTone(n, 500.0, sr, 0.01);

    dsp::demod::SSB<float> ssb;
    ssb.init(nullptr, dsp::demod::SSB<float>::DSB, 3000.0, sr, 0.01, 0.001);
    ssb.out.setBufferSize(64);

    std::vector<float> out(n);
    ssb.process(n, loud.data(), out.data());   // settle on the loud signal
    ssb.setAGCFrozen(true);
    ssb.process(n, quiet.data(), out.data());  // must not ride the gain up

    std::vector<float> tail(out.begin() + n / 2, out.end());
    REQUIRE(peak(tail) < 0.2f);
}

TEST_CASE("demod::SSB stereo output duplicates the mono output", "[dsp][demod][ssb]") {
    const double sr = 24000.0;
    const int n = 8192;
    auto in = complexTone(n, 800.0, sr, 0.3);

    dsp::demod::SSB<float> mono;
    mono.init(nullptr, dsp::demod::SSB<float>::USB, 3000.0, sr, 0.01, 0.001);
    mono.out.setBufferSize(64);
    std::vector<float> monoOut(n);
    mono.process(n, in.data(), monoOut.data());

    dsp::demod::SSB<dsp::stereo_t> stereo;
    stereo.init(nullptr, dsp::demod::SSB<dsp::stereo_t>::USB, 3000.0, sr, 0.01, 0.001);
    stereo.out.setBufferSize(64);
    std::vector<dsp::stereo_t> stereoOut(n);
    stereo.process(n, in.data(), stereoOut.data());

    auto left = leftChannel(stereoOut);
    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(left[i] == Approx(monoOut[i]).margin(1e-4));
        REQUIRE(stereoOut[i].r == Approx(stereoOut[i].l).margin(1e-6));
    }
}

TEST_CASE("demod::SSB setMode retunes the translation", "[dsp][demod][ssb]") {
    const double sr = 24000.0;
    const int n = 32768;
    auto in = complexTone(n, 0.0, sr, 0.5); // DC carrier

    dsp::demod::SSB<float> ssb;
    ssb.init(nullptr, dsp::demod::SSB<float>::DSB, 3000.0, sr, 0.05, 0.05);
    ssb.out.setBufferSize(64);

    std::vector<float> out(n);
    ssb.process(n, in.data(), out.data());
    std::vector<float> dsbTail(out.begin() + n / 2, out.end());
    // DSB leaves the carrier at DC, which the AGC drives to the set point.
    REQUIRE(std::fabs(mean(dsbTail)) > 0.3);

    ssb.setMode(dsp::demod::SSB<float>::USB);
    ssb.process(n, in.data(), out.data());
    std::vector<float> usbTail(out.begin() + n / 2, out.end());
    // USB moves it to bw/2 = 1500 Hz, so the DC content collapses.
    REQUIRE(std::fabs(mean(usbTail)) < 0.15);
    REQUIRE(goertzelMag(usbTail, 1500.0, sr) > 0.2);
}

// -------------------------------------------------------------------- CW

TEST_CASE("demod::CW puts the carrier at the side tone", "[dsp][demod][cw]") {
    const double sr = 24000.0;
    const double tone = 800.0;
    const int n = 65536;

    // A carrier exactly on the VFO centre must come out at the side tone.
    auto in = complexTone(n, 0.0, sr, 0.2);

    dsp::demod::CW<float> cw;
    cw.init(nullptr, tone, 0.01, 0.001, sr);
    cw.out.setBufferSize(64);

    std::vector<float> out(n);
    REQUIRE(cw.process(n, in.data(), out.data()) == n);

    std::vector<float> tail(out.begin() + n / 2, out.end());
    double at800 = goertzelMag(tail, tone, sr);
    REQUIRE(at800 > 0.2);
    REQUIRE(std::fabs(mean(tail)) < at800 * 0.3);
}

TEST_CASE("demod::CW setTone moves the side tone", "[dsp][demod][cw]") {
    const double sr = 24000.0;
    const int n = 65536;
    auto in = complexTone(n, 0.0, sr, 0.2);

    dsp::demod::CW<float> cw;
    cw.init(nullptr, 600.0, 0.01, 0.001, sr);
    cw.out.setBufferSize(64);

    std::vector<float> out(n);
    cw.setTone(1200.0);
    cw.process(n, in.data(), out.data());

    std::vector<float> tail(out.begin() + n / 2, out.end());
    REQUIRE(goertzelMag(tail, 1200.0, sr) > 0.2);
    REQUIRE(goertzelMag(tail, 600.0, sr) < goertzelMag(tail, 1200.0, sr) * 0.2);
}

TEST_CASE("demod::CW stereo output duplicates the mono output", "[dsp][demod][cw]") {
    const double sr = 24000.0;
    const int n = 8192;
    auto in = complexTone(n, 100.0, sr, 0.2);

    dsp::demod::CW<float> mono;
    mono.init(nullptr, 700.0, 0.01, 0.001, sr);
    mono.out.setBufferSize(64);
    std::vector<float> monoOut(n);
    mono.process(n, in.data(), monoOut.data());

    dsp::demod::CW<dsp::stereo_t> stereo;
    stereo.init(nullptr, 700.0, 0.01, 0.001, sr);
    stereo.out.setBufferSize(64);
    std::vector<dsp::stereo_t> stereoOut(n);
    stereo.process(n, in.data(), stereoOut.data());

    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(stereoOut[i].l == Approx(monoOut[i]).margin(1e-4));
        REQUIRE(stereoOut[i].r == Approx(stereoOut[i].l).margin(1e-6));
    }
}

TEST_CASE("demod::CW frozen AGC holds the gain", "[dsp][demod][cw]") {
    const double sr = 24000.0;
    const int n = 16384;

    auto loud = complexTone(n, 0.0, sr, 1.0);
    auto quiet = complexTone(n, 0.0, sr, 0.01);

    dsp::demod::CW<float> cw;
    cw.init(nullptr, 800.0, 0.01, 0.001, sr);
    cw.out.setBufferSize(64);

    std::vector<float> out(n);
    cw.process(n, loud.data(), out.data());
    cw.setAGCFrozen(true);
    cw.process(n, quiet.data(), out.data());

    std::vector<float> tail(out.begin() + n / 2, out.end());
    REQUIRE(peak(tail) < 0.2f);
}
