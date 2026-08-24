// dsp::block / dsp::Processor lifecycle: start/stop, tempStop nesting, input
// rewiring and the bounded-run helper that protects the output buffer.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <dsp/processor.h>
#include <dsp/sink.h>

#include "support/dsp_test_helpers.h"

using namespace std::chrono_literals;
using namespace sdrpp_test;

namespace {
    // Minimal processor: multiplies by a factor and counts start/stop calls.
    class Scaler : public dsp::Processor<float, float> {
        using base_type = dsp::Processor<float, float>;

    public:
        Scaler() {}

        void init(dsp::stream<float>* in, float factor) {
            _factor = factor;
            base_type::init(in);
        }

        int process(int count, const float* in, float* out) {
            for (int i = 0; i < count; i++) { out[i] = in[i] * _factor; }
            processCalls++;
            return count;
        }

        DEFAULT_PROC_RUN

        std::atomic<int> processCalls{ 0 };
        float _factor = 1.0f;
    };

    // Processor whose output/input ratio would overflow a small output buffer
    // unless runBounded() splits the work.
    class Doubler2x : public dsp::Processor<float, float> {
        using base_type = dsp::Processor<float, float>;

    public:
        Doubler2x() {}
        void init(dsp::stream<float>* in) { base_type::init(in); }

        int process(int count, const float* in, float* out) {
            for (int i = 0; i < count; i++) {
                out[2 * i] = in[i];
                out[2 * i + 1] = in[i];
            }
            return count * 2;
        }

        int run() override {
            return dsp::runBounded(base_type::_in, base_type::out,
                                   [this]() { return base_type::out.getBufferSize() / 2; },
                                   [this](int count, const float* in, float* out) { return process(count, in, out); });
        }
    };

#ifdef NDEBUG
    // Deliberately misbehaving processor: claims it can take more input than
    // its output buffer can hold. runBounded() must reject that. Only built
    // with NDEBUG, since runBounded() asserts before returning the error.
    class Overflowing : public dsp::Processor<float, float> {
        using base_type = dsp::Processor<float, float>;

    public:
        void init(dsp::stream<float>* in) { base_type::init(in); }

        int run() override {
            return dsp::runBounded(base_type::_in, base_type::out,
                                   [this]() { return base_type::out.getBufferSize(); },
                                   [this](int count, const float*, float*) { return count * 4; });
        }
    };
#endif
}

TEST_CASE("processor forwards processed data downstream", "[dsp][processor]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Scaler scaler;
    scaler.init(&in, 2.0f);
    scaler.out.setBufferSize(256);

    StreamCollector<float> collector(&scaler.out);
    StreamFeeder<float> feeder(&in);

    scaler.start();
    REQUIRE(feeder.feed(ramp(64)));
    REQUIRE(collector.waitFor(64));

    auto data = collector.data();
    REQUIRE(data.size() == 64);
    for (int i = 0; i < 64; i++) { REQUIRE(data[i] == (float)i * 2.0f); }

    scaler.stop();
    collector.stop();
}

TEST_CASE("processor start and stop are idempotent", "[dsp][processor]") {
    dsp::stream<float> in;
    in.setBufferSize(64);

    Scaler scaler;
    scaler.init(&in, 1.0f);
    scaler.out.setBufferSize(64);

    scaler.start();
    scaler.start(); // no second worker thread
    scaler.stop();
    scaler.stop();  // no double join

    SUCCEED("start/stop did not deadlock or crash");
}

TEST_CASE("processor tempStop nests and restores the running state", "[dsp][processor]") {
    dsp::stream<float> in;
    in.setBufferSize(64);

    Scaler scaler;
    scaler.init(&in, 1.0f);
    scaler.out.setBufferSize(64);

    StreamCollector<float> collector(&scaler.out);
    StreamFeeder<float> feeder(&in);

    scaler.start();

    scaler.tempStop();
    scaler.tempStop();
    scaler.tempStart();
    // Still inside the outer tempStop: the block must remain stopped.
    scaler.tempStart();

    // Data flows again, so the outer tempStart really restarted the worker.
    REQUIRE(feeder.feed(constant(8, 3.0f)));
    REQUIRE(collector.waitFor(8));
    REQUIRE(collector.data().size() == 8);

    scaler.stop();
    collector.stop();
}

