// FIR, decimating FIR and de-emphasis.
//
// The FIR blocks carry state in a work buffer across process() calls; the tests
// below check both the arithmetic and that state handling, because a rewrite
// that only gets the convolution right would still break audio continuity.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/filter/decimating_fir.h>
#include <dsp/filter/deephasis.h>
#include <dsp/filter/fir.h>
#include <dsp/taps/from_array.h>
#include <dsp/taps/low_pass.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    template <class T>
    struct ScopedTaps {
        explicit ScopedTaps(dsp::tap<T> t) : taps(t) {}
        ~ScopedTaps() { dsp::taps::free(taps); }
        ScopedTaps(const ScopedTaps&) = delete;
        ScopedTaps& operator=(const ScopedTaps&) = delete;
        dsp::tap<T> taps;
    };

    // Reference convolution with the same "state starts zeroed" convention the
    // FIR block uses, so the two can be compared directly.
    std::vector<float> refConvolve(const std::vector<float>& in, const std::vector<float>& taps) {
        std::vector<float> out(in.size(), 0.0f);
        for (size_t i = 0; i < in.size(); i++) {
            float acc = 0.0f;
            for (size_t t = 0; t < taps.size(); t++) {
                long long idx = (long long)i - (long long)(taps.size() - 1 - t);
                if (idx >= 0) { acc += in[(size_t)idx] * taps[t]; }
            }
            out[i] = acc;
        }
        return out;
    }
}

TEST_CASE("FIR convolves against a reference implementation", "[dsp][filter][fir]") {
    const std::vector<float> tapVals = { 0.25f, 0.5f, 0.25f };
    ScopedTaps<float> taps(dsp::taps::fromArray<float>((int)tapVals.size(), tapVals.data()));

    dsp::stream<float> in;
    in.setBufferSize(1024);
    dsp::filter::FIR<float, float> fir;
    fir.init(&in, taps.taps);
    fir.out.setBufferSize(1024);

    auto input = noise(200, 7);
    std::vector<float> out(input.size());
    REQUIRE(fir.process((int)input.size(), input.data(), out.data()) == (int)input.size());

    auto expected = refConvolve(input, tapVals);
    for (size_t i = 0; i < input.size(); i++) {
        REQUIRE(out[i] == Approx(expected[i]).margin(1e-5));
    }
}

TEST_CASE("FIR preserves state across process calls", "[dsp][filter][fir]") {
    const std::vector<float> tapVals = { 0.25f, 0.5f, 0.25f };
    ScopedTaps<float> taps(dsp::taps::fromArray<float>((int)tapVals.size(), tapVals.data()));

    dsp::stream<float> in;
    in.setBufferSize(1024);
    dsp::filter::FIR<float, float> fir;
    fir.init(&in, taps.taps);
    fir.out.setBufferSize(1024);

    auto input = noise(120, 9);
    auto expected = refConvolve(input, tapVals);

    // Process in three uneven chunks; the result must be identical to one pass.
    std::vector<float> out;
    out.reserve(input.size());
    for (int off : { 0, 37, 90 }) {
        int n = (off == 0) ? 37 : (off == 37 ? 53 : 30);
        std::vector<float> chunk(n);
        fir.process(n, &input[off], chunk.data());
        out.insert(out.end(), chunk.begin(), chunk.end());
    }

    REQUIRE(out.size() == input.size());
    for (size_t i = 0; i < input.size(); i++) {
        REQUIRE(out[i] == Approx(expected[i]).margin(1e-5));
    }
}

TEST_CASE("FIR reset clears the history", "[dsp][filter][fir]") {
    const std::vector<float> tapVals = { 0.25f, 0.5f, 0.25f };
    ScopedTaps<float> taps(dsp::taps::fromArray<float>((int)tapVals.size(), tapVals.data()));

    dsp::stream<float> in;
    in.setBufferSize(1024);
    dsp::filter::FIR<float, float> fir;
    fir.init(&in, taps.taps);
    fir.out.setBufferSize(1024);

    auto input = constant(32, 1.0f);
    std::vector<float> first(32), second(32);
    fir.process(32, input.data(), first.data());

    fir.reset();
    fir.process(32, input.data(), second.data());

    for (int i = 0; i < 32; i++) { REQUIRE(second[i] == Approx(first[i]).margin(1e-6)); }
}

