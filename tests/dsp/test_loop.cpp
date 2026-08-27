// Control loops: AGC and FastAGC. These are stateful and their behaviour is
// only meaningful over time, so the tests look at settled levels rather than
// individual samples.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/loop/agc.h>
#include <dsp/loop/fast_agc.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    // The AGC ramps its output in over the first _totalEnvelopeLength samples
    // to avoid a click. Anything measured before that point is meaningless.
    constexpr int ENVELOPE_SAMPLES = 4800;
}

TEST_CASE("AGC drives a steady signal to the set point", "[dsp][loop][agc]") {
    dsp::stream<float> in;
    in.setBufferSize(65536);

    dsp::loop::AGC<float> agc;
    agc.init(&in, 1.0, 0.01, 0.001, 1e6, 10.0, 1.0);
    agc.out.setBufferSize(65536);

    const int N = 65536;
    auto input = cosine(N, 1000.0, 48000.0, 0.01); // 40 dB below the set point
    std::vector<float> out(N);
    agc.process(N, input.data(), out.data());

    std::vector<float> tail(out.begin() + 2 * ENVELOPE_SAMPLES, out.end());
    // A sine driven to a set point of 1.0 on the envelope has RMS ~1/sqrt(2).
    REQUIRE(rms(tail) == Approx(1.0 / std::sqrt(2.0)).epsilon(0.25));
}

TEST_CASE("AGC pulls a loud signal down as well as a quiet one up", "[dsp][loop][agc]") {
    const int N = 65536;

    auto settledRms = [&](double amplitude) {
        dsp::stream<float> in;
        in.setBufferSize(N);
        dsp::loop::AGC<float> agc;
        agc.init(&in, 1.0, 0.01, 0.001, 1e6, 10.0, 1.0);
        agc.out.setBufferSize(N);

        auto input = cosine(N, 1000.0, 48000.0, amplitude);
        std::vector<float> out(N);
        agc.process(N, input.data(), out.data());
        return rms(std::vector<float>(out.begin() + 2 * ENVELOPE_SAMPLES, out.end()));
    };

    double quiet = settledRms(0.001);
    double loud = settledRms(100.0);

    REQUIRE(quiet == Approx(loud).epsilon(0.3));
}

TEST_CASE("AGC respects maxGain", "[dsp][loop][agc]") {
    const int N = 32768;
    dsp::stream<float> in;
    in.setBufferSize(N);

    dsp::loop::AGC<float> agc;
    agc.init(&in, 1.0, 0.01, 0.001, /*maxGain*/ 2.0, 10.0, 1.0);
    agc.out.setBufferSize(N);

    auto input = cosine(N, 1000.0, 48000.0, 0.001);
    std::vector<float> out(N);
    agc.process(N, input.data(), out.data());

    // Gain is capped at 2, so a 0.001 amplitude tone cannot exceed 0.002.
    REQUIRE(peak(out) <= 0.002f + 1e-6f);
}

TEST_CASE("AGC with attack <= 0 is a pass-through", "[dsp][loop][agc]") {
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::loop::AGC<float> agc;
    agc.init(&in, 1.0, 0.0, 0.001, 1e6, 10.0, 1.0);
    agc.out.setBufferSize(1024);

    auto input = ramp(64, 1.0f);
    std::vector<float> out(64);
    agc.process(64, input.data(), out.data());

    for (int i = 0; i < 64; i++) { REQUIRE(out[i] == input[i]); }
}

TEST_CASE("AGC freeze makes the block a pass-through", "[dsp][loop][agc]") {
    // Characterization, not a specification: setFrozen(true) skips the gain
    // update, and because `gain` is a per-sample local initialised to 1.0, the
    // frozen block applies unity gain rather than holding the gain it had
    // converged to. Pinned here so a refactor makes that change deliberately.
    const int N = 16384;
    dsp::stream<float> in;
    in.setBufferSize(N);

    dsp::loop::AGC<float> agc;
    agc.init(&in, 1.0, 0.01, 0.01, 1e6, 10.0, 1.0);
    agc.out.setBufferSize(N);

    // Let it converge first, and get past the start envelope.
    auto quiet = cosine(N, 1000.0, 48000.0, 0.01);
    std::vector<float> out(N);
    agc.process(N, quiet.data(), out.data());
    REQUIRE(rms(std::vector<float>(out.end() - 4096, out.end())) > 0.3);

    agc.setFrozen(true);
    agc.process(N, quiet.data(), out.data());

    for (int i = 0; i < N; i++) { REQUIRE(out[i] == Approx(quiet[i]).margin(1e-6)); }
}

