// Scalar helpers in dsp/math plus the stream operators built on volk.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/math/add.h>
#include <dsp/math/conjugate.h>
#include <dsp/math/constants.h>
#include <dsp/math/delay.h>
#include <dsp/math/fast_atan2.h>
#include <dsp/math/hz_to_rads.h>
#include <dsp/math/multiply.h>
#include <dsp/math/normalize_phase.h>
#include <dsp/math/phasor.h>
#include <dsp/math/sinc.h>
#include <dsp/math/step.h>
#include <dsp/math/subtract.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

TEST_CASE("math constants match their std counterparts", "[dsp][math]") {
    REQUIRE(DB_M_PI == Approx(std::acos(-1.0)).epsilon(1e-15));
    REQUIRE(DB_M_SQRT2 == Approx(std::sqrt(2.0)).epsilon(1e-15));
    // FL_M_PI is a float literal, so it only matches to single precision.
    REQUIRE((double)FL_M_PI == Approx(std::acos(-1.0)).epsilon(1e-7));
}

TEST_CASE("sinc", "[dsp][math]") {
    REQUIRE(dsp::math::sinc(0.0) == 1.0);
    REQUIRE(dsp::math::sinc(DB_M_PI) == Approx(0.0).margin(1e-15));
    REQUIRE(dsp::math::sinc(DB_M_PI / 2.0) == Approx(2.0 / DB_M_PI));
    // Even function.
    REQUIRE(dsp::math::sinc(1.234) == Approx(dsp::math::sinc(-1.234)));
}

TEST_CASE("hzToRads", "[dsp][math]") {
    REQUIRE(dsp::math::hzToRads(0.0, 48000.0) == 0.0);
    // Nyquist maps to pi.
    REQUIRE(dsp::math::hzToRads(24000.0, 48000.0) == Approx(DB_M_PI));
    REQUIRE(dsp::math::hzToRads(1000.0, 48000.0) == Approx(2.0 * DB_M_PI / 48.0));
    // Negative frequencies stay negative.
    REQUIRE(dsp::math::hzToRads(-1000.0, 48000.0) == Approx(-2.0 * DB_M_PI / 48.0));
}

TEST_CASE("normalizePhase wraps into (-pi, pi]", "[dsp][math]") {
    REQUIRE(dsp::math::normalizePhase(0.0f) == 0.0f);
    REQUIRE(dsp::math::normalizePhase(FL_M_PI) == Approx(FL_M_PI));
    REQUIRE(dsp::math::normalizePhase(FL_M_PI + 0.1f) == Approx(-FL_M_PI + 0.1f).margin(1e-5));
    REQUIRE(dsp::math::normalizePhase(-FL_M_PI - 0.1f) == Approx(FL_M_PI - 0.1f).margin(1e-5));

    // A single correction only: the helper is not a general modulo. Pinning
    // this because callers rely on inputs already being within (-2pi, 2pi).
    float wayOut = 5.0f * FL_M_PI;
    REQUIRE(dsp::math::normalizePhase(wayOut) == Approx(3.0f * FL_M_PI));
}