TEST_CASE("processor tempStop on a stopped block does not start it", "[dsp][processor]") {
    dsp::stream<float> in;
    in.setBufferSize(64);

    Scaler scaler;
    scaler.init(&in, 1.0f);
    scaler.out.setBufferSize(64);

    scaler.tempStop();
    scaler.tempStart();

    StreamCollector<float> collector(&scaler.out);
    StreamFeeder<float> feeder(&in);

    // The block was never started, so nothing should be produced. feed() itself
    // succeeds because the first swap always fits in the free buffer.
    REQUIRE(feeder.feed(constant(4, 1.0f)));
    REQUIRE_FALSE(collector.waitFor(1, 100ms));

    collector.stop();
}

TEST_CASE("processor setInput rewires a running block", "[dsp][processor]") {
    dsp::stream<float> inA, inB;
    inA.setBufferSize(64);
    inB.setBufferSize(64);

    Scaler scaler;
    scaler.init(&inA, 1.0f);
    scaler.out.setBufferSize(64);

    StreamCollector<float> collector(&scaler.out);
    scaler.start();

    StreamFeeder<float> feederA(&inA);
    REQUIRE(feederA.feed(constant(4, 1.0f)));
    REQUIRE(collector.waitFor(4));

    scaler.setInput(&inB);

    StreamFeeder<float> feederB(&inB);
    REQUIRE(feederB.feed(constant(4, 2.0f)));
    REQUIRE(collector.waitFor(8));

    auto data = collector.data();
    REQUIRE(data.size() == 8);
    REQUIRE(data[0] == 1.0f);
    REQUIRE(data[7] == 2.0f);

    scaler.stop();
    collector.stop();
}

TEST_CASE("TempStopGuard stops on entry and restarts on exit", "[dsp][processor]") {
    dsp::stream<float> in;
    in.setBufferSize(64);

    Scaler scaler;
    scaler.init(&in, 1.0f);
    scaler.out.setBufferSize(64);

    StreamCollector<float> collector(&scaler.out);
    StreamFeeder<float> feeder(&in);

    scaler.start();
    {
        dsp::TempStopGuard<Scaler> guard(scaler);
    }
    REQUIRE(feeder.feed(constant(4, 5.0f)));
    REQUIRE(collector.waitFor(4));

    scaler.stop();
    collector.stop();
}

TEST_CASE("runBounded splits work that would overflow the output buffer", "[dsp][processor][bounded]") {
    dsp::stream<float> in;
    in.setBufferSize(1024);

    Doubler2x block;
    block.init(&in);
    block.out.setBufferSize(64); // holds only 32 input samples worth of output

    StreamCollector<float> collector(&block.out);
    StreamFeeder<float> feeder(&in);

    block.start();
    REQUIRE(feeder.feed(ramp(256)));
    REQUIRE(collector.waitFor(512));

    auto data = collector.data();
    REQUIRE(data.size() == 512);
    for (int i = 0; i < 256; i++) {
        REQUIRE(data[2 * i] == (float)i);
        REQUIRE(data[2 * i + 1] == (float)i);
    }

    // Every swap must have fitted in the output buffer.
    for (int chunk : collector.chunks()) { REQUIRE(chunk <= 64); }

    block.stop();
    collector.stop();
}

#ifdef NDEBUG
TEST_CASE("runBounded refuses to write past the output buffer", "[dsp][processor][bounded]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Overflowing block;
    block.init(&in);
    block.out.setBufferSize(16);

    StreamFeeder<float> feeder(&in);
    REQUIRE(feeder.feed(constant(8, 1.0f)));

    // run() detects the over-production and reports failure instead of
    // corrupting memory.
    REQUIRE(block.run() == -1);
}
#endif
