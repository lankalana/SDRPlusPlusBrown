// Sample rate conversion. The bounded-input accounting here is what keeps the
// resamplers from writing past their output stream, so it gets as much
// attention as the resampling maths.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/multirate/polyphase_resampler.h>
#include <dsp/multirate/power_decimator.h>
#include <dsp/multirate/rational_resampler.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

TEST_CASE("PowerDecimator accepts only power-of-two ratios", "[dsp][multirate][decim]") {
    REQUIRE(dsp::multirate::PowerDecimator<float>::getMaxRatio() >= 2u);
    // The maximum is derived from the number of decimation plans.
    unsigned int maxRatio = dsp::multirate::PowerDecimator<float>::getMaxRatio();
    REQUIRE((maxRatio & (maxRatio - 1)) == 0);
}

TEST_CASE("PowerDecimator with ratio 1 is a pass-through", "[dsp][multirate][decim]") {
    dsp::stream<float> in;
    in.setBufferSize(4096);

    dsp::multirate::PowerDecimator<float> decim;
    decim.init(&in, 1);
    decim.out.setBufferSize(4096);

    auto input = ramp(256);
    std::vector<float> out(256);
    REQUIRE(decim.process(256, input.data(), out.data()) == 256);
    for (int i = 0; i < 256; i++) { REQUIRE(out[i] == input[i]); }
}

TEST_CASE("PowerDecimator reduces the sample count by the ratio", "[dsp][multirate][decim]") {
    for (unsigned int ratio : { 2u, 4u, 8u, 16u }) {
        INFO("ratio: " << ratio);
        dsp::stream<float> in;
        in.setBufferSize(16384);

        dsp::multirate::PowerDecimator<float> decim;
        decim.init(&in, ratio);
        decim.out.setBufferSize(16384);

        const int N = 8192;
        auto input = constant(N, 0.0f);
        std::vector<float> out(N);
        int produced = decim.process(N, input.data(), out.data());

        // Allow for the filter history: the first block can be one short.
        REQUIRE(produced <= (int)(N / ratio));
        REQUIRE(produced >= (int)(N / ratio) - 1);
    }
}

TEST_CASE("PowerDecimator keeps an in-band tone and kills an out-of-band one", "[dsp][multirate][decim]") {
    const double sr = 48000.0;
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(65536);

    dsp::multirate::PowerDecimator<dsp::complex_t> decim;
    decim.init(&in, 4); // 48 kHz -> 12 kHz
    decim.out.setBufferSize(65536);

    const int N = 32768;

    auto runTone = [&](double freq) {
        decim.reset();
        auto input = complexTone(N, freq, sr);
        std::vector<dsp::complex_t> out(N);
        int produced = decim.process(N, input.data(), out.data());
        std::vector<dsp::complex_t> tail(out.begin() + produced / 4, out.begin() + produced);
        return rms(tail);
    };

    REQUIRE(runTone(2000.0) == Approx(1.0).epsilon(0.05)); // well inside 12 kHz
    REQUIRE(runTone(20000.0) < 0.01);                      // would alias otherwise
}

TEST_CASE("PowerDecimator setRatio reconfigures the filter chain", "[dsp][multirate][decim]") {
    dsp::stream<float> in;
    in.setBufferSize(16384);

    dsp::multirate::PowerDecimator<float> decim;
    decim.init(&in, 2);
    decim.out.setBufferSize(16384);

    std::vector<float> out(8192);
    auto input = constant(8192, 0.0f);

    int at2 = decim.process(8192, input.data(), out.data());
    decim.setRatio(8);
    int at8 = decim.process(8192, input.data(), out.data());

    REQUIRE(at2 > at8);
    REQUIRE(at8 <= 8192 / 8);
    REQUIRE(at8 >= 8192 / 8 - 1);
}

TEST_CASE("PowerDecimator getMaxInputCount respects the output capacity", "[dsp][multirate][decim]") {
    dsp::stream<float> in;
    in.setBufferSize(16384);

    dsp::multirate::PowerDecimator<float> decim;
    decim.init(&in, 1);
    decim.out.setBufferSize(1024);
    REQUIRE(decim.getMaxInputCount(1024) == 1024); // ratio 1: no limit beyond output

    decim.setRatio(4);
    REQUIRE(decim.getMaxInputCount(10) > 0);
    REQUIRE(decim.getMaxInputCount(10) <= 10 * 4 + 8);
}

