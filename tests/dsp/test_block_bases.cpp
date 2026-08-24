// The block base classes that the rest of the framework is built from:
// dsp::hier_block, dsp::Operator and dsp::Source.
//
// dsp::block and dsp::Processor are covered in test_processor.cpp; this file
// covers the three bases that have no tests anywhere else. They have no
// concrete implementations in core/src, only in plugins, so the tests define
// their own minimal subclasses.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <vector>

#include <dsp/hier_block.h>
#include <dsp/operator.h>
#include <dsp/processor.h>
#include <dsp/source.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace std::chrono_literals;
using namespace sdrpp_test;

namespace {
    // A leaf that just records how many times it was started and stopped.
    class CountingBlock : public dsp::generic_block {
    public:
        void start() override { starts++; }
        void stop() override { stops++; }
        int run() override { return -1; }

        int starts = 0;
        int stops = 0;
    };

    class TestHier : public dsp::hier_block {
    public:
        void add(dsp::generic_block* b) { registerBlock(b); }
        void remove(dsp::generic_block* b) { unregisterBlock(b); }
        void markInit() { _block_init = true; }
    };

    // Sums two streams sample by sample, consuming the same count from each.
    class Adder : public dsp::Operator<float, float, float> {
        using base_type = dsp::Operator<float, float, float>;

    public:
        int run() override {
            int ca = _a->read();
            if (ca < 0) { return -1; }
            int cb = _b->read();
            if (cb < 0) {
                _a->flush();
                return -1;
            }
            int n = (std::min)(ca, cb);
            for (int i = 0; i < n; i++) { out.writeBuf[i] = _a->readBuf[i] + _b->readBuf[i]; }
            _a->flush();
            _b->flush();
            if (!out.swap(n)) { return -1; }
            return n;
        }
    };

    // Emits a fixed number of ascending samples, then ends.
    class Counter : public dsp::Source<float> {
    public:
        int run() override {
            if (emitted >= limit) { return -1; }
            const int n = (std::min)(64, limit - emitted);
            for (int i = 0; i < n; i++) { out.writeBuf[i] = (float)(emitted + i); }
            emitted += n;
            if (!out.swap(n)) { return -1; }
            return n;
        }

        int limit = 256;
        int emitted = 0;
    };
}

// ------------------------------------------------------------- hier_block

TEST_CASE("hier_block start and stop propagate to its children", "[dsp][hier]") {
    CountingBlock a, b;
    TestHier hier;
    hier.markInit();
    hier.add(&a);
    hier.add(&b);

    hier.start();
    REQUIRE(a.starts == 1);
    REQUIRE(b.starts == 1);

    hier.stop();
    REQUIRE(a.stops == 1);
    REQUIRE(b.stops == 1);
}

TEST_CASE("hier_block start and stop are idempotent", "[dsp][hier]") {
    CountingBlock a;
    TestHier hier;
    hier.markInit();
    hier.add(&a);

    hier.start();
    hier.start();
    REQUIRE(a.starts == 1);

    hier.stop();
    hier.stop();
    REQUIRE(a.stops == 1);
}

TEST_CASE("hier_block tempStop nests", "[dsp][hier]") {
    // Same contract as dsp::block: only the outermost tempStop/tempStart pair
    // actually touches the children.
    CountingBlock a;
    TestHier hier;
    hier.markInit();
    hier.add(&a);

    hier.start();
    REQUIRE(a.starts == 1);

    hier.tempStop();
    hier.tempStop();
    REQUIRE(a.stops == 1); // only the outer one stopped anything

    hier.tempStart();
    REQUIRE(a.starts == 1); // inner: still nested, nothing restarted
    hier.tempStart();
    REQUIRE(a.starts == 2);

    hier.stop();
}

TEST_CASE("hier_block tempStop on a stopped hierarchy does nothing", "[dsp][hier]") {
    CountingBlock a;
    TestHier hier;
    hier.markInit();
    hier.add(&a);

    hier.tempStop();
    hier.tempStart();
    REQUIRE(a.starts == 0);
    REQUIRE(a.stops == 0);
}

TEST_CASE("hier_block unregisterBlock detaches a child", "[dsp][hier]") {
    CountingBlock a, b;
    TestHier hier;
    hier.markInit();
    hier.add(&a);
    hier.add(&b);

    hier.remove(&a);
    hier.start();
    REQUIRE(a.starts == 0);
    REQUIRE(b.starts == 1);
    hier.stop();
}

