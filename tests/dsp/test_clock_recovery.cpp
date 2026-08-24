// Symbol clock recovery: Mueller & Muller (MM) and the falling-derivative (FD)
// recovery loop.
//
// These blocks decide how many samples come out for how many go in, which makes
// them the easiest place in the graph to introduce a silent buffer overrun. The
// tests therefore check both the recovered symbols and the output rate.
//
// Note: both blocks allocate a STREAM_BUFFER_SIZE work buffer per instance
// (4 MB for float, 8 MB for complex), so the tests keep the instance count low.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/clock_recovery/fd.h>
#include <dsp/clock_recovery/mm.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    // +-1 symbols, held for `sps` samples each. Deterministic.
    std::vector<float> bpskSymbols(int count, unsigned seed = 2024) {
        auto n = noise(count, seed);
        std::vector<float> syms(count);
        for (int i = 0; i < count; i++) { syms[i] = n[i] >= 0.0f ? 1.0f : -1.0f; }
        return syms;
    }

    std::vector<float> holdSymbols(const std::vector<float>& syms, int sps) {
        std::vector<float> out;
        out.reserve(syms.size() * sps);
        for (float s : syms) {
            for (int i = 0; i < sps; i++) { out.push_back(s); }
        }
        return out;
    }

    // Best fraction of matching signs over a small lag search. Clock recovery
    // has an arbitrary symbol offset, so a fixed alignment would be brittle.
    double bestSignMatch(const std::vector<float>& got, const std::vector<float>& want, int maxLag = 6) {
        double best = 0.0;
        for (int lag = 0; lag <= maxLag; lag++) {
            int n = (int)std::min(got.size() - lag, want.size());
            if (n <= 0) { continue; }
            int hits = 0;
            for (int i = 0; i < n; i++) {
                bool g = got[i + lag] >= 0.0f;
                bool w = want[i] >= 0.0f;
                if (g == w) { hits++; }
            }
            best = std::max(best, (double)hits / (double)n);
        }
        return best;
    }
}

// ------------------------------------------------------------------ MM float

TEST_CASE("MM recovers one sample per symbol from a real BPSK stream", "[dsp][clock][mm]") {
    const int sps = 8;
    const int symbols = 1024;

    auto syms = bpskSymbols(symbols);
    auto wave = holdSymbols(syms, sps);

    dsp::clock_recovery::MM<float> mm;
    mm.init(nullptr, sps, 1e-5, 0.01, 0.01);
    mm.out.setBufferSize(4096);

    std::vector<float> out(wave.size());
    int produced = mm.process((int)wave.size(), wave.data(), out.data());
    out.resize(produced);

    // One output per symbol, give or take the loop's own settling.
    REQUIRE(produced == Approx(symbols).epsilon(0.05));

    // Drop the first few symbols: the loop starts with an arbitrary phase.
    std::vector<float> tail(out.begin() + 32, out.end());
    std::vector<float> wantTail(syms.begin() + 32, syms.end());
    REQUIRE(bestSignMatch(tail, wantTail) > 0.9);
}

TEST_CASE("MM output rate follows omega", "[dsp][clock][mm]") {
    const int symbols = 512;

    for (int sps : { 2, 4, 16 }) {
        INFO("sps = " << sps);
        auto wave = holdSymbols(bpskSymbols(symbols), sps);

        dsp::clock_recovery::MM<float> mm;
        mm.init(nullptr, sps, 1e-5, 0.01, 0.01);
        mm.out.setBufferSize(4096);

        std::vector<float> out(wave.size());
        int produced = mm.process((int)wave.size(), wave.data(), out.data());
        REQUIRE(produced == Approx(symbols).epsilon(0.1));
    }
}

