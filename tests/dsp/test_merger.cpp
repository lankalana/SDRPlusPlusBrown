// dsp::routing::Merger: N inputs, one output, lowest priority number wins.
//
// The sink manager uses this to let a plugin (say, a voice keyer) take over the
// audio path from the demodulator. The switching rule is time-based, so the
// tests assert the parts that are deterministic — data gets through, priority
// is honoured, binding is idempotent-checked — and avoid racing the 100 ms
// switch hysteresis.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <vector>

#include <dsp/routing/merger.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace std::chrono_literals;
using namespace sdrpp_test;

TEST_CASE("Merger forwards data from its only input", "[dsp][routing][merger]") {
    dsp::stream<float> src;
    src.setBufferSize(4096);

    dsp::routing::Merger<float> merger;
    merger.bindStream(0, &src);
    merger.getOutput()->setBufferSize(4096);

    StreamCollector<float> collector(merger.getOutput());
    StreamFeeder<float> feeder(&src);

    merger.start();
    REQUIRE(feeder.feed(ramp(512)));
    REQUIRE(collector.waitFor(512));

    auto got = collector.data();
    REQUIRE(got.size() >= 512);
    for (size_t i = 0; i < 512; i++) {
        INFO("sample " << i);
        REQUIRE(got[i] == Approx((float)i));
    }

    collector.stop();
    merger.stop();
}

TEST_CASE("Merger splits long inputs into 1024 sample swaps", "[dsp][routing][merger]") {
    // The copy loop caps each output swap at 1024 samples. Anything larger has
    // to come out in pieces, in order.
    dsp::stream<float> src;
    src.setBufferSize(8192);

    dsp::routing::Merger<float> merger;
    merger.bindStream(0, &src);
    merger.getOutput()->setBufferSize(4096);

    StreamCollector<float> collector(merger.getOutput());
    StreamFeeder<float> feeder(&src);

    merger.start();
    REQUIRE(feeder.feed(ramp(4096)));
    REQUIRE(collector.waitFor(4096));

    for (int c : collector.chunks()) { REQUIRE(c <= 1024); }
    auto got = collector.data();
    for (size_t i = 0; i < 4096; i++) {
        INFO("sample " << i);
        REQUIRE(got[i] == Approx((float)i));
    }

    collector.stop();
    merger.stop();
}

TEST_CASE("Merger prefers the lower priority number", "[dsp][routing][merger]") {
    dsp::stream<float> low, high;
    low.setBufferSize(4096);
    high.setBufferSize(4096);

    dsp::routing::Merger<float> merger;
    merger.bindStream(0, &high); // priority 0 wins
    merger.bindStream(5, &low);
    merger.getOutput()->setBufferSize(4096);

    StreamCollector<float> collector(merger.getOutput());
    StreamFeeder<float> feedHigh(&high);
    StreamFeeder<float> feedLow(&low);

    // Queue both streams before starting the worker so selection is
    // deterministic rather than depending on which feeder thread wins.
    REQUIRE(feedLow.feed(constant(256, -1.0f)));
    REQUIRE(feedHigh.feed(constant(256, 1.0f)));
    merger.start();
    REQUIRE(collector.waitFor(256));
    collector.waitIdle(30ms, 500ms);

    // Everything that came out came from the priority 0 stream; the loser's
    // backlog is dropped rather than interleaved.
    for (float v : collector.data()) { REQUIRE(v == Approx(1.0f)); }

    collector.stop();
    merger.stop();
}

TEST_CASE("Merger falls back to the remaining stream", "[dsp][routing][merger]") {
    dsp::stream<float> a, b;
    a.setBufferSize(4096);
    b.setBufferSize(4096);

    dsp::routing::Merger<float> merger;
    merger.bindStream(0, &a);
    merger.bindStream(5, &b);
    merger.getOutput()->setBufferSize(4096);

    StreamCollector<float> collector(merger.getOutput());
    StreamFeeder<float> feedB(&b);

    merger.start();

    // Only the low priority stream ever produces anything.
    REQUIRE(feedB.feed(constant(128, 7.0f)));
    // The switch hysteresis can hold the choice for up to SWITCH_DELAY, so keep
    // feeding until something arrives.
    for (int i = 0; i < 20 && collector.size() == 0; i++) {
        std::this_thread::sleep_for(20ms);
        REQUIRE(feedB.feed(constant(128, 7.0f)));
    }

    REQUIRE(collector.size() > 0);
    for (float v : collector.data()) { REQUIRE(v == Approx(7.0f)); }

    collector.stop();
    merger.stop();
}

TEST_CASE("Merger rejects binding the same stream twice", "[dsp][routing][merger]") {
    dsp::stream<float> src;
    src.setBufferSize(256);

    dsp::routing::Merger<float> merger;
    merger.bindStream(0, &src);
    REQUIRE_THROWS_AS(merger.bindStream(1, &src), std::runtime_error);

    merger.stop();
    src.stopReader();
}

TEST_CASE("Merger unbindStream removes an input", "[dsp][routing][merger]") {
    dsp::stream<float> a, b;
    a.setBufferSize(1024);
    b.setBufferSize(1024);

    dsp::routing::Merger<float> merger;
    merger.bindStream(0, &a);
    merger.bindStream(1, &b);
    REQUIRE(merger.secondaryStreams.size() == 2);

    merger.unbindStream(&a);
    REQUIRE(merger.secondaryStreams.size() == 1);

    // The same stream can be bound again afterwards.
    a.clearReadStop();
    REQUIRE_NOTHROW(merger.bindStream(2, &a));
    REQUIRE(merger.secondaryStreams.size() == 2);

    merger.stop();
}

TEST_CASE("Merger stop is idempotent", "[dsp][routing][merger]") {
    dsp::stream<float> src;
    src.setBufferSize(1024);

    dsp::routing::Merger<float> merger;
    merger.bindStream(0, &src);
    merger.getOutput()->setBufferSize(1024);

    merger.start();
    merger.stop();
    REQUIRE_NOTHROW(merger.stop());
}
