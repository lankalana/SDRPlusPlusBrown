// Fan-out / fan-in blocks. The rest of the DSP framework is linear; these are
// the only places where the graph branches, so their bind/unbind bookkeeping
// matters as much as the data they copy.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <vector>

#include <dsp/routing/doubler.h>
#include <dsp/routing/splitter.h>
#include <dsp/routing/stream_link.h>

#include "support/dsp_test_helpers.h"

using namespace std::chrono_literals;
using namespace sdrpp_test;

TEST_CASE("Splitter copies the input to every bound stream", "[dsp][routing][splitter]") {
    dsp::stream<float> in, a, b;
    in.setBufferSize(256);
    a.setBufferSize(256);
    b.setBufferSize(256);

    dsp::routing::Splitter<float> splitter;
    splitter.init(&in);
    splitter.bindStream(&a);
    splitter.bindStream(&b);

    StreamCollector<float> ca(&a);
    StreamCollector<float> cb(&b);
    StreamFeeder<float> feeder(&in);

    splitter.start();
    auto data = ramp(64);
    REQUIRE(feeder.feed(data));

    REQUIRE(ca.waitFor(64));
    REQUIRE(cb.waitFor(64));
    REQUIRE(ca.data() == data);
    REQUIRE(cb.data() == data);

    splitter.stop();
    ca.stop();
    cb.stop();
}

TEST_CASE("Splitter with no bound stream just consumes input", "[dsp][routing][splitter]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    dsp::routing::Splitter<float> splitter;
    splitter.init(&in);

    StreamFeeder<float> feeder(&in);
    splitter.start();

    // Two feeds in a row prove the splitter is flushing the input.
    REQUIRE(feeder.feed(constant(8, 1.0f)));
    REQUIRE(feeder.feed(constant(8, 1.0f)));

    splitter.stop();
}

TEST_CASE("Splitter rejects double bind and unknown unbind", "[dsp][routing][splitter]") {
    dsp::stream<float> in, a, b;
    in.setBufferSize(64);
    a.setBufferSize(64);
    b.setBufferSize(64);

    dsp::routing::Splitter<float> splitter;
    splitter.init(&in);
    splitter.bindStream(&a);

    REQUIRE_THROWS_AS(splitter.bindStream(&a), std::runtime_error);
    REQUIRE_THROWS_AS(splitter.unbindStream(&b), std::runtime_error);

    splitter.unbindStream(&a);
    REQUIRE_THROWS_AS(splitter.unbindStream(&a), std::runtime_error);
}

TEST_CASE("Splitter unbind stops delivering to that stream", "[dsp][routing][splitter]") {
    dsp::stream<float> in, a, b;
    in.setBufferSize(256);
    a.setBufferSize(256);
    b.setBufferSize(256);

    dsp::routing::Splitter<float> splitter;
    splitter.init(&in);
    splitter.bindStream(&a);
    splitter.bindStream(&b);

    StreamCollector<float> ca(&a);
    StreamCollector<float> cb(&b);
    StreamFeeder<float> feeder(&in);

    splitter.start();
    REQUIRE(feeder.feed(constant(8, 1.0f)));
    REQUIRE(ca.waitFor(8));
    REQUIRE(cb.waitFor(8));

    splitter.unbindStream(&a);

    REQUIRE(feeder.feed(constant(8, 2.0f)));
    REQUIRE(cb.waitFor(16));
    ca.waitIdle(20ms, 200ms);
    REQUIRE(ca.size() == 8);

    splitter.stop();
    ca.stop();
    cb.stop();
}

TEST_CASE("Splitter hook sees the data", "[dsp][routing][splitter]") {
    dsp::stream<float> in, a;
    in.setBufferSize(256);
    a.setBufferSize(256);

    dsp::routing::Splitter<float> splitter;
    splitter.init(&in);
    splitter.bindStream(&a);

    std::atomic<int> hookSamples{ 0 };
    splitter.setHook([&](float*, int n) { hookSamples += n; });

    StreamCollector<float> ca(&a);
    StreamFeeder<float> feeder(&in);

    splitter.start();
    REQUIRE(feeder.feed(constant(32, 1.0f)));
    REQUIRE(ca.waitFor(32));

    splitter.stop();
    ca.stop();

    REQUIRE(hookSamples.load() == 32);
}

