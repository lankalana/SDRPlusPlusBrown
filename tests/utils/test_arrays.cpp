// utils/arrays.h: the numpy-flavoured array layer used by the noise reduction,
// the signal detector and the FT8 decoder.
//
// Every function here allocates a fresh shared_ptr vector, which is the single
// biggest source of allocation churn in the audio path. These tests pin the
// semantics so that a rewrite to in-place / span-based APIs can be checked
// against them.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <vector>

#include <utils/arrays.h>

using Catch::Approx;
using namespace dsp::arrays;

namespace {
    FloatArray fa(std::initializer_list<float> v) {
        return std::make_shared<std::vector<float>>(v);
    }

    ComplexArray ca(std::initializer_list<dsp::complex_t> v) {
        return std::make_shared<std::vector<dsp::complex_t>>(v);
    }
}

// ------------------------------------------------------------- construction

TEST_CASE("npzeros and npzeros_c produce zeroed arrays", "[utils][arrays]") {
    auto z = npzeros(5);
    REQUIRE(z->size() == 5);
    for (float v : *z) { REQUIRE(v == 0.0f); }

    auto zc = npzeros_c(3);
    REQUIRE(zc->size() == 3);
    for (const auto& v : *zc) {
        REQUIRE(v.re == 0.0f);
        REQUIRE(v.im == 0.0f);
    }
}

TEST_CASE("linspace spans the endpoints inclusively", "[utils][arrays]") {
    auto v = linspace(0.0f, 1.0f, 5);
    REQUIRE(v->size() == 5);
    REQUIRE(v->at(0) == Approx(0.0f));
    REQUIRE(v->at(4) == Approx(1.0f));
    REQUIRE(v->at(2) == Approx(0.5f));
}

TEST_CASE("nphanning and hamming are symmetric windows", "[utils][arrays]") {
    const int n = 33;

    auto han = nphanning(n);
    REQUIRE(han->size() == n);
    REQUIRE(han->at(0) == Approx(0.0f).margin(1e-6));
    REQUIRE(han->at(n - 1) == Approx(0.0f).margin(1e-6));
    REQUIRE(han->at(n / 2) == Approx(1.0f).margin(1e-6));

    auto ham = hamming(n);
    REQUIRE(ham->at(0) == Approx(0.08f).margin(1e-5));
    REQUIRE(ham->at(n / 2) == Approx(1.0f).margin(1e-5));

    for (int i = 0; i < n; i++) {
        INFO("index " << i);
        REQUIRE(han->at(i) == Approx(han->at(n - 1 - i)).margin(1e-5));
        REQUIRE(ham->at(i) == Approx(ham->at(n - 1 - i)).margin(1e-5));
    }
}

// --------------------------------------------------------------- arithmetic

TEST_CASE("scalar mul, add and div", "[utils][arrays]") {
    auto v = fa({ 1.0f, 2.0f, 4.0f });

    auto m = mul(v, 3.0f);
    REQUIRE(*m == std::vector<float>{ 3.0f, 6.0f, 12.0f });

    auto a = add(v, 0.5f);
    REQUIRE(*a == std::vector<float>{ 1.5f, 2.5f, 4.5f });

    auto d = div(v, 2.0f);
    REQUIRE(*d == std::vector<float>{ 0.5f, 1.0f, 2.0f });

    // The originals are untouched: these are pure functions.
    REQUIRE(*v == std::vector<float>{ 1.0f, 2.0f, 4.0f });
}

TEST_CASE("div_ mutates in place", "[utils][arrays]") {
    auto v = fa({ 2.0f, 4.0f });
    div_(v, 2.0f);
    REQUIRE(*v == std::vector<float>{ 1.0f, 2.0f });

    auto c = ca({ { 2.0f, 4.0f } });
    div_(c, 2.0f);
    REQUIRE(c->at(0).re == Approx(1.0f));
    REQUIRE(c->at(0).im == Approx(2.0f));
}

TEST_CASE("element-wise add, subtract, multiply and divide", "[utils][arrays]") {
    auto a = fa({ 1.0f, 2.0f, 3.0f });
    auto b = fa({ 4.0f, 5.0f, 6.0f });

    REQUIRE(*addeach(a, b) == std::vector<float>{ 5.0f, 7.0f, 9.0f });
    REQUIRE(*subeach(b, a) == std::vector<float>{ 3.0f, 3.0f, 3.0f });
    REQUIRE(*muleach(a, b) == std::vector<float>{ 4.0f, 10.0f, 18.0f });

    auto q = diveach(b, a);
    REQUIRE(q->at(0) == Approx(4.0f));
    REQUIRE(q->at(2) == Approx(2.0f));
}