TEST_CASE("PowerDecimator never overruns a small output stream", "[dsp][multirate][decim]") {
    dsp::stream<float> in;
    in.setBufferSize(65536);

    dsp::multirate::PowerDecimator<float> decim;
    decim.init(&in, 2);
    decim.out.setBufferSize(64); // deliberately tiny

    StreamCollector<float> collector(&decim.out);
    StreamFeeder<float> feeder(&in);

    decim.start();
    REQUIRE(feeder.feed(constant(16384, 1.0f)));
    REQUIRE(collector.waitFor(4096));

    for (int chunk : collector.chunks()) { REQUIRE(chunk <= 64); }

    decim.stop();
    collector.stop();
}

TEST_CASE("RationalResampler with equal rates is a pass-through", "[dsp][multirate][resamp]") {
    dsp::stream<float> in;
    in.setBufferSize(8192);

    dsp::multirate::RationalResampler<float> resamp;
    resamp.init(&in, 48000.0, 48000.0);
    resamp.out.setBufferSize(8192);

    REQUIRE(resamp.getInSampleRate() == 48000.0);
    REQUIRE(resamp.getOutSampleRate() == 48000.0);

    auto input = ramp(256);
    std::vector<float> out(256);
    REQUIRE(resamp.process(256, input.data(), out.data()) == 256);
    for (int i = 0; i < 256; i++) { REQUIRE(out[i] == input[i]); }
}

TEST_CASE("RationalResampler output count tracks the rate ratio", "[dsp][multirate][resamp]") {
    struct Case {
        double in, out;
    };
    const Case cases[] = {
        { 48000.0, 24000.0 },
        { 48000.0, 16000.0 },
        { 48000.0, 44100.0 },
        { 24000.0, 48000.0 },
        { 250000.0, 48000.0 },
    };

    for (const auto& c : cases) {
        INFO("rates: " << c.in << " -> " << c.out);
        dsp::stream<float> in;
        in.setBufferSize(1 << 18);

        dsp::multirate::RationalResampler<float> resamp;
        resamp.init(&in, c.in, c.out);
        resamp.out.setBufferSize(1 << 18);

        const int N = 1 << 15;
        auto input = constant(N, 0.0f);
        std::vector<float> outBuf(1 << 18);

        // Two passes so the transient of the first block is amortised.
        int produced = resamp.process(N, input.data(), outBuf.data());
        produced += resamp.process(N, input.data(), outBuf.data());

        double expected = 2.0 * (double)N * c.out / c.in;
        REQUIRE((double)produced == Approx(expected).epsilon(0.01));
    }
}

TEST_CASE("RationalResampler preserves a tone across rate change", "[dsp][multirate][resamp]") {
    const double inSr = 48000.0;
    const double outSr = 24000.0;
    const double tone = 1000.0;

    dsp::stream<float> in;
    in.setBufferSize(1 << 18);

    dsp::multirate::RationalResampler<float> resamp;
    resamp.init(&in, inSr, outSr);
    resamp.out.setBufferSize(1 << 18);

    const int N = 1 << 16;
    auto input = cosine(N, tone, inSr);
    std::vector<float> outBuf(1 << 18);
    int produced = resamp.process(N, input.data(), outBuf.data());
    REQUIRE(produced > 0);

    // Drop the filter transient, then check the tone is still at 1 kHz and
    // roughly at its original level.
    std::vector<float> tail(outBuf.begin() + produced / 4, outBuf.begin() + produced);
    REQUIRE(goertzelMag(tail, tone, outSr) > 0.4);
    REQUIRE(goertzelMag(tail, 3000.0, outSr) < 0.02);
    REQUIRE(rms(tail) == Approx(1.0 / std::sqrt(2.0)).epsilon(0.1));
}

