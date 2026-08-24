// Filter-tap designers. These tests check the resulting frequency response
// rather than individual coefficients, so an optimised rewrite of the
// generators stays free to change the arithmetic.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <dsp/taps/band_pass.h>
#include <dsp/taps/estimate_tap_count.h>
#include <dsp/taps/from_array.h>
#include <dsp/taps/high_pass.h>
#include <dsp/taps/low_pass.h>
#include <dsp/taps/raised_cosine.h>
#include <dsp/taps/root_raised_cosine.h>
#include <dsp/taps/tap.h>
#include <dsp/taps/windowed_sinc.h>
#include <dsp/window/nuttall.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    // RAII wrapper so a failing REQUIRE doesn't leak the volk allocation.
    template <class T>
    struct ScopedTaps {
        explicit ScopedTaps(dsp::tap<T> t) : taps(t) {}
        ~ScopedTaps() { dsp::taps::free(taps); }
        ScopedTaps(const ScopedTaps&) = delete;
        ScopedTaps& operator=(const ScopedTaps&) = delete;
        dsp::tap<T> taps;
    };

    double dB(double lin) { return 20.0 * std::log10((std::max)(lin, 1e-12)); }
}

TEST_CASE("taps alloc and free", "[dsp][taps]") {
    auto taps = dsp::taps::alloc<float>(17);
    REQUIRE(taps.size == 17);
    REQUIRE(taps.taps != nullptr);

    dsp::taps::free(taps);
    REQUIRE(taps.size == 0);
    REQUIRE(taps.taps == nullptr);

    // Freeing twice must be safe: several blocks do it on reconfiguration.
    dsp::taps::free(taps);
}

TEST_CASE("taps fromArray copies the coefficients", "[dsp][taps]") {
    const float src[] = { 1.0f, -2.0f, 3.0f };
    ScopedTaps<float> t(dsp::taps::fromArray<float>(3, src));

    REQUIRE(t.taps.size == 3);
    REQUIRE(t.taps.taps[0] == 1.0f);
    REQUIRE(t.taps.taps[1] == -2.0f);
    REQUIRE(t.taps.taps[2] == 3.0f);
    REQUIRE(t.taps.taps != src); // it is a copy, not an alias
}

TEST_CASE("estimateTapCount scales with the transition width", "[dsp][taps]") {
    REQUIRE(dsp::taps::estimateTapCount(1000.0, 48000.0) == (int)(3.8 * 48000.0 / 1000.0));
    // Halving the transition width doubles the tap count.
    REQUIRE(dsp::taps::estimateTapCount(500.0, 48000.0) == 2 * dsp::taps::estimateTapCount(1000.0, 48000.0));
    // Higher sample rate at the same transition width needs more taps.
    REQUIRE(dsp::taps::estimateTapCount(1000.0, 96000.0) > dsp::taps::estimateTapCount(1000.0, 48000.0));
}

TEST_CASE("lowPass passes the band and rejects the stopband", "[dsp][taps][filter-design]") {
    const double sr = 48000.0;
    const double cutoff = 4000.0;
    ScopedTaps<float> t(dsp::taps::lowPass(cutoff, 1000.0, sr));

    REQUIRE(t.taps.size > 0);

    double dc = tapResponse(t.taps.taps, t.taps.size, 0.0, sr);
    REQUIRE(dc == Approx(1.0).margin(0.05)); // unity passband gain

    REQUIRE(tapResponse(t.taps.taps, t.taps.size, 1000.0, sr) == Approx(dc).margin(0.05));
    // Well into the stopband the response must be far down.
    REQUIRE(dB(tapResponse(t.taps.taps, t.taps.size, 12000.0, sr) / dc) < -60.0);
    REQUIRE(dB(tapResponse(t.taps.taps, t.taps.size, 20000.0, sr) / dc) < -60.0);
}