TEST_CASE("complex element-wise operations", "[utils][arrays]") {
    auto a = ca({ { 1.0f, 2.0f }, { 3.0f, -1.0f } });
    auto b = ca({ { 0.5f, 0.5f }, { 1.0f, 1.0f } });

    auto sum = addeach(a, b);
    REQUIRE(sum->at(0).re == Approx(1.5f));
    REQUIRE(sum->at(1).im == Approx(0.0f));

    auto scale = fa({ 2.0f, 3.0f });
    auto scaled = muleach(scale, a);
    REQUIRE(scaled->at(0).re == Approx(2.0f));
    REQUIRE(scaled->at(1).re == Approx(9.0f));
}

TEST_CASE("npsum adds every element", "[utils][arrays]") {
    REQUIRE(npsum(fa({ 1.0f, 2.0f, 3.5f })) == Approx(6.5f));
    REQUIRE(npsum(npzeros(10)) == Approx(0.0f));
}

TEST_CASE("npmax, npmin and amin", "[utils][arrays]") {
    auto v = fa({ 3.0f, -1.0f, 7.0f, 0.0f });
    REQUIRE(npmax(v) == Approx(7.0f));
    REQUIRE(npmin(v) == Approx(-1.0f));
    REQUIRE(amin(v) == Approx(-1.0f));

    // For complex arrays amin picks the smallest magnitude, not the smallest
    // real part.
    auto c = ca({ { 3.0f, 4.0f }, { 0.3f, 0.4f }, { -10.0f, 0.0f } });
    auto smallest = amin(c);
    REQUIRE(smallest.re == Approx(0.3f));
    REQUIRE(smallest.im == Approx(0.4f));
}

TEST_CASE("npminimum and npmaximum clamp against a scalar or an array", "[utils][arrays]") {
    auto v = fa({ 1.0f, 5.0f, -2.0f });

    REQUIRE(*npminimum(v, 2.0f) == std::vector<float>{ 1.0f, 2.0f, -2.0f });
    REQUIRE(*npmaximum(v, 0.0f) == std::vector<float>{ 1.0f, 5.0f, 0.0f });

    auto w = fa({ 3.0f, 1.0f, 1.0f });
    REQUIRE(*npminimum(v, w) == std::vector<float>{ 1.0f, 1.0f, -2.0f });
}

TEST_CASE("npminimum_ and npmaximum_ clamp in place", "[utils][arrays]") {
    auto v = fa({ 1.0f, 5.0f, -2.0f });
    npminimum_(v, 2.0f);
    REQUIRE(*v == std::vector<float>{ 1.0f, 2.0f, -2.0f });

    npmaximum_(v, 0.0f);
    REQUIRE(*v == std::vector<float>{ 1.0f, 2.0f, 0.0f });
}

TEST_CASE("npall is false when any element is zero", "[utils][arrays]") {
    REQUIRE(npall(fa({ 1.0f, 2.0f })));
    REQUIRE_FALSE(npall(fa({ 1.0f, 0.0f })));
    REQUIRE(npall(npzeros(0)));
}

TEST_CASE("neg, npexp, npsqrt and nplog", "[utils][arrays]") {
    REQUIRE(*neg(fa({ 1.0f, -2.0f })) == std::vector<float>{ -1.0f, 2.0f });

    auto e = npexp(fa({ 0.0f, 1.0f }));
    REQUIRE(e->at(0) == Approx(1.0f));
    REQUIRE(e->at(1) == Approx(std::exp(1.0f)));

    auto s = npsqrt(fa({ 4.0f, 9.0f }));
    REQUIRE(s->at(0) == Approx(2.0f));
    REQUIRE(s->at(1) == Approx(3.0f));

    auto l = nplog(fa({ 1.0f, std::exp(1.0f) }));
    REQUIRE(l->at(0) == Approx(0.0f).margin(1e-6));
    REQUIRE(l->at(1) == Approx(1.0f).margin(1e-5));
}

TEST_CASE("exp free function matches npexp", "[utils][arrays]") {
    auto v = fa({ -1.0f, 0.0f, 2.0f });
    auto a = npexp(v);
    auto b = dsp::arrays::exp(v);
    for (size_t i = 0; i < v->size(); i++) {
        REQUIRE(a->at(i) == Approx(b->at(i)));
    }
}