TEST_CASE("FIR rejects tap counts it cannot buffer", "[dsp][filter][fir]") {
    using FloatFIR = dsp::filter::FIR<float, float>;

    dsp::tap<float> empty;
    empty.taps = nullptr;
    empty.size = 0;
    REQUIRE_THROWS_AS(FloatFIR::validateTapCount(empty), std::invalid_argument);

    dsp::tap<float> huge;
    huge.taps = nullptr;
    huge.size = FloatFIR::WORK_BUFFER_SIZE + 1;
    REQUIRE_THROWS_AS(FloatFIR::validateTapCount(huge), std::invalid_argument);
}

TEST_CASE("FIR low-pass attenuates an out-of-band tone", "[dsp][filter][fir]") {
    const double sr = 48000.0;
    ScopedTaps<float> taps(dsp::taps::lowPass(3000.0, 1000.0, sr));

    dsp::stream<float> in;
    in.setBufferSize(8192);
    dsp::filter::FIR<float, float> fir;
    fir.init(&in, taps.taps);
    fir.out.setBufferSize(8192);

    const int N = 8192;
    auto passband = cosine(N, 1000.0, sr);
    auto stopband = cosine(N, 12000.0, sr);

    std::vector<float> outPass(N), outStop(N);
    fir.process(N, passband.data(), outPass.data());
    fir.reset();
    fir.process(N, stopband.data(), outStop.data());

    // Ignore the transient: measure over the tail only.
    std::vector<float> tailPass(outPass.begin() + 1024, outPass.end());
    std::vector<float> tailStop(outStop.begin() + 1024, outStop.end());

    REQUIRE(rms(tailPass) == Approx(rms(passband)).epsilon(0.05));
    REQUIRE(rms(tailStop) < rms(stopband) * 0.01);
}

TEST_CASE("FIR handles complex data with real taps", "[dsp][filter][fir]") {
    const double sr = 48000.0;
    ScopedTaps<float> taps(dsp::taps::lowPass(3000.0, 1500.0, sr));

    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(8192);
    dsp::filter::FIR<dsp::complex_t, float> fir;
    fir.init(&in, taps.taps);
    fir.out.setBufferSize(8192);

    const int N = 8192;
    auto pass = complexTone(N, 1000.0, sr);
    auto stop = complexTone(N, 15000.0, sr);
    std::vector<dsp::complex_t> outPass(N), outStop(N);

    fir.process(N, pass.data(), outPass.data());
    fir.reset();
    fir.process(N, stop.data(), outStop.data());

    std::vector<dsp::complex_t> tailPass(outPass.begin() + 1024, outPass.end());
    std::vector<dsp::complex_t> tailStop(outStop.begin() + 1024, outStop.end());

    REQUIRE(rms(tailPass) == Approx(1.0).epsilon(0.05));
    REQUIRE(rms(tailStop) < 0.01);
}

TEST_CASE("FIR setTaps keeps the filter running", "[dsp][filter][fir]") {
    const std::vector<float> shortTaps = { 1.0f };
    const std::vector<float> longTaps = { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f };

    ScopedTaps<float> a(dsp::taps::fromArray<float>(1, shortTaps.data()));
    ScopedTaps<float> b(dsp::taps::fromArray<float>(5, longTaps.data()));

    dsp::stream<float> in;
    in.setBufferSize(1024);
    dsp::filter::FIR<float, float> fir;
    fir.init(&in, a.taps);
    fir.out.setBufferSize(1024);

    auto input = constant(32, 1.0f);
    std::vector<float> out(32);
    fir.process(32, input.data(), out.data());
    REQUIRE(out[31] == Approx(1.0f));

    // Growing the tap count must not read uninitialised history.
    fir.setTaps(b.taps);
    fir.process(32, input.data(), out.data());
    REQUIRE(out[31] == Approx(1.0f));

    // Shrinking it back works too.
    fir.setTaps(a.taps);
    fir.process(32, input.data(), out.data());
    REQUIRE(out[31] == Approx(1.0f));
}