TEST_CASE("lowPass taps are symmetric (linear phase)", "[dsp][taps][filter-design]") {
    ScopedTaps<float> t(dsp::taps::lowPass(4000.0, 2000.0, 48000.0));
    unsigned int n = t.taps.size;
    for (unsigned int i = 0; i < n / 2; i++) {
        REQUIRE(t.taps.taps[i] == Approx(t.taps.taps[n - 1 - i]).margin(1e-6));
    }
}

TEST_CASE("lowPass honours the odd tap count request", "[dsp][taps][filter-design]") {
    ScopedTaps<float> even(dsp::taps::lowPass(4000.0, 1000.0, 48000.0, false));
    ScopedTaps<float> odd(dsp::taps::lowPass(4000.0, 1000.0, 48000.0, true));

    REQUIRE(odd.taps.size % 2 == 1);
    REQUIRE(odd.taps.size >= even.taps.size);
    REQUIRE(odd.taps.size - even.taps.size <= 1);
}

TEST_CASE("highPass rejects DC and passes the top of the band", "[dsp][taps][filter-design]") {
    const double sr = 48000.0;
    ScopedTaps<float> t(dsp::taps::highPass(4000.0, 1000.0, sr, true));

    double pass = tapResponse(t.taps.taps, t.taps.size, 20000.0, sr);
    REQUIRE(pass == Approx(1.0).margin(0.1));
    REQUIRE(dB(tapResponse(t.taps.taps, t.taps.size, 0.0, sr) / pass) < -50.0);
    REQUIRE(dB(tapResponse(t.taps.taps, t.taps.size, 500.0, sr) / pass) < -50.0);
}

TEST_CASE("bandPass<float> is centred on the requested band", "[dsp][taps][filter-design]") {
    const double sr = 48000.0;
    ScopedTaps<float> t(dsp::taps::bandPass<float>(5000.0, 9000.0, 500.0, sr));

    double centre = tapResponse(t.taps.taps, t.taps.size, 7000.0, sr);
    REQUIRE(centre > 0.5);

    REQUIRE(dB(tapResponse(t.taps.taps, t.taps.size, 0.0, sr) / centre) < -50.0);
    REQUIRE(dB(tapResponse(t.taps.taps, t.taps.size, 20000.0, sr) / centre) < -50.0);

    // Real taps give a symmetric response: the negative band passes too.
    REQUIRE(tapResponse(t.taps.taps, t.taps.size, -7000.0, sr) == Approx(centre).margin(centre * 0.01));
}

TEST_CASE("bandPass<complex_t> is asymmetric", "[dsp][taps][filter-design]") {
    // A complex band-pass selects a single sideband instead of a mirrored pair.
    // Note the sign: the generator negates the offset ("Complex bandpass are
    // asymetric"), so asking for the 5..9 kHz band produces taps that pass
    // -7 kHz and reject +7 kHz. Pinned because callers compensate for it.
    const double sr = 48000.0;
    ScopedTaps<dsp::complex_t> t(dsp::taps::bandPass<dsp::complex_t>(5000.0, 9000.0, 500.0, sr));

    double atPlus = tapResponse(t.taps.taps, t.taps.size, 7000.0, sr);
    double atMinus = tapResponse(t.taps.taps, t.taps.size, -7000.0, sr);

    REQUIRE(atMinus > 0.4);
    REQUIRE(dB(atPlus / atMinus) < -50.0);
}

TEST_CASE("windowedSinc gain scales with the cutoff", "[dsp][taps][filter-design]") {
    // The generator normalises by omega/pi, so a wider filter keeps unity gain
    // rather than growing with bandwidth.
    const double sr = 48000.0;
    ScopedTaps<float> narrow(dsp::taps::windowedSinc<float>(201, 2000.0, sr, dsp::window::nuttall));
    ScopedTaps<float> wide(dsp::taps::windowedSinc<float>(201, 8000.0, sr, dsp::window::nuttall));

    REQUIRE(tapResponse(narrow.taps.taps, narrow.taps.size, 0.0, sr) == Approx(1.0).margin(0.05));
    REQUIRE(tapResponse(wide.taps.taps, wide.taps.size, 0.0, sr) == Approx(1.0).margin(0.05));
}