// ------------------------------------------------------------- reshaping

TEST_CASE("nparange slices a range", "[utils][arrays]") {
    auto v = fa({ 0.0f, 1.0f, 2.0f, 3.0f, 4.0f });
    REQUIRE(*nparange(v, 1, 4) == std::vector<float>{ 1.0f, 2.0f, 3.0f });

    // end == -1 means "to the end".
    REQUIRE(nparange(v, 3, -1)->size() == 2);

    auto c = ca({ { 0, 0 }, { 1, 1 }, { 2, 2 } });
    auto slice = nparange(c, 1, 3);
    REQUIRE(slice->size() == 2);
    REQUIRE(slice->at(0).re == Approx(1.0f));
}

TEST_CASE("nparangeset writes a slice back in place", "[utils][arrays]") {
    auto v = fa({ 0.0f, 0.0f, 0.0f, 0.0f });
    nparangeset(v, 1, fa({ 5.0f, 6.0f }));
    REQUIRE(*v == std::vector<float>{ 0.0f, 5.0f, 6.0f, 0.0f });

    auto c = npzeros_c(4);
    nparangeset(c, 2, ca({ { 1.0f, 2.0f } }));
    REQUIRE(c->at(2).re == Approx(1.0f));
    REQUIRE(c->at(2).im == Approx(2.0f));
}

TEST_CASE("tile repeats an array", "[utils][arrays]") {
    auto v = fa({ 1.0f, 2.0f });
    auto t = tile(v, 3);
    REQUIRE(*t == std::vector<float>{ 1.0f, 2.0f, 1.0f, 2.0f, 1.0f, 2.0f });

    auto c = ca({ { 1.0f, -1.0f } });
    REQUIRE(tile(c, 4)->size() == 4);
}

TEST_CASE("concatenate joins two arrays", "[utils][arrays]") {
    auto a = fa({ 1.0f });
    auto b = fa({ 2.0f, 3.0f });
    REQUIRE(*concatenate(a, b) == std::vector<float>{ 1.0f, 2.0f, 3.0f });

    auto ca1 = ca({ { 1, 1 } });
    auto ca2 = ca({ { 2, 2 }, { 3, 3 } });
    REQUIRE(concatenate(ca1, ca2)->size() == 3);
}

TEST_CASE("resize pads with zeros or truncates", "[utils][arrays]") {
    auto c = ca({ { 1.0f, 1.0f }, { 2.0f, 2.0f } });

    auto bigger = resize(c, 4);
    REQUIRE(bigger->size() == 4);
    REQUIRE(bigger->at(1).re == Approx(2.0f));
    REQUIRE(bigger->at(3).re == Approx(0.0f));

    auto smaller = resize(c, 1);
    REQUIRE(smaller->size() == 1);
    REQUIRE(smaller->at(0).re == Approx(1.0f));

    // Same size returns the same object, not a copy.
    REQUIRE(resize(c, 2) == c);
}

TEST_CASE("clone makes an independent copy", "[utils][arrays]") {
    auto v = fa({ 1.0f, 2.0f });
    auto copy = clone(v);
    REQUIRE(copy != v);
    copy->at(0) = 99.0f;
    REQUIRE(v->at(0) == Approx(1.0f));

    auto c = ca({ { 1.0f, 2.0f } });
    auto cc = clone(c);
    REQUIRE(cc != c);
    cc->at(0).re = 99.0f;
    REQUIRE(c->at(0).re == Approx(1.0f));
}

TEST_CASE("swapfft rotates the two halves", "[utils][arrays]") {
    auto c = ca({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } });
    swapfft(c);
    REQUIRE(c->at(0).re == Approx(2.0f));
    REQUIRE(c->at(1).re == Approx(3.0f));
    REQUIRE(c->at(2).re == Approx(0.0f));
    REQUIRE(c->at(3).re == Approx(1.0f));

    // Applying it twice is the identity.
    swapfft(c);
    REQUIRE(c->at(0).re == Approx(0.0f));
    REQUIRE(c->at(3).re == Approx(3.0f));
}

// ------------------------------------------------------------ complex views

TEST_CASE("tocomplex, npreal and real", "[utils][arrays]") {
    auto v = fa({ 1.0f, -2.0f });
    auto c = tocomplex(v);
    REQUIRE(c->size() == 2);
    REQUIRE(c->at(0).re == Approx(1.0f));
    REQUIRE(c->at(0).im == Approx(0.0f));

    REQUIRE(*npreal(c) == std::vector<float>{ 1.0f, -2.0f });
    REQUIRE(*dsp::arrays::real(c) == std::vector<float>{ 1.0f, -2.0f });
}

