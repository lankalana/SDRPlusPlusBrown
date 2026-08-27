// Throughput benchmarks for the hot DSP kernels.
//
// These are hidden by default: every test case here is tagged "[.]", so a plain
// `sdrpp_core_tests` run and `ctest` both skip them. Run them explicitly with
//
//     sdrpp_core_tests "[benchmark]"
//
// and keep the output from before and after a refactor side by side. They are
// not assertions — nothing here fails on a slow machine — they exist so that a
// performance change is visible rather than discovered in the field.
//
// Every benchmark processes a fixed 65536 sample block so the numbers are
// directly comparable between blocks and between runs.

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <dsp/channel/frequency_xlator.h>
#include <dsp/channel/rx_vfo.h>
#include <dsp/convert/complex_to_real.h>
#include <dsp/correction/dc_blocker.h>
#include <dsp/demod/quadrature.h>
#include <dsp/filter/decimating_fir.h>
#include <dsp/filter/fir.h>
#include <dsp/loop/agc.h>
#include <dsp/math/multiply.h>
#include <dsp/multirate/power_decimator.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/taps/low_pass.h>

#include "support/dsp_test_helpers.h"

using namespace sdrpp_test;

namespace {
    constexpr int BLOCK = 65536;
    constexpr double SR = 2400000.0;

    struct ScopedTaps {
        explicit ScopedTaps(dsp::tap<float> t) : taps(t) {}
        ~ScopedTaps() { dsp::taps::free(taps); }
        ScopedTaps(const ScopedTaps&) = delete;
        ScopedTaps& operator=(const ScopedTaps&) = delete;
        dsp::tap<float> taps;
    };
}