TEST_CASE("Splitter hasInput reflects its wiring", "[dsp][routing][splitter]") {
    dsp::stream<float> in;
    in.setBufferSize(64);

    dsp::routing::Splitter<float> splitter;
    splitter.init(&in);
    REQUIRE(splitter.hasInput());

    splitter.setInput(nullptr);
    REQUIRE_FALSE(splitter.hasInput());
}

TEST_CASE("Splitter counts the samples it forwarded", "[dsp][routing][splitter]") {
    dsp::stream<float> in, a;
    in.setBufferSize(256);
    a.setBufferSize(256);

    dsp::routing::Splitter<float> splitter;
    splitter.init(&in);
    splitter.bindStream(&a);

    StreamCollector<float> ca(&a);
    StreamFeeder<float> feeder(&in);

    REQUIRE(splitter.workedCount == 0);
    splitter.start();
    REQUIRE(feeder.feed(constant(20, 1.0f)));
    REQUIRE(ca.waitFor(20));
    splitter.stop();
    ca.stop();

    REQUIRE(splitter.workedCount == 20);
}

TEST_CASE("Doubler feeds both of its outputs", "[dsp][routing][doubler]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    dsp::routing::Doubler<float> doubler;
    doubler.init(&in);
    doubler.outA.setBufferSize(256);
    doubler.outB.setBufferSize(256);

    StreamCollector<float> ca(&doubler.outA);
    StreamCollector<float> cb(&doubler.outB);
    StreamFeeder<float> feeder(&in);

    doubler.start();
    auto data = ramp(32);
    REQUIRE(feeder.feed(data));
    REQUIRE(ca.waitFor(32));
    REQUIRE(cb.waitFor(32));

    REQUIRE(ca.data() == data);
    REQUIRE(cb.data() == data);

    doubler.stop();
    ca.stop();
    cb.stop();
}

TEST_CASE("StreamLink forwards to its target stream", "[dsp][routing][link]") {
    dsp::stream<float> in, out;
    in.setBufferSize(256);
    out.setBufferSize(256);

    dsp::routing::StreamLink<float> link;
    link.init(&in, &out);

    StreamCollector<float> collector(&out);
    StreamFeeder<float> feeder(&in);

    link.start();
    auto data = ramp(16);
    REQUIRE(feeder.feed(data));
    REQUIRE(collector.waitFor(16));
    REQUIRE(collector.data() == data);

    link.stop();
    collector.stop();
}

TEST_CASE("StreamLink setOutput retargets a running link", "[dsp][routing][link]") {
    dsp::stream<float> in, outA, outB;
    in.setBufferSize(256);
    outA.setBufferSize(256);
    outB.setBufferSize(256);

    dsp::routing::StreamLink<float> link;
    link.init(&in, &outA);

    StreamCollector<float> ca(&outA);
    StreamCollector<float> cb(&outB);
    StreamFeeder<float> feeder(&in);

    link.start();
    REQUIRE(feeder.feed(constant(8, 1.0f)));
    REQUIRE(ca.waitFor(8));

    link.setOutput(&outB);
    REQUIRE(feeder.feed(constant(8, 2.0f)));
    REQUIRE(cb.waitFor(8));

    REQUIRE(ca.size() == 8);
    REQUIRE(cb.data()[0] == 2.0f);

    link.stop();
    ca.stop();
    cb.stop();
}

TEST_CASE("Splitter carries complex samples", "[dsp][routing][splitter]") {
    dsp::stream<dsp::complex_t> in, a;
    in.setBufferSize(256);
    a.setBufferSize(256);

    dsp::routing::Splitter<dsp::complex_t> splitter;
    splitter.init(&in);
    splitter.bindStream(&a);

    StreamCollector<dsp::complex_t> ca(&a);
    StreamFeeder<dsp::complex_t> feeder(&in);

    splitter.start();
    auto data = complexTone(64, 1000.0, 48000.0);
    REQUIRE(feeder.feed(data));
    REQUIRE(ca.waitFor(64));

    auto got = ca.data();
    for (size_t i = 0; i < data.size(); i++) {
        REQUIRE(got[i].re == data[i].re);
        REQUIRE(got[i].im == data[i].im);
    }

    splitter.stop();
    ca.stop();
}
