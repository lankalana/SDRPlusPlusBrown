// dsp::complex_t / dsp::stereo_t arithmetic and the fast approximations that
// several hot loops rely on.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <dsp/types.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

TEST_CASE("complex_t has the memory layout volk expects", "[dsp][types]") {
    // Every volk call in the DSP casts complex_t* to lv_32fc_t*. That is only
    // valid while complex_t stays two tightly packed floats, re first.
    REQUIRE(sizeof(dsp::complex_t) == 2 * sizeof(float));
    REQUIRE(alignof(dsp::complex_t) == alignof(float));
    REQUIRE(offsetof(dsp::complex_t, re) == 0);
    REQUIRE(offsetof(dsp::complex_t, im) == sizeof(float));

    REQUIRE(sizeof(dsp::stereo_t) == 2 * sizeof(float));
    REQUIRE(offsetof(dsp::stereo_t, l) == 0);
    REQUIRE(offsetof(dsp::stereo_t, r) == sizeof(float));
}

TEST_CASE("complex_t arithmetic", "[dsp][types]") {
    dsp::complex_t a{ 1.0f, 2.0f };
    dsp::complex_t b{ 3.0f, -4.0f };

    SECTION("addition and subtraction") {
        REQUIRE((a + b).re == 4.0f);
        REQUIRE((a + b).im == -2.0f);
        REQUIRE((a - b).re == -2.0f);
        REQUIRE((a - b).im == 6.0f);
    }

    SECTION("complex multiplication") {
        auto p = a * b;
        // (1+2i)(3-4i) = 3 - 4i + 6i + 8 = 11 + 2i
        REQUIRE(p.re == Approx(11.0f));
        REQUIRE(p.im == Approx(2.0f));
    }

    SECTION("scalar multiplication and division") {
        REQUIRE((a * 2.0f).re == 2.0f);
        REQUIRE((a * 2.0f).im == 4.0f);
        REQUIRE((a / 2.0f).re == 0.5f);
        REQUIRE((a / 2.0f).im == 1.0f);
        REQUIRE((a * 2.0).re == 2.0f); // double overload
        REQUIRE((a / 2.0).im == 1.0f);
    }

    SECTION("compound assignment") {
        dsp::complex_t c = a;
        c += b;
        REQUIRE(c.re == 4.0f);
        c -= b;
        REQUIRE(c.re == 1.0f);
        REQUIRE(c.im == 2.0f);
        c *= 3.0f;
        REQUIRE(c.re == 3.0f);
        REQUIRE(c.im == 6.0f);
    }

    SECTION("conjugate") {
        REQUIRE(a.conj().re == 1.0f);
        REQUIRE(a.conj().im == -2.0f);
    }
}

TEST_CASE("complex_t phase and amplitude", "[dsp][types]") {
    SECTION("exact") {
        dsp::complex_t c{ 0.0f, 1.0f };
        REQUIRE(c.phase() == Approx((float)(PI / 2.0)));
        REQUIRE(c.amplitude() == Approx(1.0f));

        dsp::complex_t d{ 3.0f, 4.0f };
        REQUIRE(d.amplitude() == Approx(5.0f));
    }

    SECTION("fastPhase stays within the documented error of atan2") {
        // The approximation is used in the FM discriminator. Its worst case
        // error over the full circle is about 0.07 rad (4 deg); pinned so that
        // swapping in a different approximation is a visible decision.
        double worst = 0.0;
        for (int i = 0; i < 1000; i++) {
            double ang = -PI + 2.0 * PI * (double)i / 1000.0;
            dsp::complex_t c{ (float)std::cos(ang), (float)std::sin(ang) };
            double err = std::fabs((double)c.fastPhase() - std::atan2(c.im, c.re));
            worst = (std::max)(worst, err);
        }
        REQUIRE(worst < 0.08);
    }

    SECTION("fastPhase and fastAmplitude handle the origin") {
        dsp::complex_t zero{ 0.0f, 0.0f };
        REQUIRE(zero.fastPhase() == 0.0f);
        REQUIRE(zero.fastAmplitude() == 0.0f);
        REQUIRE(zero.amplitude() == 0.0f);
    }

    SECTION("fastAmplitude is within 12% of the true magnitude") {
        double worst = 0.0;
        for (int i = 0; i < 1000; i++) {
            double ang = 2.0 * PI * (double)i / 1000.0;
            dsp::complex_t c{ (float)std::cos(ang), (float)std::sin(ang) };
            worst = (std::max)(worst, std::fabs((double)c.fastAmplitude() - 1.0));
        }
        REQUIRE(worst < 0.12);
    }
}

TEST_CASE("stereo_t arithmetic", "[dsp][types]") {
    dsp::stereo_t a{ 1.0f, 2.0f };
    dsp::stereo_t b{ 0.5f, 0.25f };

    REQUIRE((a + b).l == 1.5f);
    REQUIRE((a + b).r == 2.25f);
    REQUIRE((a - b).l == 0.5f);
    REQUIRE((a * 2.0f).r == 4.0f);

    dsp::stereo_t c = a;
    c += b;
    REQUIRE(c.r == 2.25f);
    c -= b;
    REQUIRE(c.l == 1.0f);
    c *= 0.0f;
    REQUIRE(c.l == 0.0f);
    REQUIRE(c.r == 0.0f);
}