TEST_CASE("MM state carries across process calls", "[dsp][clock][mm]") {
    // Feeding the same waveform in one shot and in uneven chunks must give the
    // same symbols: the work buffer and the loop phase have to survive the call
    // boundary. This is the property a buffer-management rewrite is most likely
    // to break.
    const int sps = 8;
    const int symbols = 512;
    auto wave = holdSymbols(bpskSymbols(symbols, 909), sps);

    dsp::clock_recovery::MM<float> whole;
    whole.init(nullptr, sps, 1e-5, 0.01, 0.01);
    whole.out.setBufferSize(4096);
    std::vector<float> ref(wave.size());
    int refCount = whole.process((int)wave.size(), wave.data(), ref.data());
    ref.resize(refCount);

    dsp::clock_recovery::MM<float> chunked;
    chunked.init(nullptr, sps, 1e-5, 0.01, 0.01);
    chunked.out.setBufferSize(4096);
    std::vector<float> got;
    std::vector<float> scratch(wave.size());
    const int steps[] = { 333, 91, 1024, 7, 512 };
    size_t off = 0;
    int step = 0;
    while (off < wave.size()) {
        int n = (int)std::min<size_t>(steps[step++ % 5], wave.size() - off);
        int produced = chunked.process(n, &wave[off], scratch.data());
        got.insert(got.end(), scratch.begin(), scratch.begin() + produced);
        off += n;
    }

    REQUIRE(got.size() == ref.size());
    for (size_t i = 0; i < got.size(); i++) {
        INFO("symbol " << i);
        REQUIRE(got[i] == Approx(ref[i]).margin(1e-4));
    }
}

TEST_CASE("MM reset makes the block reproducible", "[dsp][clock][mm]") {
    const int sps = 4;
    auto wave = holdSymbols(bpskSymbols(256, 12), sps);

    dsp::clock_recovery::MM<float> mm;
    mm.init(nullptr, sps, 1e-5, 0.01, 0.01);
    mm.out.setBufferSize(4096);

    std::vector<float> a(wave.size()), b(wave.size());
    int ca = mm.process((int)wave.size(), wave.data(), a.data());
    mm.reset();
    int cb = mm.process((int)wave.size(), wave.data(), b.data());

    REQUIRE(ca == cb);
    for (int i = 0; i < ca; i++) {
        INFO("symbol " << i);
        REQUIRE(b[i] == Approx(a[i]).margin(1e-4));
    }
}

TEST_CASE("MM setOmega retunes the symbol rate", "[dsp][clock][mm]") {
    dsp::clock_recovery::MM<float> mm;
    mm.init(nullptr, 4.0, 1e-5, 0.01, 0.01);
    mm.out.setBufferSize(4096);

    auto wave = holdSymbols(bpskSymbols(512, 5), 8);
    std::vector<float> out(wave.size());

    // Configured for 4 sps but fed 8 sps: twice as many outputs as symbols.
    int at4 = mm.process((int)wave.size(), wave.data(), out.data());
    REQUIRE(at4 == Approx(1024).epsilon(0.15));

    mm.setOmega(8.0);
    int at8 = mm.process((int)wave.size(), wave.data(), out.data());
    REQUIRE(at8 == Approx(512).epsilon(0.15));
}

TEST_CASE("MM setInterpParams rebuilds the interpolator", "[dsp][clock][mm]") {
    const int sps = 8;
    auto wave = holdSymbols(bpskSymbols(256, 66), sps);

    dsp::clock_recovery::MM<float> mm;
    mm.init(nullptr, sps, 1e-5, 0.01, 0.01);
    mm.out.setBufferSize(4096);

    mm.setInterpParams(32, 4);
    std::vector<float> out(wave.size());
    int produced = mm.process((int)wave.size(), wave.data(), out.data());
    REQUIRE(produced == Approx(256).epsilon(0.1));
}

// ---------------------------------------------------------------- MM complex

