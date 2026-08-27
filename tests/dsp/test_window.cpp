// Window functions used for FFT display and FIR design.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/window/blackman.h>
#include <dsp/window/blackman_harris.h>
#include <dsp/window/blackman_nuttall.h>
#include <dsp/window/cosine.h>
#include <dsp/window/hamming.h>
#include <dsp/window/hann.h>
#include <dsp/window/nuttall.h>
#include <dsp/window/rectangular.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    using WindowFn = double (*)(double, double);

    struct NamedWindow {
        const char* name;
        WindowFn fn;
        double dcValue; // sum of the cosine coefficients with alternating signs
    };

    const NamedWindow WINDOWS[] = {
        { "hann", dsp::window::hann, 0.5 - 0.5 },
        { "hamming", dsp::window::hamming, 0.54 - 0.46 },
        { "blackman", dsp::window::blackman, 0.42 - 0.5 + 0.08 },
        { "blackmanHarris", dsp::window::blackmanHarris, 0.35875 - 0.48829 + 0.14128 - 0.01168 },
        { "blackmanNuttall", dsp::window::blackmanNuttall, 0.3635819 - 0.4891775 + 0.1365995 - 0.0106411 },
        { "nuttall", dsp::window::nuttall, 0.355768 - 0.487396 + 0.144232 - 0.012604 },
    };
}

TEST_CASE("rectangular window is flat", "[dsp][window]") {
    for (int i = 0; i <= 32; i++) {
        REQUIRE(dsp::window::rectangular((double)i, 32.0) == 1.0);
    }
}

TEST_CASE("cosine window sums its coefficients with alternating signs", "[dsp][window]") {
    const double coefs[] = { 1.0, 2.0, 3.0 };
    // At n == 0 every cosine term is 1, so the result is c0 - c1 + c2.
    REQUIRE(dsp::window::cosine(0.0, 16.0, coefs, 3) == Approx(1.0 - 2.0 + 3.0));
    // At n == N/2 the odd terms flip sign, giving c0 + c1 + c2.
    REQUIRE(dsp::window::cosine(8.0, 16.0, coefs, 3) == Approx(1.0 + 2.0 + 3.0));
}

TEST_CASE("windows peak in the middle and taper at the edges", "[dsp][window]") {
    const double N = 64.0;
    for (const auto& w : WINDOWS) {
        INFO("window: " << w.name);

        // Edges hold the alternating coefficient sum.
        REQUIRE(w.fn(0.0, N) == Approx(w.dcValue).margin(1e-9));
        REQUIRE(w.fn(N, N) == Approx(w.dcValue).margin(1e-9));

        // The maximum is at the centre.
        double centre = w.fn(N / 2.0, N);
        for (int i = 0; i <= (int)N; i++) {
            REQUIRE(w.fn((double)i, N) <= centre + 1e-9);
        }
        REQUIRE(centre > 0.5);
    }
}

TEST_CASE("windows are symmetric about the centre", "[dsp][window]") {
    const double N = 64.0;
    for (const auto& w : WINDOWS) {
        INFO("window: " << w.name);
        for (int i = 0; i <= (int)N; i++) {
            REQUIRE(w.fn((double)i, N) == Approx(w.fn(N - (double)i, N)).margin(1e-12));
        }
    }
}

TEST_CASE("windows are even in n", "[dsp][window]") {
    // Callers pass negative n (windowedSinc does), which relies on cos() being
    // even. Pinned so that a rewrite using a table doesn't break it.
    const double N = 64.0;
    for (const auto& w : WINDOWS) {
        INFO("window: " << w.name);
        for (int i = 1; i <= 32; i++) {
            REQUIRE(w.fn((double)i, N) == Approx(w.fn(-(double)i, N)).margin(1e-12));
        }
    }
}

TEST_CASE("hann matches its closed form", "[dsp][window]") {
    const double N = 32.0;
    for (int i = 0; i <= (int)N; i++) {
        double expected = 0.5 - 0.5 * std::cos(2.0 * PI * (double)i / N);
        // The implementation computes 0.5 - 0.5*cos, i.e. the same value.
        REQUIRE(dsp::window::hann((double)i, N) == Approx(expected).margin(1e-12));
    }
}

TEST_CASE("nuttall has a small DC leakage compared to rectangular", "[dsp][window]") {
    // Sidelobe suppression is the reason the FFT path uses nuttall. Compare the
    // response one bin away from DC for both windows.
    const int N = 256;
    std::vector<float> rect(N), nutt(N);
    for (int i = 0; i < N; i++) {
        rect[i] = 1.0f;
        nutt[i] = (float)dsp::window::nuttall((double)i, (double)N);
    }

    double rectLeak = tapResponse(rect.data(), N, 3.5, (double)N);
    double nuttLeak = tapResponse(nutt.data(), N, 3.5, (double)N);
    double rectDC = tapResponse(rect.data(), N, 0.0, (double)N);
    double nuttDC = tapResponse(nutt.data(), N, 0.0, (double)N);

    REQUIRE((nuttLeak / nuttDC) < (rectLeak / rectDC));
}