TEST_CASE("benchmark: FrequencyXlator", "[.][benchmark][dsp]") {
    dsp::channel::FrequencyXlator x;
    x.init(nullptr, 100000.0, SR);
    x.out.setBufferSize(64);

    auto in = complexTone(BLOCK, 12345.0, SR);
    std::vector<dsp::complex_t> out(BLOCK);

    BENCHMARK("65536 complex samples") {
        return x.process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: FIR complex data, real taps", "[.][benchmark][dsp]") {
    ScopedTaps taps(dsp::taps::lowPass(100000.0, 20000.0, SR));

    dsp::filter::FIR<dsp::complex_t, float> fir;
    fir.init(nullptr, taps.taps);
    fir.out.setBufferSize(64);

    auto in = complexTone(BLOCK, 12345.0, SR);
    std::vector<dsp::complex_t> out(BLOCK);

    INFO("tap count: " << taps.taps.size);
    BENCHMARK("65536 complex samples") {
        return fir.process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: FIR real data, real taps", "[.][benchmark][dsp]") {
    ScopedTaps taps(dsp::taps::lowPass(5000.0, 1000.0, 48000.0));

    dsp::filter::FIR<float, float> fir;
    fir.init(nullptr, taps.taps);
    fir.out.setBufferSize(64);

    auto in = cosine(BLOCK, 1000.0, 48000.0);
    std::vector<float> out(BLOCK);

    INFO("tap count: " << taps.taps.size);
    BENCHMARK("65536 real samples") {
        return fir.process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: DecimatingFIR by 8", "[.][benchmark][dsp]") {
    ScopedTaps taps(dsp::taps::lowPass(100000.0, 40000.0, SR));

    dsp::filter::DecimatingFIR<dsp::complex_t, float> fir;
    fir.init(nullptr, taps.taps, 8);
    fir.out.setBufferSize(BLOCK);

    auto in = complexTone(BLOCK, 12345.0, SR);
    std::vector<dsp::complex_t> out(BLOCK);

    BENCHMARK("65536 complex samples in") {
        return fir.process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: PowerDecimator by 8", "[.][benchmark][dsp]") {
    dsp::multirate::PowerDecimator<dsp::complex_t> dec;
    dec.init(nullptr, 8);
    dec.out.setBufferSize(BLOCK);

    auto in = complexTone(BLOCK, 12345.0, SR);
    std::vector<dsp::complex_t> out(BLOCK);

    BENCHMARK("65536 complex samples in") {
        return dec.process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: RationalResampler 2.4M to 48k", "[.][benchmark][dsp]") {
    dsp::multirate::RationalResampler<dsp::complex_t> res;
    res.init(nullptr, SR, 48000.0);
    res.out.setBufferSize(BLOCK);

    auto in = complexTone(BLOCK, 12345.0, SR);
    std::vector<dsp::complex_t> out(BLOCK);

    BENCHMARK("65536 complex samples in") {
        return res.process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: RxVFO 2.4M to 48k", "[.][benchmark][dsp]") {
    // The full per-VFO cost: translate, resample, filter.
    dsp::channel::RxVFO vfo;
    vfo.init(nullptr, SR, 48000.0, 24000.0, 100000.0);
    vfo.out.setBufferSize(BLOCK);

    auto in = complexTone(BLOCK, 112345.0, SR);
    std::vector<dsp::complex_t> out(BLOCK);

    BENCHMARK("65536 complex samples in") {
        return vfo.process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: DCBlocker", "[.][benchmark][dsp]") {
    dsp::correction::DCBlocker<dsp::complex_t> dc;
    dc.init(nullptr, 50.0 / SR);
    dc.out.setBufferSize(64);

    auto in = complexTone(BLOCK, 12345.0, SR);
    std::vector<dsp::complex_t> out(BLOCK);

    BENCHMARK("65536 complex samples") {
        return dc.process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: quadrature FM demodulator", "[.][benchmark][dsp]") {
    dsp::demod::Quadrature demod;
    demod.init(nullptr, 75000.0, 240000.0);
    demod.out.setBufferSize(64);

    auto in = complexTone(BLOCK, 12345.0, 240000.0);
    std::vector<float> out(BLOCK);

    BENCHMARK("65536 complex samples") {
        return demod.process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: AGC", "[.][benchmark][dsp]") {
    dsp::loop::AGC<float> agc;
    agc.init(nullptr, 1.0, 0.01, 0.001, 1e6, 10.0);
    agc.out.setBufferSize(64);

    auto in = cosine(BLOCK, 1000.0, 48000.0, 0.1);
    std::vector<float> out(BLOCK);

    BENCHMARK("65536 real samples") {
        return agc.process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: complex multiply", "[.][benchmark][dsp]") {
    auto a = complexTone(BLOCK, 1000.0, SR);
    auto b = complexTone(BLOCK, 2000.0, SR);
    std::vector<dsp::complex_t> out(BLOCK);

    BENCHMARK("65536 complex samples") {
        return dsp::math::Multiply<dsp::complex_t>::process(BLOCK, a.data(), b.data(), out.data());
    };
}

TEST_CASE("benchmark: complex to real", "[.][benchmark][dsp]") {
    auto in = complexTone(BLOCK, 1000.0, SR);
    std::vector<float> out(BLOCK);

    BENCHMARK("65536 complex samples") {
        return dsp::convert::ComplexToReal::process(BLOCK, in.data(), out.data());
    };
}

TEST_CASE("benchmark: stream swap and read round trip", "[.][benchmark][stream]") {
    // The cost of moving a buffer between two blocks, excluding any processing.
    // Everything in the graph pays this per swap, so it sets the floor on how
    // small a block size is worth using.
    dsp::stream<dsp::complex_t> str;
    str.setBufferSize(BLOCK);

    auto data = complexTone(1024, 1000.0, SR);

    BENCHMARK("1024 sample swap plus read plus flush") {
        memcpy(str.writeBuf, data.data(), 1024 * sizeof(dsp::complex_t));
        // Single threaded: swap then immediately consume, so nothing blocks.
        str.swap(1024);
        int n = str.read();
        str.flush();
        return n;
    };
}