TEST_CASE("MM recovers symbols from a complex BPSK stream", "[dsp][clock][mm]") {
    const int sps = 8;
    const int symbols = 1024;

    auto syms = bpskSymbols(symbols, 1717);
    std::vector<dsp::complex_t> wave(symbols * sps);
    for (int s = 0; s < symbols; s++) {
        for (int k = 0; k < sps; k++) { wave[s * sps + k] = { syms[s], 0.0f }; }
    }

    dsp::clock_recovery::MM<dsp::complex_t> mm;
    mm.init(nullptr, sps, 1e-5, 0.01, 0.01);
    mm.out.setBufferSize(4096);

    std::vector<dsp::complex_t> out(wave.size());
    int produced = mm.process((int)wave.size(), wave.data(), out.data());
    REQUIRE(produced == Approx(symbols).epsilon(0.05));

    std::vector<float> re;
    for (int i = 32; i < produced; i++) { re.push_back(out[i].re); }
    std::vector<float> want(syms.begin() + 32, syms.end());
    REQUIRE(bestSignMatch(re, want) > 0.9);
}

// -------------------------------------------------------------------- FD

TEST_CASE("FD recovers one sample per symbol", "[dsp][clock][fd]") {
    const int sps = 8;
    const int symbols = 1024;

    auto syms = bpskSymbols(symbols, 4321);
    auto wave = holdSymbols(syms, sps);

    dsp::clock_recovery::FD fd;
    fd.init(nullptr, sps, 1e-5, 0.01, 0.01);
    fd.out.setBufferSize(4096);

    std::vector<float> out(wave.size());
    int produced = fd.process((int)wave.size(), wave.data(), out.data());
    out.resize(produced);

    REQUIRE(produced == Approx(symbols).epsilon(0.05));

    std::vector<float> tail(out.begin() + 32, out.end());
    std::vector<float> wantTail(syms.begin() + 32, syms.end());
    REQUIRE(bestSignMatch(tail, wantTail) > 0.85);
}

TEST_CASE("FD keeps its loop frequency inside the relative limit", "[dsp][clock][fd]") {
    const int sps = 8;
    const double relLimit = 0.01;

    // pcl is public on FD (unlike MM), so the limit can be checked directly.
    dsp::clock_recovery::FD fd;
    fd.init(nullptr, sps, 1e-3, 0.1, relLimit);
    fd.out.setBufferSize(4096);

    // Feed something at a deliberately wrong rate so the loop pushes at its
    // limits for the whole run.
    auto wave = holdSymbols(bpskSymbols(512, 8), 3);
    std::vector<float> out(wave.size());
    fd.process((int)wave.size(), wave.data(), out.data());

    REQUIRE(fd.pcl.freq <= sps * (1.0 + relLimit) + 1e-4);
    REQUIRE(fd.pcl.freq >= sps * (1.0 - relLimit) - 1e-4);
}

TEST_CASE("FD state carries across process calls", "[dsp][clock][fd]") {
    const int sps = 8;
    auto wave = holdSymbols(bpskSymbols(512, 99), sps);

    dsp::clock_recovery::FD whole;
    whole.init(nullptr, sps, 1e-5, 0.01, 0.01);
    whole.out.setBufferSize(4096);
    std::vector<float> ref(wave.size());
    int refCount = whole.process((int)wave.size(), wave.data(), ref.data());
    ref.resize(refCount);

    dsp::clock_recovery::FD chunked;
    chunked.init(nullptr, sps, 1e-5, 0.01, 0.01);
    chunked.out.setBufferSize(4096);
    std::vector<float> got;
    std::vector<float> scratch(wave.size());
    size_t off = 0;
    const int steps[] = { 777, 13, 2048 };
    int step = 0;
    while (off < wave.size()) {
        int n = (int)std::min<size_t>(steps[step++ % 3], wave.size() - off);
        int produced = chunked.process(n, &wave[off], scratch.data());
        got.insert(got.end(), scratch.begin(), scratch.begin() + produced);
        off += n;
    }

    REQUIRE(got.size() == ref.size());
    for (size_t i = 0; i < got.size(); i++) {
        INFO("symbol " << i);
        REQUIRE(got[i] == Approx(ref[i]).margin(1e-4));
    }
}