TEST_CASE("AGC reset restores the initial gain", "[dsp][loop][agc]") {
    const int N = 16384;
    dsp::stream<float> in;
    in.setBufferSize(N);

    dsp::loop::AGC<float> agc;
    agc.init(&in, 1.0, 0.01, 0.001, 1e6, 10.0, 1.0);
    agc.out.setBufferSize(N);

    auto input = cosine(N, 1000.0, 48000.0, 0.01);
    std::vector<float> first(N), second(N);
    agc.process(N, input.data(), first.data());

    agc.reset();
    agc.process(N, input.data(), second.data());

    for (int i = 0; i < N; i++) { REQUIRE(second[i] == Approx(first[i]).margin(1e-6)); }
}

TEST_CASE("AGC leaves a silent input silent", "[dsp][loop][agc]") {
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::loop::AGC<float> agc;
    agc.init(&in, 1.0, 0.01, 0.001, 1e6, 10.0, 1.0);
    agc.out.setBufferSize(1024);

    auto input = constant(512, 0.0f);
    std::vector<float> out(512);
    agc.process(512, input.data(), out.data());

    for (float s : out) { REQUIRE(s == 0.0f); }
}

TEST_CASE("AGC works on complex samples", "[dsp][loop][agc]") {
    const int N = 32768;
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(N);

    dsp::loop::AGC<dsp::complex_t> agc;
    agc.init(&in, 1.0, 0.01, 0.001, 1e6, 10.0, 1.0);
    agc.out.setBufferSize(N);

    auto input = complexTone(N, 1000.0, 48000.0, 0.01);
    std::vector<dsp::complex_t> out(N);
    agc.process(N, input.data(), out.data());

    std::vector<dsp::complex_t> tail(out.begin() + 2 * ENVELOPE_SAMPLES, out.end());
    // A complex exponential has constant envelope, so RMS equals the set point.
    REQUIRE(rms(tail) == Approx(1.0).epsilon(0.25));
}

TEST_CASE("FastAGC converges towards the set point", "[dsp][loop][agc]") {
    const int N = 65536;
    dsp::stream<float> in;
    in.setBufferSize(N);

    dsp::loop::FastAGC<float> agc;
    agc.init(&in, 1.0, 1e6, 0.001, 1.0);
    agc.out.setBufferSize(N);

    auto input = cosine(N, 1000.0, 48000.0, 0.05);
    std::vector<float> out(N);
    agc.process(N, input.data(), out.data());

    std::vector<float> tail(out.end() - 8192, out.end());
    REQUIRE(peak(tail) > 0.5f);
    REQUIRE(peak(tail) < 4.0f);
}

TEST_CASE("FastAGC clamps at maxGain", "[dsp][loop][agc]") {
    const int N = 8192;
    dsp::stream<float> in;
    in.setBufferSize(N);

    dsp::loop::FastAGC<float> agc;
    agc.init(&in, 1.0, /*maxGain*/ 3.0, 0.5, 1.0);
    agc.out.setBufferSize(N);

    auto input = constant(N, 1e-6f);
    std::vector<float> out(N);
    agc.process(N, input.data(), out.data());

    REQUIRE(peak(out) <= 3e-6f + 1e-9f);
}

TEST_CASE("FastAGC setGain and reset control the loop state", "[dsp][loop][agc]") {
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::loop::FastAGC<float> agc;
    agc.init(&in, 1.0, 100.0, 0.0, 2.0); // rate 0: gain never adapts
    agc.out.setBufferSize(1024);

    auto input = constant(4, 1.0f);
    std::vector<float> out(4);

    agc.process(4, input.data(), out.data());
    REQUIRE(out[0] == Approx(2.0f));

    agc.setGain(5.0);
    agc.process(4, input.data(), out.data());
    REQUIRE(out[0] == Approx(5.0f));

    agc.reset();
    agc.process(4, input.data(), out.data());
    REQUIRE(out[0] == Approx(2.0f));
}