TEST_CASE("conj flips the sign of the imaginary part", "[utils][arrays]") {
    auto c = ca({ { 1.0f, 2.0f }, { -3.0f, -4.0f } });
    auto k = conj(c);
    REQUIRE(k->at(0).im == Approx(-2.0f));
    REQUIRE(k->at(1).im == Approx(4.0f));
    // The input is not modified.
    REQUIRE(c->at(0).im == Approx(2.0f));
}

TEST_CASE("npabsolute takes the magnitude", "[utils][arrays]") {
    auto c = ca({ { 3.0f, 4.0f }, { 0.0f, -1.0f } });
    auto m = npabsolute(c);
    REQUIRE(m->at(0) == Approx(5.0f));
    REQUIRE(m->at(1) == Approx(1.0f));
}

TEST_CASE("power handles the complex branch by polar form", "[utils][arrays]") {
    REQUIRE(power(2.0f, 3.0f) == Approx(8.0f));

    // (0 + 1j)^2 == -1.
    auto sq = power(dsp::complex_t{ 0.0f, 1.0f }, 2.0f);
    REQUIRE(sq.re == Approx(-1.0f).margin(1e-5));
    REQUIRE(sq.im == Approx(0.0f).margin(1e-5));
}

// ---------------------------------------------------------------- filtering

TEST_CASE("convolve produces the full linear convolution", "[utils][arrays]") {
    auto a = fa({ 1.0f, 2.0f, 3.0f });
    auto b = fa({ 1.0f, 1.0f });
    auto c = convolve(a, b);

    REQUIRE(c->size() == 4);
    REQUIRE(c->at(0) == Approx(1.0f));
    REQUIRE(c->at(1) == Approx(3.0f));
    REQUIRE(c->at(2) == Approx(5.0f));
    REQUIRE(c->at(3) == Approx(3.0f));
}

TEST_CASE("npmavg returns an array of the same length", "[utils][arrays]") {
    // The implementation aborts the process if the lengths do not match, so
    // this is worth an explicit test rather than relying on it incidentally.
    auto v = std::make_shared<std::vector<float>>(64, 1.0f);
    auto avg = npmavg(v, 8);
    REQUIRE(avg->size() == v->size());
    for (float x : *avg) { REQUIRE(x == Approx(1.0f).margin(1e-5)); }
}

TEST_CASE("centeredSma smooths a constant to itself", "[utils][arrays]") {
    auto v = std::make_shared<std::vector<float>>(64, 3.0f);
    auto sma = centeredSma(v, 8);
    REQUIRE(sma->size() == 64);
    for (float x : *sma) { REQUIRE(x == Approx(3.0f).margin(1e-4)); }
}

TEST_CASE("centeredSma follows a step with a delay of half the window", "[utils][arrays]") {
    auto v = std::make_shared<std::vector<float>>(128, 0.0f);
    for (int i = 64; i < 128; i++) { v->at(i) = 1.0f; }

    auto sma = centeredSma(v, 16);
    // Well before the step it is still zero, well after it is one, and it is
    // monotonically non-decreasing across the transition.
    REQUIRE(sma->at(32) == Approx(0.0f).margin(1e-4));
    REQUIRE(sma->at(100) == Approx(1.0f).margin(1e-4));
    for (int i = 40; i < 100; i++) {
        INFO("index " << i);
        REQUIRE(sma->at(i) >= sma->at(i - 1) - 1e-5f);
    }
}

TEST_CASE("movingVariance is zero for a constant signal", "[utils][arrays]") {
    auto v = std::make_shared<std::vector<float>>(64, 5.0f);
    auto var = movingVariance(v, 8);
    REQUIRE(var->size() == 64);
    for (float x : *var) { REQUIRE(x == Approx(0.0f).margin(1e-4)); }
}

TEST_CASE("movingVariance grows with the signal spread", "[utils][arrays]") {
    auto quiet = std::make_shared<std::vector<float>>(128, 0.0f);
    auto loud = std::make_shared<std::vector<float>>(128, 0.0f);
    for (int i = 0; i < 128; i++) {
        quiet->at(i) = (i % 2) ? 0.1f : -0.1f;
        loud->at(i) = (i % 2) ? 1.0f : -1.0f;
    }

    auto vq = movingVariance(quiet, 8);
    auto vl = movingVariance(loud, 8);
    REQUIRE(vl->at(64) > vq->at(64));
}