TEST_CASE("fastAtan2 approximates atan2", "[dsp][math]") {
    double worst = 0.0;
    for (int i = 0; i < 2000; i++) {
        double ang = -PI + 2.0 * PI * (double)i / 2000.0;
        float x = (float)std::cos(ang);
        float y = (float)std::sin(ang);
        // Note the argument order: fastAtan2(x, y) mirrors atan2(y, x).
        worst = (std::max)(worst, std::fabs((double)dsp::math::fastAtan2(x, y) - std::atan2(y, x)));
    }
    // The single-term rational approximation is good to about 0.07 rad (4 deg)
    // over the full circle. Pinned so a change in the approximation is visible.
    REQUIRE(worst < 0.08);
    REQUIRE(worst > 0.05);

    REQUIRE(dsp::math::fastAtan2(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("phasor produces a unit vector at the requested angle", "[dsp][math]") {
    auto p = dsp::math::phasor(0.0f);
    REQUIRE(p.re == Approx(1.0f));
    REQUIRE(p.im == Approx(0.0f).margin(1e-6));

    auto q = dsp::math::phasor((float)(PI / 2.0));
    REQUIRE(q.re == Approx(0.0f).margin(1e-6));
    REQUIRE(q.im == Approx(1.0f));
    REQUIRE(q.amplitude() == Approx(1.0f));
}

TEST_CASE("step", "[dsp][math]") {
    REQUIRE(dsp::math::step(1.0f) == 1.0f);
    REQUIRE(dsp::math::step(-1.0f) == -1.0f);
    REQUIRE(dsp::math::step(0.0f) == -1.0f); // zero maps to -1

    auto c = dsp::math::step(dsp::complex_t{ 0.5f, -0.5f });
    REQUIRE(c.re == 1.0f);
    REQUIRE(c.im == -1.0f);

    auto s = dsp::math::step(dsp::stereo_t{ -0.1f, 0.1f });
    REQUIRE(s.l == -1.0f);
    REQUIRE(s.r == 1.0f);
}

TEST_CASE("Conjugate::process flips the imaginary part", "[dsp][math]") {
    auto in = complexTone(64, 1000.0, 48000.0);
    std::vector<dsp::complex_t> out(in.size());

    REQUIRE(dsp::math::Conjugate::process((int)in.size(), in.data(), out.data()) == (int)in.size());
    for (size_t i = 0; i < in.size(); i++) {
        REQUIRE(out[i].re == Approx(in[i].re));
        REQUIRE(out[i].im == Approx(-in[i].im));
    }
}

TEST_CASE("Conjugate mirrors the spectrum", "[dsp][math]") {
    // This is the IQ-inversion stage of IQFrontEnd: a tone at +f must land on -f.
    const double sr = 48000.0;
    auto in = complexTone(4096, 3000.0, sr);
    std::vector<dsp::complex_t> out(in.size());
    dsp::math::Conjugate::process((int)in.size(), in.data(), out.data());

    REQUIRE(goertzelMag(out, -3000.0, sr) > 0.9);
    REQUIRE(goertzelMag(out, 3000.0, sr) < 0.05);
}

TEST_CASE("Add / Subtract / Multiply process functions", "[dsp][math]") {
    SECTION("float") {
        std::vector<float> a = { 1.0f, 2.0f, 3.0f, 4.0f };
        std::vector<float> b = { 0.5f, 0.5f, 0.5f, 0.5f };
        std::vector<float> out(4);

        dsp::math::Add<float>::process(4, a.data(), b.data(), out.data());
        REQUIRE(out[3] == Approx(4.5f));

        dsp::math::Subtract<float>::process(4, a.data(), b.data(), out.data());
        REQUIRE(out[0] == Approx(0.5f));

        dsp::math::Multiply<float>::process(4, a.data(), b.data(), out.data());
        REQUIRE(out[2] == Approx(1.5f));
    }

    SECTION("complex") {
        std::vector<dsp::complex_t> a = { { 1.0f, 2.0f }, { 3.0f, 4.0f } };
        std::vector<dsp::complex_t> b = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
        std::vector<dsp::complex_t> out(2);

        dsp::math::Add<dsp::complex_t>::process(2, a.data(), b.data(), out.data());
        REQUIRE(out[0].im == Approx(3.0f));

        dsp::math::Subtract<dsp::complex_t>::process(2, a.data(), b.data(), out.data());
        REQUIRE(out[1].re == Approx(2.0f));

        dsp::math::Multiply<dsp::complex_t>::process(2, a.data(), b.data(), out.data());
        // (1+2i)*(i) = -2 + i
        REQUIRE(out[0].re == Approx(-2.0f));
        REQUIRE(out[0].im == Approx(1.0f));
    }
}

TEST_CASE("Delay shifts the stream by the configured amount", "[dsp][math]") {
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::math::Delay<float> delay;
    delay.init(&in, 4);
    delay.out.setBufferSize(1024);

    // The delay line starts zeroed, so the first `delay` outputs are zeros.
    auto input = ramp(16, 1.0f);
    std::vector<float> out(16);
    REQUIRE(delay.process(16, input.data(), out.data()) == 16);

    for (int i = 0; i < 4; i++) { REQUIRE(out[i] == 0.0f); }
    for (int i = 4; i < 16; i++) { REQUIRE(out[i] == input[i - 4]); }

    // State carries over between calls.
    std::vector<float> out2(16);
    auto input2 = ramp(16, 100.0f);
    delay.process(16, input2.data(), out2.data());
    for (int i = 0; i < 4; i++) { REQUIRE(out2[i] == input[12 + i]); }
    REQUIRE(out2[4] == input2[0]);
}