TEST_CASE("windowedSinc norm parameter scales the taps linearly", "[dsp][taps][filter-design]") {
    const double sr = 48000.0;
    ScopedTaps<float> one(dsp::taps::windowedSinc<float>(101, 4000.0, sr, dsp::window::nuttall, 1.0));
    ScopedTaps<float> three(dsp::taps::windowedSinc<float>(101, 4000.0, sr, dsp::window::nuttall, 3.0));

    for (unsigned int i = 0; i < one.taps.size; i++) {
        REQUIRE(three.taps.taps[i] == Approx(3.0f * one.taps.taps[i]).margin(1e-6));
    }
}

TEST_CASE("windowedSinc for complex and stereo mirrors the float taps", "[dsp][taps][filter-design]") {
    const double sr = 48000.0;
    ScopedTaps<float> f(dsp::taps::windowedSinc<float>(51, 4000.0, sr, dsp::window::nuttall));
    ScopedTaps<dsp::complex_t> c(dsp::taps::windowedSinc<dsp::complex_t>(51, 4000.0, sr, dsp::window::nuttall));
    ScopedTaps<dsp::stereo_t> s(dsp::taps::windowedSinc<dsp::stereo_t>(51, 4000.0, sr, dsp::window::nuttall));

    for (unsigned int i = 0; i < f.taps.size; i++) {
        REQUIRE(c.taps.taps[i].re == Approx(f.taps.taps[i]).margin(1e-6));
        REQUIRE(c.taps.taps[i].im == Approx(0.0f).margin(1e-6));
        REQUIRE(s.taps.taps[i].l == Approx(f.taps.taps[i]).margin(1e-6));
        REQUIRE(s.taps.taps[i].r == Approx(f.taps.taps[i]).margin(1e-6));
    }
}

TEST_CASE("rootRaisedCosine is symmetric and peaks in the middle", "[dsp][taps][filter-design]") {
    ScopedTaps<float> t(dsp::taps::rootRaisedCosine<float>(65, 0.35, 4.0));

    unsigned int n = t.taps.size;
    REQUIRE(n == 65);
    for (unsigned int i = 0; i < n / 2; i++) {
        REQUIRE(t.taps.taps[i] == Approx(t.taps.taps[n - 1 - i]).margin(1e-5));
    }

    float maxTap = 0.0f;
    unsigned int maxIdx = 0;
    for (unsigned int i = 0; i < n; i++) {
        if (std::fabs(t.taps.taps[i]) > maxTap) {
            maxTap = std::fabs(t.taps.taps[i]);
            maxIdx = i;
        }
    }
    REQUIRE(maxIdx >= n / 2 - 1);
    REQUIRE(maxIdx <= n / 2);
}

TEST_CASE("rootRaisedCosine symbolrate overload matches the Ts overload", "[dsp][taps][filter-design]") {
    ScopedTaps<float> a(dsp::taps::rootRaisedCosine<float>(33, 0.5, 4.0));
    ScopedTaps<float> b(dsp::taps::rootRaisedCosine<float>(33, 0.5, 12000.0, 48000.0));

    REQUIRE(a.taps.size == b.taps.size);
    for (unsigned int i = 0; i < a.taps.size; i++) {
        REQUIRE(a.taps.taps[i] == Approx(b.taps.taps[i]).margin(1e-6));
    }
}

TEST_CASE("raisedCosine is symmetric", "[dsp][taps][filter-design]") {
    ScopedTaps<float> t(dsp::taps::raisedCosine<float>(65, 0.35, 4.0));
    unsigned int n = t.taps.size;
    for (unsigned int i = 0; i < n / 2; i++) {
        REQUIRE(t.taps.taps[i] == Approx(t.taps.taps[n - 1 - i]).margin(1e-5));
    }
}