// ------------------------------------------------------------------- FFT

TEST_CASE("FFT plan round-trips a delta into a flat spectrum", "[utils][arrays][fft]") {
    const int n = 64;
    auto plan = allocateFFTWPlan(false, n);
    REQUIRE(plan != nullptr);

    auto in = plan->getInput();
    REQUIRE(in->size() == n);
    for (auto& v : *in) { v = { 0.0f, 0.0f }; }
    in->at(0) = { 1.0f, 0.0f };

    plan->execute();

    auto out = plan->getOutput();
    REQUIRE(out->size() == n);
    for (int i = 0; i < n; i++) {
        INFO("bin " << i);
        REQUIRE(out->at(i).re == Approx(1.0f).margin(1e-4));
        REQUIRE(out->at(i).im == Approx(0.0f).margin(1e-4));
    }
}

TEST_CASE("FFT plan puts a single tone in a single bin", "[utils][arrays][fft]") {
    const int n = 64;
    const int bin = 5;
    auto plan = allocateFFTWPlan(false, n);

    const double twoPi = 6.283185307179586;
    auto in = plan->getInput();
    for (int i = 0; i < n; i++) {
        double t = twoPi * bin * i / n;
        in->at(i) = { (float)std::cos(t), (float)std::sin(t) };
    }

    plan->execute();

    auto mag = npabsolute(plan->getOutput());
    REQUIRE(mag->at(bin) == Approx((float)n).epsilon(0.01));
    for (int i = 0; i < n; i++) {
        if (i == bin) { continue; }
        INFO("bin " << i);
        REQUIRE(mag->at(i) < 1.0f);
    }
}

TEST_CASE("large FFT plans produce correct output", "[utils][arrays][fft][threaded]") {
    const int n = 32768;
    auto plan = allocateFFTWPlan(false, n);
    auto in = plan->getInput();
    for (auto& v : *in) { v = { 0.0f, 0.0f }; }
    in->at(0) = { 1.0f, 0.0f };

    plan->execute();

    auto out = plan->getOutput();
    for (int bin : { 0, 1, 127, 4096, n - 1 }) {
        REQUIRE(out->at(bin).re == Approx(1.0f).margin(1e-4));
        REQUIRE(out->at(bin).im == Approx(0.0f).margin(1e-4));
    }
}

TEST_CASE("npfftfft copies the input into the plan", "[utils][arrays][fft]") {
    const int n = 32;
    auto plan = allocateFFTWPlan(false, n);

    auto data = npzeros_c(n);
    data->at(0) = { 2.0f, 0.0f };
    npfftfft(data, plan);

    auto out = plan->getOutput();
    for (int i = 0; i < n; i++) {
        INFO("bin " << i);
        REQUIRE(out->at(i).re == Approx(2.0f).margin(1e-4));
    }
}

TEST_CASE("forward then backward FFT recovers the input up to a scale factor",
          "[utils][arrays][fft]") {
    const int n = 64;
    auto fwd = allocateFFTWPlan(false, n);
    auto bwd = allocateFFTWPlan(true, n);

    auto original = npzeros_c(n);
    for (int i = 0; i < n; i++) {
        original->at(i) = { (float)std::cos(0.3 * i), (float)std::sin(0.11 * i) };
    }

    npfftfft(original, fwd);
    npfftfft(fwd->getOutput(), bwd);

    // The plan normalizes the backward transform itself (it divides by the
    // bucket count), so the round-trip is already unit scaled.
    auto back = bwd->getOutput();
    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(back->at(i).re == Approx(original->at(i).re).margin(1e-4));
        REQUIRE(back->at(i).im == Approx(original->at(i).im).margin(1e-4));
    }
}

// ------------------------------------------------------------- dsp::math

TEST_CASE("dsp::math::sma smooths with the given window", "[utils][arrays][math]") {
    std::vector<float> src(32, 2.0f);
    auto out = dsp::math::sma(4, src);
    REQUIRE(out.size() == src.size());
    for (size_t i = 4; i < out.size(); i++) {
        INFO("index " << i);
        REQUIRE(out[i] == Approx(2.0f).margin(1e-4));
    }
}

TEST_CASE("dsp::math::sma averages partial windows at the beginning",
          "[utils][arrays][math]") {
    std::vector<float> src(8, 2.0f);
    auto out = dsp::math::sma(4, src);
    REQUIRE(out[0] == Approx(2.0f));
    REQUIRE(out[1] == Approx(2.0f));
}

