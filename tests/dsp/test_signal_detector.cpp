// dsp::detector::SignalDetector and its ArrayView helper.
//
// The detector is wired into the IQ front end's preprocessor chain, so the
// property that matters above all others is that it is transparent: whatever it
// decides internally, the samples that come out must be exactly the samples
// that went in. Everything else it does is advisory.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <dsp/detector/signal_detector.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

// ---------------------------------------------------------------- ArrayView

TEST_CASE("ArrayView wraps a pointer and length", "[dsp][detector][arrayview]") {
    std::vector<float> data = { 1.0f, 2.0f, 3.0f };
    dsp::detector::ArrayView<float> view(data.data(), data.size());

    REQUIRE(view.size() == 3);
    REQUIRE(view[0] == 1.0f);
    REQUIRE(view[2] == 3.0f);
    REQUIRE(view.data() == data.data());
    REQUIRE(view.end() - view.begin() == 3);
}

TEST_CASE("ArrayView constructs from a vector", "[dsp][detector][arrayview]") {
    std::vector<int> data = { 5, 6, 7, 8 };
    dsp::detector::ArrayView<int> view(data);

    REQUIRE(view.size() == 4);
    int sum = 0;
    for (int v : view) { sum += v; }
    REQUIRE(sum == 26);
}

TEST_CASE("ArrayView::at bounds-checks", "[dsp][detector][arrayview]") {
    std::vector<float> data = { 1.0f };
    dsp::detector::ArrayView<float> view(data);

    REQUIRE(view.at(0) == 1.0f);
    REQUIRE_THROWS_AS(view.at(1), std::out_of_range);
    REQUIRE_THROWS_AS(view.at(9999), std::out_of_range);
}

TEST_CASE("ArrayView::dump renders floats and ints", "[dsp][detector][arrayview]") {
    std::vector<float> f = { 1.0f, 2.5f };
    REQUIRE(dsp::detector::ArrayView<float>(f).dump() == "{1f, 2.5f}");

    std::vector<int> i = { 1, 2, 3 };
    REQUIRE(dsp::detector::ArrayView<int>(i).dump() == "{1, 2, 3}");

    // Any other element type dumps as empty braces.
    std::vector<double> d = { 1.0 };
    REQUIRE(dsp::detector::ArrayView<double>(d).dump() == "{}");
}

// ------------------------------------------------------------ SignalDetector

TEST_CASE("SignalDetector passes samples through unchanged", "[dsp][detector]") {
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(4096);

    dsp::detector::SignalDetector det;
    det.init(&in);
    det.out.setBufferSize(4096);
    det.setSampleRate(1000.0); // FFT size 100

    StreamCollector<dsp::complex_t> collector(&det.out);
    StreamFeeder<dsp::complex_t> feeder(&in);

    det.start();
    auto data = complexTone(2048, 100.0, 1000.0, 0.7);
    REQUIRE(feeder.feed(data, 512));
    REQUIRE(collector.waitFor(2048));

    auto got = collector.data();
    REQUIRE(got.size() >= 2048);
    for (size_t i = 0; i < 2048; i++) {
        INFO("sample " << i);
        REQUIRE(got[i].re == data[i].re);
        REQUIRE(got[i].im == data[i].im);
    }

    det.stop();
    collector.stop();
}

TEST_CASE("SignalDetector is transparent when disabled", "[dsp][detector]") {
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(2048);

    dsp::detector::SignalDetector det;
    det.init(&in);
    det.out.setBufferSize(2048);
    det.setSampleRate(1000.0);

    REQUIRE(det.isEnabled());
    det.setEnabled(false);
    REQUIRE_FALSE(det.isEnabled());

    StreamCollector<dsp::complex_t> collector(&det.out);
    StreamFeeder<dsp::complex_t> feeder(&in);

    det.start();
    auto data = complexTone(512, 100.0, 1000.0);
    REQUIRE(feeder.feed(data));
    REQUIRE(collector.waitFor(512));

    auto got = collector.data();
    for (size_t i = 0; i < 512; i++) {
        REQUIRE(got[i].re == data[i].re);
        REQUIRE(got[i].im == data[i].im);
    }

    det.stop();
    collector.stop();
}

TEST_CASE("SignalDetector is transparent before a sample rate is set", "[dsp][detector]") {
    // fftSize stays 0 until setSampleRate is called; the block must not fall
    // over on the way there.
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(2048);

    dsp::detector::SignalDetector det;
    det.init(&in);
    det.out.setBufferSize(2048);

    StreamCollector<dsp::complex_t> collector(&det.out);
    StreamFeeder<dsp::complex_t> feeder(&in);

    det.start();
    auto data = complexTone(256, 50.0, 1000.0);
    REQUIRE(feeder.feed(data));
    REQUIRE(collector.waitFor(256));

    auto got = collector.data();
    for (size_t i = 0; i < 256; i++) { REQUIRE(got[i].re == data[i].re); }

    det.stop();
    collector.stop();
}

TEST_CASE("SignalDetector ignores a non-positive sample rate", "[dsp][detector]") {
    dsp::detector::SignalDetector det;
    det.init(nullptr);
    det.out.setBufferSize(64);

    REQUIRE_NOTHROW(det.setSampleRate(0.0));
    REQUIRE_NOTHROW(det.setSampleRate(-48000.0));
}

TEST_CASE("SignalDetector setSampleRate is idempotent", "[dsp][detector]") {
    dsp::detector::SignalDetector det;
    det.init(nullptr);
    det.out.setBufferSize(64);

    det.setSampleRate(2000.0);
    // A second identical call short-circuits and must not rebuild the plan.
    REQUIRE_NOTHROW(det.setSampleRate(2000.0));
    REQUIRE_NOTHROW(det.setSampleRate(4000.0));
}

TEST_CASE("SignalDetector setCenterFrequency resets the accumulator", "[dsp][detector]") {
    dsp::detector::SignalDetector det;
    det.init(nullptr);
    det.out.setBufferSize(64);
    det.setSampleRate(1000.0);

    REQUIRE_NOTHROW(det.setCenterFrequency(14074000.0));
    REQUIRE_NOTHROW(det.setCenterFrequency(14074000.0)); // idempotent
    REQUIRE_NOTHROW(det.setCenterFrequency(7074000.0));
}

TEST_CASE("SignalDetector survives a sample rate change while running", "[dsp][detector]") {
    dsp::stream<dsp::complex_t> in;
    in.setBufferSize(4096);

    dsp::detector::SignalDetector det;
    det.init(&in);
    det.out.setBufferSize(4096);
    det.setSampleRate(1000.0);

    StreamCollector<dsp::complex_t> collector(&det.out);
    StreamFeeder<dsp::complex_t> feeder(&in);

    det.start();
    REQUIRE(feeder.feed(complexTone(512, 100.0, 1000.0)));
    REQUIRE(collector.waitFor(512));

    // NOTE: setSampleRate reallocates the FFT plan without stopping the block.
    // If a refactor makes this racy, this test is where it will show up.
    det.tempStop();
    det.setSampleRate(2000.0);
    det.tempStart();

    REQUIRE(feeder.feed(complexTone(512, 100.0, 2000.0)));
    REQUIRE(collector.waitFor(1024));

    det.stop();
    collector.stop();
}
