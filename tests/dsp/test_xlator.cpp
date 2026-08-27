// dsp::channel::FrequencyXlator and dsp::channel::RxVFO.
//
// The xlator is the single most used block in the whole signal path: every VFO
// starts with one. Its contract is narrow (multiply by a rotating phasor,
// preserving amplitude and phase continuity across calls) which makes it a good
// property-test target: any rewrite, SIMD or not, must keep these true.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <dsp/channel/frequency_xlator.h>
#include <dsp/channel/rx_vfo.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    // The xlator's out stream is never used by the cold tests, but it is
    // allocated by the constructor. Shrink it so the tests stay cheap.
    void shrinkOut(dsp::channel::FrequencyXlator& x) { x.out.setBufferSize(4096); }
}

TEST_CASE("FrequencyXlator shifts a tone by the requested offset", "[dsp][channel][xlator]") {
    const double sr = 48000.0;
    const double toneFreq = 5000.0;
    const double offset = -3000.0;
    const int n = 4096;

    dsp::channel::FrequencyXlator x;
    x.init(nullptr, offset, sr);
    shrinkOut(x);

    auto in = complexTone(n, toneFreq, sr);
    std::vector<dsp::complex_t> out(n);
    REQUIRE(x.process(n, in.data(), out.data()) == n);

    // The tone must have moved to toneFreq + offset and left nothing behind.
    REQUIRE(goertzelMag(out, toneFreq + offset, sr) == Approx(1.0).margin(0.01));
    REQUIRE(goertzelMag(out, toneFreq, sr) < 0.02);
}

TEST_CASE("FrequencyXlator preserves amplitude", "[dsp][channel][xlator]") {
    const double sr = 48000.0;
    const int n = 2048;

    dsp::channel::FrequencyXlator x;
    x.init(nullptr, 1234.0, sr);
    shrinkOut(x);

    auto in = complexTone(n, 100.0, sr, 0.25);
    std::vector<dsp::complex_t> out(n);
    x.process(n, in.data(), out.data());

    // Pure rotation: |out[i]| == |in[i]| for every sample, not just on average.
    for (int i = 0; i < n; i++) {
        REQUIRE(out[i].amplitude() == Approx(0.25).margin(1e-3));
    }
}

TEST_CASE("FrequencyXlator phase is continuous across process calls", "[dsp][channel][xlator]") {
    // Chunking must not be observable. This is the property most likely to
    // break in a rewrite that forgets to carry the phase accumulator.
    const double sr = 48000.0;
    const double offset = 700.0;
    const int n = 1024;

    auto in = complexTone(n, 2000.0, sr);

    dsp::channel::FrequencyXlator whole;
    whole.init(nullptr, offset, sr);
    shrinkOut(whole);
    std::vector<dsp::complex_t> ref(n);
    whole.process(n, in.data(), ref.data());

    dsp::channel::FrequencyXlator chunked;
    chunked.init(nullptr, offset, sr);
    shrinkOut(chunked);
    std::vector<dsp::complex_t> got(n);
    const int steps[] = { 100, 37, 511, 1, 375 };
    int off = 0;
    for (int s : steps) {
        chunked.process(s, &in[off], &got[off]);
        off += s;
    }
    REQUIRE(off == n);

    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(got[i].re == Approx(ref[i].re).margin(1e-4));
        REQUIRE(got[i].im == Approx(ref[i].im).margin(1e-4));
    }
}

TEST_CASE("FrequencyXlator with zero offset is a pass-through", "[dsp][channel][xlator]") {
    dsp::channel::FrequencyXlator x;
    x.init(nullptr, 0.0, 48000.0);
    shrinkOut(x);

    auto in = complexTone(512, 1000.0, 48000.0);
    std::vector<dsp::complex_t> out(512);
    x.process(512, in.data(), out.data());

    for (int i = 0; i < 512; i++) {
        REQUIRE(out[i].re == Approx(in[i].re).margin(1e-4));
        REQUIRE(out[i].im == Approx(in[i].im).margin(1e-4));
    }
}

TEST_CASE("FrequencyXlator setOffset takes effect on the next call", "[dsp][channel][xlator]") {
    const double sr = 48000.0;
    dsp::channel::FrequencyXlator x;
    x.init(nullptr, 0.0, sr);
    shrinkOut(x);

    auto in = complexTone(2048, 1000.0, sr);
    std::vector<dsp::complex_t> out(2048);

    x.setOffset(2000.0, sr);
    x.process(2048, in.data(), out.data());
    REQUIRE(goertzelMag(out, 3000.0, sr) == Approx(1.0).margin(0.02));
}

TEST_CASE("FrequencyXlator reset restarts the phasor at 1+0j", "[dsp][channel][xlator]") {
    const double sr = 48000.0;
    dsp::channel::FrequencyXlator x;
    x.init(nullptr, 1000.0, sr);
    shrinkOut(x);

    auto dc = std::vector<dsp::complex_t>(64, { 1.0f, 0.0f });
    std::vector<dsp::complex_t> first(64), second(64);

    x.process(64, dc.data(), first.data());
    x.reset();
    x.process(64, dc.data(), second.data());

    for (int i = 0; i < 64; i++) {
        REQUIRE(second[i].re == Approx(first[i].re).margin(1e-4));
        REQUIRE(second[i].im == Approx(first[i].im).margin(1e-4));
    }
}