TEST_CASE("RationalResampler setRates reconfigures on the fly", "[dsp][multirate][resamp]") {
    dsp::stream<float> in;
    in.setBufferSize(1 << 18);

    dsp::multirate::RationalResampler<float> resamp;
    resamp.init(&in, 48000.0, 48000.0);
    resamp.out.setBufferSize(1 << 18);

    const int N = 1 << 14;
    auto input = constant(N, 0.0f);
    std::vector<float> outBuf(1 << 18);

    REQUIRE(resamp.process(N, input.data(), outBuf.data()) == N);

    resamp.setRates(48000.0, 12000.0);
    REQUIRE(resamp.getInSampleRate() == 48000.0);
    REQUIRE(resamp.getOutSampleRate() == 12000.0);

    int produced = resamp.process(N, input.data(), outBuf.data());
    REQUIRE(produced == Approx(N / 4).epsilon(0.01));

    resamp.setOutSamplerate(48000.0);
    produced = resamp.process(N, input.data(), outBuf.data());
    REQUIRE(produced == Approx(N).epsilon(0.01));

    resamp.setInSamplerate(96000.0); // 96 kHz -> 48 kHz, i.e. half the samples
    produced = resamp.process(N, input.data(), outBuf.data());
    REQUIRE(produced == Approx(N / 2).epsilon(0.01));
}

TEST_CASE("RationalResampler never overruns a small output stream", "[dsp][multirate][resamp]") {
    dsp::stream<float> in;
    in.setBufferSize(1 << 16);

    dsp::multirate::RationalResampler<float> resamp;
    resamp.init(&in, 12000.0, 48000.0); // 4x upsampling into a tiny buffer
    resamp.out.setBufferSize(128);

    StreamCollector<float> collector(&resamp.out);
    StreamFeeder<float> feeder(&in);

    resamp.start();
    REQUIRE(feeder.feed(constant(8192, 0.0f)));
    REQUIRE(collector.waitFor(16384));

    for (int chunk : collector.chunks()) { REQUIRE(chunk <= 128); }

    resamp.stop();
    collector.stop();
}

TEST_CASE("PolyphaseResampler getMaxInputCount is consistent with process", "[dsp][multirate][resamp]") {
    // The interp/decim accounting must never let process() produce more than
    // the caller asked room for.
    dsp::tap<float> taps = dsp::taps::lowPass(0.4, 0.1, 1.0);

    dsp::stream<float> in;
    in.setBufferSize(1 << 16);

    dsp::multirate::PolyphaseResampler<float> resamp;
    resamp.init(&in, 12, 5, taps);
    resamp.out.setBufferSize(1 << 16);

    for (int maxOut : { 1, 7, 64, 1000 }) {
        int maxIn = resamp.getMaxInputCount(maxOut);
        INFO("maxOut: " << maxOut << " maxIn: " << maxIn);

        // Note: for an interpolating ratio the estimate can legitimately be 0,
        // meaning "one output sample needs less than one input sample". Callers
        // (runBounded) treat 0 as a configuration error, so it only shows up
        // with output buffers far smaller than anything the app uses.
        REQUIRE(maxIn >= 0);
        if (maxIn == 0) { continue; }

        auto input = constant(maxIn, 0.0f);
        std::vector<float> out(maxOut + 64);
        int produced = resamp.process(maxIn, input.data(), out.data());
        REQUIRE(produced <= maxOut);
    }

    dsp::taps::free(taps);
}

TEST_CASE("PolyphaseResampler rejects an invalid configuration", "[dsp][multirate][resamp]") {
    dsp::tap<float> taps = dsp::taps::lowPass(0.4, 0.1, 1.0);
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::multirate::PolyphaseResampler<float> resamp;
    REQUIRE_THROWS_AS(resamp.init(&in, 0, 1, taps), std::invalid_argument);
    REQUIRE_THROWS_AS(resamp.init(&in, 1, 0, taps), std::invalid_argument);

    dsp::tap<float> empty;
    empty.taps = nullptr;
    empty.size = 0;
    REQUIRE_THROWS_AS(resamp.init(&in, 1, 1, empty), std::invalid_argument);

    dsp::taps::free(taps);
}

TEST_CASE("PolyphaseResampler interpolates by the interp/decim ratio", "[dsp][multirate][resamp]") {
    dsp::tap<float> taps = dsp::taps::lowPass(0.2, 0.05, 1.0);

    dsp::stream<float> in;
    in.setBufferSize(1 << 16);

    dsp::multirate::PolyphaseResampler<float> resamp;
    resamp.init(&in, 3, 2, taps); // 1.5x
    resamp.out.setBufferSize(1 << 16);

    const int N = 6000;
    auto input = constant(N, 0.0f);
    std::vector<float> out(1 << 16);
    int produced = resamp.process(N, input.data(), out.data());

    REQUIRE(produced == Approx(N * 3 / 2).margin(2));

    resamp.reset();
    int again = resamp.process(N, input.data(), out.data());
    REQUIRE(again == produced);

    dsp::taps::free(taps);
}