// --------------------------------------------------------------- Operator

TEST_CASE("Operator combines two input streams", "[dsp][operator]") {
    dsp::stream<float> a, b;
    a.setBufferSize(1024);
    b.setBufferSize(1024);

    Adder op;
    op.init(&a, &b);
    op.out.setBufferSize(1024);

    StreamCollector<float> collector(&op.out);
    StreamFeeder<float> feedA(&a);
    StreamFeeder<float> feedB(&b);

    op.start();
    REQUIRE(feedA.feed(constant(64, 1.0f)));
    REQUIRE(feedB.feed(constant(64, 2.0f)));
    REQUIRE(collector.waitFor(64));

    for (float v : collector.data()) { REQUIRE(v == Approx(3.0f)); }

    op.stop();
    collector.stop();
}

TEST_CASE("Operator setInputA rewires one side", "[dsp][operator]") {
    dsp::stream<float> a, b, a2;
    a.setBufferSize(1024);
    b.setBufferSize(1024);
    a2.setBufferSize(1024);

    Adder op;
    op.init(&a, &b);
    op.out.setBufferSize(1024);

    StreamCollector<float> collector(&op.out);
    op.start();

    {
        StreamFeeder<float> feedA(&a);
        StreamFeeder<float> feedB(&b);
        REQUIRE(feedA.feed(constant(32, 1.0f)));
        REQUIRE(feedB.feed(constant(32, 1.0f)));
        REQUIRE(collector.waitFor(32));
    }

    op.setInputA(&a2);
    {
        StreamFeeder<float> feedA(&a2);
        StreamFeeder<float> feedB(&b);
        REQUIRE(feedA.feed(constant(32, 10.0f)));
        REQUIRE(feedB.feed(constant(32, 1.0f)));
        REQUIRE(collector.waitFor(64));
    }
    REQUIRE(collector.data().back() == Approx(11.0f));

    op.stop();
    collector.stop();
}

TEST_CASE("Operator setInputs rewires both sides", "[dsp][operator]") {
    dsp::stream<float> a, b, a2, b2;
    for (auto* s : { &a, &b, &a2, &b2 }) { s->setBufferSize(1024); }

    Adder op;
    op.init(&a, &b);
    op.out.setBufferSize(1024);

    StreamCollector<float> collector(&op.out);
    op.start();
    op.setInputs(&a2, &b2);

    StreamFeeder<float> feedA(&a2);
    StreamFeeder<float> feedB(&b2);
    REQUIRE(feedA.feed(constant(16, 4.0f)));
    REQUIRE(feedB.feed(constant(16, 5.0f)));
    REQUIRE(collector.waitFor(16));
    REQUIRE(collector.data().back() == Approx(9.0f));

    op.stop();
    collector.stop();
}

TEST_CASE("Operator stops cleanly when one input is stopped", "[dsp][operator]") {
    dsp::stream<float> a, b;
    a.setBufferSize(1024);
    b.setBufferSize(1024);

    Adder op;
    op.init(&a, &b);
    op.out.setBufferSize(1024);

    StreamCollector<float> collector(&op.out);
    op.start();
    REQUIRE_NOTHROW(op.stop());
    collector.stop();
}

// ----------------------------------------------------------------- Source

TEST_CASE("Source produces data on its output stream", "[dsp][source]") {
    Counter src;
    src.out.setBufferSize(1024);
    src.limit = 256;

    StreamCollector<float> collector(&src.out);
    src.start();

    REQUIRE(collector.waitFor(256));
    auto got = collector.data();
    REQUIRE(got.size() >= 256);
    for (size_t i = 0; i < 256; i++) {
        INFO("sample " << i);
        REQUIRE(got[i] == Approx((float)i));
    }

    src.stop();
    collector.stop();
}

TEST_CASE("Source stops when its run returns negative", "[dsp][source]") {
    // The worker loop exits on a negative run(); stop() must then still be safe.
    Counter src;
    src.out.setBufferSize(1024);
    src.limit = 64;

    StreamCollector<float> collector(&src.out);
    src.start();
    REQUIRE(collector.waitFor(64));

    std::this_thread::sleep_for(50ms);
    REQUIRE(src.emitted == 64);
    REQUIRE_NOTHROW(src.stop());
    collector.stop();
}