TEST_CASE("FrequencyXlator handles a zero-length call", "[dsp][channel][xlator]") {
    dsp::channel::FrequencyXlator x;
    x.init(nullptr, 1000.0, 48000.0);
    shrinkOut(x);

    dsp::complex_t dummy{ 0.0f, 0.0f };
    REQUIRE(x.process(0, &dummy, &dummy) == 0);
}

// --------------------------------------------------------------------- RxVFO

TEST_CASE("RxVFO selects the requested offset and rejects the rest", "[dsp][channel][vfo]") {
    const double inSr = 48000.0;
    const double outSr = 12000.0;
    const double bw = 6000.0;
    const double offset = 8000.0;
    const int n = 16384;

    dsp::channel::RxVFO vfo;
    vfo.init(nullptr, inSr, outSr, bw, offset);
    vfo.out.setBufferSize(n);

    // Wanted tone 1 kHz above the VFO centre, unwanted tone far outside.
    auto wanted = complexTone(n, offset + 1000.0, inSr, 1.0);
    auto unwanted = complexTone(n, offset - 15000.0, inSr, 1.0);
    std::vector<dsp::complex_t> in(n);
    for (int i = 0; i < n; i++) { in[i] = wanted[i] + unwanted[i]; }

    std::vector<dsp::complex_t> out(n);
    int produced = vfo.process(n, in.data(), out.data());
    REQUIRE(produced > 0);
    out.resize(produced);

    // Decimation by 4, allowing for the resampler's own transient.
    REQUIRE(produced == Approx(n / 4).epsilon(0.05));

    // Drop the filter transient before measuring.
    std::vector<dsp::complex_t> settled(out.begin() + produced / 4, out.end());
    double kept = goertzelMag(settled, 1000.0, outSr);
    double rejected = goertzelMag(settled, -7000.0, outSr); // where the alias would land
    REQUIRE(kept > 0.5);
    REQUIRE(rejected < kept * 0.05);
}

TEST_CASE("RxVFO with bandwidth equal to samplerate skips the filter", "[dsp][channel][vfo]") {
    // filterNeeded is false in this configuration; the block must still work and
    // must not attenuate the passband.
    const double sr = 48000.0;
    const int n = 8192;

    dsp::channel::RxVFO vfo;
    vfo.init(nullptr, sr, sr, sr, 0.0);
    vfo.out.setBufferSize(n);

    auto in = complexTone(n, 1000.0, sr);
    std::vector<dsp::complex_t> out(n);
    int produced = vfo.process(n, in.data(), out.data());
    REQUIRE(produced == n);

    std::vector<dsp::complex_t> settled(out.begin() + 64, out.end());
    REQUIRE(goertzelMag(settled, 1000.0, sr) == Approx(1.0).margin(0.05));
}

TEST_CASE("RxVFO setOffset retunes without restarting", "[dsp][channel][vfo]") {
    const double inSr = 48000.0;
    const double outSr = 12000.0;
    const int n = 16384;

    dsp::channel::RxVFO vfo;
    vfo.init(nullptr, inSr, outSr, 6000.0, 0.0);
    vfo.out.setBufferSize(n);

    auto in = complexTone(n, 5000.0, inSr);
    std::vector<dsp::complex_t> out(n);

    // Off-centre and outside the 6 kHz passband: mostly rejected.
    int produced = vfo.process(n, in.data(), out.data());
    std::vector<dsp::complex_t> before(out.begin() + produced / 4, out.begin() + produced);
    double magBefore = goertzelMag(before, 5000.0 - outSr, outSr);

    vfo.setOffset(5000.0);
    vfo.reset();
    produced = vfo.process(n, in.data(), out.data());
    std::vector<dsp::complex_t> after(out.begin() + produced / 4, out.begin() + produced);
    double magAfter = goertzelMag(after, 0.0, outSr);

    REQUIRE(magAfter > 0.5);
    REQUIRE(magAfter > magBefore * 4.0);
}

TEST_CASE("RxVFO rejects a bandwidth that needs too many taps", "[dsp][channel][vfo]") {
    // generateTaps() runs the tap count through FIR::validateTapCount and must
    // propagate the failure instead of installing a filter that would overrun
    // the work buffer.
    dsp::channel::RxVFO vfo;
    vfo.init(nullptr, 48000.0, 48000.0, 48000.0, 0.0);
    vfo.out.setBufferSize(1024);

    // A 1 Hz filter at 48 kHz needs 3.8 * 48000 / 0.05 taps, a few million,
    // which is past the FIR's work buffer. The exact threshold is an
    // implementation detail; what matters is that the block throws rather than
    // silently installing a filter that overruns the buffer.
    REQUIRE_THROWS(vfo.setOutSamplerate(48000.0, 1.0));
}

TEST_CASE("RxVFO reset clears filter and resampler state", "[dsp][channel][vfo]") {
    const double inSr = 48000.0;
    const double outSr = 24000.0;
    const int n = 4096;

    dsp::channel::RxVFO vfo;
    vfo.init(nullptr, inSr, outSr, 12000.0, 0.0);
    vfo.out.setBufferSize(n);

    auto in = complexTone(n, 1000.0, inSr);
    std::vector<dsp::complex_t> a(n), b(n);

    int ca = vfo.process(n, in.data(), a.data());
    vfo.reset();
    int cb = vfo.process(n, in.data(), b.data());

    REQUIRE(ca == cb);
    for (int i = 0; i < ca; i++) {
        INFO("sample " << i);
        REQUIRE(b[i].re == Approx(a[i].re).margin(1e-4));
        REQUIRE(b[i].im == Approx(a[i].im).margin(1e-4));
    }
}