TEST_CASE("dsp::math::maxeach decimates by the window size", "[utils][arrays][math]") {
    // Note this is not a sliding maximum: it emits one value per window, so the
    // output is shorter than the input.
    std::vector<float> src = { 1, 5, 2, 0, 9, 1, 1, 1, 3 };
    auto out = dsp::math::maxeach(3, src);

    REQUIRE(out.size() == 3);
    REQUIRE(out[0] == Approx(5.0f)); // max of {1,5,2}
    REQUIRE(out[1] == Approx(9.0f)); // max of {0,9,1}
    REQUIRE(out[2] == Approx(3.0f)); // max of {1,1,3}
}

TEST_CASE("dsp::math::maxeach flushes a partial trailing window", "[utils][arrays][math]") {
    std::vector<float> src = { 1, 2, 3, 7 };
    auto out = dsp::math::maxeach(3, src);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == Approx(3.0f));
    REQUIRE(out[1] == Approx(7.0f));
}

TEST_CASE("dsp::math::maxeach handles all-negative windows", "[utils][arrays][math]") {
    std::vector<float> src = { -5, -2, -7, -1 };
    auto out = dsp::math::maxeach(3, src);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == Approx(-2.0f));
    REQUIRE(out[1] == Approx(-1.0f));
}

TEST_CASE("dsp::math::sinc matches its definition", "[utils][arrays][math]") {
    // x == 0 short-circuits to 1 regardless of omega and norm.
    REQUIRE(dsp::math::sinc(3.0, 0.0, 2.0) == Approx(1.0));
    // Otherwise it is sin(omega*x) / (norm*x).
    REQUIRE(dsp::math::sinc(1.0, 2.0, 1.0) == Approx(std::sin(2.0) / 2.0));
    REQUIRE(dsp::math::sinc(1.0, 2.0, 4.0) == Approx(std::sin(2.0) / 8.0));
    REQUIRE(std::fabs(dsp::math::sinc(1.0, 10.0, 1.0)) < 1.0);
}

TEST_CASE("dsp::math::linearInterpolateHoles fills runs of zeros", "[utils][arrays][math]") {
    // Zero, not NaN, is the hole marker.
    std::vector<float> data = { 0.0f, 1.0f, 0.0f, 0.0f, 4.0f, 0.0f };
    REQUIRE(dsp::math::linearInterpolateHoles(data.data(), (int)data.size()));

    REQUIRE(data[0] == Approx(1.0f)); // leading hole takes the first real value
    REQUIRE(data[1] == Approx(1.0f));
    REQUIRE(data[2] == Approx(2.0f)); // linear ramp 1 -> 4
    REQUIRE(data[3] == Approx(3.0f));
    REQUIRE(data[4] == Approx(4.0f));
    REQUIRE(data[5] == Approx(4.0f)); // trailing hole holds the last real value
}

TEST_CASE("dsp::math::linearInterpolateHoles reports failure on an all-zero array",
          "[utils][arrays][math]") {
    std::vector<float> data(8, 0.0f);
    REQUIRE_FALSE(dsp::math::linearInterpolateHoles(data.data(), (int)data.size()));
}

TEST_CASE("dsp::math::expn is finite over its useful range", "[utils][arrays][math]") {
    for (float q = 0.01f; q < 50.0f; q *= 2.0f) {
        INFO("q = " << q);
        REQUIRE(std::isfinite(dsp::math::expn(q)));
    }
}

// ------------------------------------------------------------------ dumping

TEST_CASE("dumpArr renders something for each overload", "[utils][arrays][dump]") {
    // These are debugging aids; the test only guards against crashes and empty
    // output, not against the exact formatting.
    auto v = fa({ 1.0f, 2.0f, 3.0f });
    REQUIRE_FALSE(dumpArr(v).empty());

    auto c = ca({ { 1.0f, 2.0f } });
    REQUIRE_FALSE(dumpArr(c).empty());

    REQUIRE_FALSE(dumpArr(v->data(), 2).empty());
    REQUIRE_FALSE(dumpArr(c->data(), 1).empty());

    // sampleArr picks fixed indices out of the array (up to 140), so it needs a
    // long one.
    auto longFloat = linspace(0.0f, 1.0f, 200);
    auto longComplex = tocomplex(longFloat);
    REQUIRE_FALSE(sampleArr(longFloat).empty());
    REQUIRE_FALSE(sampleArr(longComplex).empty());
    REQUIRE_FALSE(ftos(1.5f).empty());
}