TEST_CASE("FIR getMaxInputCount leaves room for the tap history", "[dsp][filter][fir]") {
    const std::vector<float> tapVals(9, 0.1f);
    ScopedTaps<float> taps(dsp::taps::fromArray<float>(9, tapVals.data()));

    dsp::stream<float> in;
    in.setBufferSize(1024);
    dsp::filter::FIR<float, float> fir;
    fir.init(&in, taps.taps);
    fir.out.setBufferSize(1024);

    REQUIRE(fir.getMaxInputCount() == dsp::filter::FIR<float, float>::WORK_BUFFER_SIZE - 8);
}

TEST_CASE("DecimatingFIR produces one output per decimation samples", "[dsp][filter][decim]") {
    const std::vector<float> tapVals = { 0.5f, 0.5f };
    ScopedTaps<float> taps(dsp::taps::fromArray<float>(2, tapVals.data()));

    dsp::stream<float> in;
    in.setBufferSize(1024);
    dsp::filter::DecimatingFIR<float, float> fir;
    fir.init(&in, taps.taps, 4);
    fir.out.setBufferSize(1024);

    auto input = constant(64, 1.0f);
    std::vector<float> out(64);
    REQUIRE(fir.process(64, input.data(), out.data()) == 16);
}

TEST_CASE("DecimatingFIR keeps its phase across chunk boundaries", "[dsp][filter][decim]") {
    // Feeding N samples as one block or as several must yield the same total
    // output count and the same samples.
    const std::vector<float> tapVals = { 1.0f };
    ScopedTaps<float> tapsA(dsp::taps::fromArray<float>(1, tapVals.data()));
    ScopedTaps<float> tapsB(dsp::taps::fromArray<float>(1, tapVals.data()));

    dsp::stream<float> inA, inB;
    inA.setBufferSize(1024);
    inB.setBufferSize(1024);

    dsp::filter::DecimatingFIR<float, float> whole, split;
    whole.init(&inA, tapsA.taps, 3);
    whole.out.setBufferSize(1024);
    split.init(&inB, tapsB.taps, 3);
    split.out.setBufferSize(1024);

    auto input = ramp(90);

    std::vector<float> outWhole(90);
    int nWhole = whole.process(90, input.data(), outWhole.data());

    std::vector<float> outSplit;
    for (int off = 0; off < 90; off += 7) {
        int n = (std::min)(7, 90 - off);
        std::vector<float> chunk(n);
        int produced = split.process(n, &input[off], chunk.data());
        outSplit.insert(outSplit.end(), chunk.begin(), chunk.begin() + produced);
    }

    REQUIRE((int)outSplit.size() == nWhole);
    for (int i = 0; i < nWhole; i++) {
        REQUIRE(outSplit[i] == Approx(outWhole[i]).margin(1e-6));
    }
}

TEST_CASE("DecimatingFIR reset restarts the decimation phase", "[dsp][filter][decim]") {
    const std::vector<float> tapVals = { 1.0f };
    ScopedTaps<float> taps(dsp::taps::fromArray<float>(1, tapVals.data()));

    dsp::stream<float> in;
    in.setBufferSize(1024);
    dsp::filter::DecimatingFIR<float, float> fir;
    fir.init(&in, taps.taps, 4);
    fir.out.setBufferSize(1024);

    auto input = ramp(10);
    std::vector<float> first(10), second(10);
    int n1 = fir.process(10, input.data(), first.data());

    fir.reset();
    int n2 = fir.process(10, input.data(), second.data());

    REQUIRE(n1 == n2);
    for (int i = 0; i < n1; i++) { REQUIRE(first[i] == second[i]); }
}

TEST_CASE("DecimatingFIR getMaxInputCount respects the output capacity", "[dsp][filter][decim]") {
    const std::vector<float> tapVals(5, 0.2f);
    ScopedTaps<float> taps(dsp::taps::fromArray<float>(5, tapVals.data()));

    dsp::stream<float> in;
    in.setBufferSize(1024);
    dsp::filter::DecimatingFIR<float, float> fir;
    fir.init(&in, taps.taps, 8);
    fir.out.setBufferSize(1024);

    // 10 output samples at 8x decimation need at most 80 input samples.
    REQUIRE(fir.getMaxInputCount(10) == 80);
    REQUIRE(fir.getMaxInputCount(0) == 0);
    // Never more than the work buffer allows.
    REQUIRE(fir.getMaxInputCount(1000000000) <= dsp::filter::FIR<float, float>::WORK_BUFFER_SIZE);
}

TEST_CASE("DecimatingFIR low-pass filters before decimating", "[dsp][filter][decim]") {
    const double sr = 48000.0;
    ScopedTaps<float> taps(dsp::taps::lowPass(4000.0, 2000.0, sr));

    dsp::stream<float> in;
    in.setBufferSize(16384);
    dsp::filter::DecimatingFIR<float, float> fir;
    fir.init(&in, taps.taps, 4);
    fir.out.setBufferSize(16384);

    // A 20 kHz tone would alias to 4 kHz at the 12 kHz output rate. The
    // anti-alias filter must remove it instead.
    const int N = 16384;
    auto input = cosine(N, 20000.0, sr);
    std::vector<float> out(N);
    int produced = fir.process(N, input.data(), out.data());
    REQUIRE(produced == N / 4);

    std::vector<float> tail(out.begin() + 512, out.begin() + produced);
    REQUIRE(rms(tail) < 0.01);
}

TEST_CASE("Deemphasis is a one-pole low pass with unity DC gain", "[dsp][filter][deemph]") {
    const double sr = 48000.0;
    dsp::stream<float> in;
    in.setBufferSize(8192);

    dsp::filter::Deemphasis<float> deemph;
    deemph.init(&in, 50e-6, sr);
    deemph.out.setBufferSize(8192);

    SECTION("DC settles at the input level") {
        auto input = constant(4096, 1.0f);
        std::vector<float> out(4096);
        deemph.process(4096, input.data(), out.data());
        REQUIRE(out[4095] == Approx(1.0f).margin(1e-3));
        REQUIRE(out[0] < 1.0f); // it ramps up, it isn't a pass-through
    }

    SECTION("high frequencies are attenuated more than low ones") {
        deemph.reset();
        auto low = cosine(8192, 1000.0, sr);
        std::vector<float> outLow(8192);
        deemph.process(8192, low.data(), outLow.data());

        deemph.reset();
        auto high = cosine(8192, 10000.0, sr);
        std::vector<float> outHigh(8192);
        deemph.process(8192, high.data(), outHigh.data());

        std::vector<float> tailLow(outLow.begin() + 1024, outLow.end());
        std::vector<float> tailHigh(outHigh.begin() + 1024, outHigh.end());
        REQUIRE(rms(tailHigh) < rms(tailLow));
    }
}

TEST_CASE("Deemphasis setTau changes the corner frequency", "[dsp][filter][deemph]") {
    const double sr = 48000.0;
    dsp::stream<float> in;
    in.setBufferSize(8192);

    dsp::filter::Deemphasis<float> deemph;
    deemph.init(&in, 50e-6, sr);
    deemph.out.setBufferSize(8192);

    auto tone = cosine(8192, 5000.0, sr);
    std::vector<float> fast(8192), slow(8192);

    deemph.process(8192, tone.data(), fast.data());

    deemph.setTau(500e-6); // much lower corner -> stronger attenuation
    deemph.reset();
    deemph.process(8192, tone.data(), slow.data());

    std::vector<float> tailFast(fast.begin() + 2048, fast.end());
    std::vector<float> tailSlow(slow.begin() + 2048, slow.end());
    REQUIRE(rms(tailSlow) < rms(tailFast));
}

TEST_CASE("Deemphasis works on stereo samples channel-independently", "[dsp][filter][deemph]") {
    const double sr = 48000.0;
    dsp::stream<dsp::stereo_t> in;
    in.setBufferSize(4096);

    dsp::filter::Deemphasis<dsp::stereo_t> deemph;
    deemph.init(&in, 50e-6, sr);
    deemph.out.setBufferSize(4096);

    std::vector<dsp::stereo_t> input(2048);
    for (auto& s : input) { s = { 1.0f, -1.0f }; }
    std::vector<dsp::stereo_t> out(2048);
    deemph.process(2048, input.data(), out.data());

    REQUIRE(out[2047].l == Approx(1.0f).margin(1e-3));
    REQUIRE(out[2047].r == Approx(-1.0f).margin(1e-3));
}
