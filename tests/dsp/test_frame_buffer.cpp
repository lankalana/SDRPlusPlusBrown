// dsp::buffer::SampleFrameBuffer and dsp::buffer::Prebuffer.
//
// SampleFrameBuffer sits at the very top of the IQ front end and decouples the
// source thread from the DSP thread. Its own header says it "IS TRASH AND MUST
// BE REWRITTEN", which is exactly why it needs a test before anyone does.
//
// Sizing note: SampleFrameBuffer allocates TEST_BUFFER_SIZE (32) buffers of
// STREAM_BUFFER_SIZE samples each, unconditionally and regardless of how much
// data actually flows: 128 MB for float, 256 MB for complex_t. The tests use
// float and keep one instance alive at a time. A rewrite should make this
// proportional to the configured latency; if it does, this comment is stale.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

#include <dsp/buffer/frame_buffer.h>
#include <dsp/buffer/prebuffer.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace std::chrono_literals;
using namespace sdrpp_test;

// -------------------------------------------------------- SampleFrameBuffer

TEST_CASE("SampleFrameBuffer forwards data unchanged", "[dsp][buffer][frame]") {
    dsp::stream<float> in;
    in.setBufferSize(4096);

    dsp::buffer::SampleFrameBuffer<float> fb;
    fb.init(&in);
    fb.out.setBufferSize(4096);

    StreamCollector<float> collector(&fb.out);
    StreamFeeder<float> feeder(&in);

    fb.start();
    auto data = ramp(1024);
    REQUIRE(feeder.feed(data, 256));
    REQUIRE(collector.waitFor(1024));

    auto got = collector.data();
    REQUIRE(got.size() >= 1024);
    for (size_t i = 0; i < 1024; i++) {
        INFO("sample " << i);
        REQUIRE(got[i] == Approx(data[i]));
    }

    fb.stop();
    collector.stop();
}

TEST_CASE("SampleFrameBuffer preserves chunk boundaries", "[dsp][buffer][frame]") {
    // Each input swap becomes exactly one output swap: the block buffers whole
    // frames, it does not repack them.
    dsp::stream<float> in;
    in.setBufferSize(4096);

    dsp::buffer::SampleFrameBuffer<float> fb;
    fb.init(&in);
    fb.out.setBufferSize(4096);

    StreamCollector<float> collector(&fb.out);
    StreamFeeder<float> feeder(&in);

    fb.start();
    for (int i = 0; i < 5; i++) { REQUIRE(feeder.feed(constant(100, (float)i))); }
    REQUIRE(collector.waitFor(500));
    collector.waitIdle(20ms, 300ms);

    for (int c : collector.chunks()) { REQUIRE(c == 100); }

    fb.stop();
    collector.stop();
}

TEST_CASE("SampleFrameBuffer bypass mode still forwards data", "[dsp][buffer][frame]") {
    dsp::stream<float> in;
    in.setBufferSize(4096);

    dsp::buffer::SampleFrameBuffer<float> fb;
    fb.init(&in);
    fb.out.setBufferSize(4096);
    fb.bypass = true;

    StreamCollector<float> collector(&fb.out);
    StreamFeeder<float> feeder(&in);

    fb.start();
    auto data = ramp(512);
    REQUIRE(feeder.feed(data));
    REQUIRE(collector.waitFor(512));

    auto got = collector.data();
    for (size_t i = 0; i < 512; i++) { REQUIRE(got[i] == Approx(data[i])); }

    fb.stop();
    collector.stop();
}

TEST_CASE("SampleFrameBuffer flush drops queued frames", "[dsp][buffer][frame]") {
    // flush() moves the read cursor to the write cursor. Without a consumer
    // attached nothing has drained yet, so this must discard the backlog rather
    // than replay it.
    dsp::stream<float> in;
    in.setBufferSize(4096);

    dsp::buffer::SampleFrameBuffer<float> fb;
    fb.init(&in);
    fb.out.setBufferSize(4096);

    fb.start();
    StreamFeeder<float> feeder(&in);
    for (int i = 0; i < 3; i++) { REQUIRE(feeder.feed(constant(64, 1.0f))); }
    std::this_thread::sleep_for(50ms);

    fb.flush();
    REQUIRE(fb.readCur == fb.writeCur);

    fb.stop();
}

TEST_CASE("SampleFrameBuffer can be stopped and restarted", "[dsp][buffer][frame]") {
    dsp::stream<float> in;
    in.setBufferSize(4096);

    dsp::buffer::SampleFrameBuffer<float> fb;
    fb.init(&in);
    fb.out.setBufferSize(4096);

    for (int round = 0; round < 2; round++) {
        INFO("round " << round);
        StreamCollector<float> collector(&fb.out);
        StreamFeeder<float> feeder(&in);

        fb.start();
        REQUIRE(feeder.feed(constant(128, (float)round)));
        REQUIRE(collector.waitFor(128));
        REQUIRE(collector.data().back() == Approx((float)round));

        fb.stop();
        collector.stop();
    }
}

TEST_CASE("SampleFrameBuffer setInput switches source", "[dsp][buffer][frame]") {
    dsp::stream<float> inA, inB;
    inA.setBufferSize(4096);
    inB.setBufferSize(4096);

    dsp::buffer::SampleFrameBuffer<float> fb;
    fb.init(&inA);
    fb.out.setBufferSize(4096);

    StreamCollector<float> collector(&fb.out);
    fb.start();

    {
        StreamFeeder<float> feeder(&inA);
        REQUIRE(feeder.feed(constant(64, 1.0f)));
        REQUIRE(collector.waitFor(64));
    }

    fb.setInput(&inB);
    {
        StreamFeeder<float> feeder(&inB);
        REQUIRE(feeder.feed(constant(64, 2.0f)));
        REQUIRE(collector.waitFor(128));
    }
    REQUIRE(collector.data().back() == Approx(2.0f));

    fb.stop();
    collector.stop();
}

// ------------------------------------------------------------------ Prebuffer

TEST_CASE("Prebuffer computes its target size from rate and latency", "[dsp][buffer][prebuffer]") {
    dsp::buffer::Prebuffer<dsp::stereo_t> pb;
    pb.init(nullptr);
    pb.out.setBufferSize(4096);

    pb.setSampleRate(48000);
    pb.setPrebufferMsec(250);
    REQUIRE(pb.getNeededBufferSize() == 12000);

    pb.setPrebufferMsec(0);
    REQUIRE(pb.getNeededBufferSize() == 0);
}

TEST_CASE("Prebuffer reports fullness as a percentage of the target", "[dsp][buffer][prebuffer]") {
    dsp::buffer::Prebuffer<float> pb;
    pb.init(nullptr);
    pb.out.setBufferSize(4096);

    pb.setSampleRate(1000);
    pb.setPrebufferMsec(100); // target 100 samples

    REQUIRE(pb.getPercentFull() == 0);
    pb.buffer.resize(50);
    REQUIRE(pb.getPercentFull() == 50);
    pb.buffer.resize(100);
    REQUIRE(pb.getPercentFull() == 100);

    // With no target configured, it reports full rather than dividing by zero.
    pb.setPrebufferMsec(0);
    REQUIRE(pb.getPercentFull() == 100);
}

TEST_CASE("Prebuffer reports the buffered duration", "[dsp][buffer][prebuffer]") {
    dsp::buffer::Prebuffer<float> pb;
    pb.init(nullptr);
    pb.out.setBufferSize(4096);

    pb.setSampleRate(48000);
    pb.buffer.resize(4800);
    REQUIRE(pb.getTimeDelayInMillis() == 100);

    pb.buffer.clear();
    REQUIRE(pb.getTimeDelayInMillis() == 0);
}

TEST_CASE("Prebuffer clear empties the buffer and rearms the fill", "[dsp][buffer][prebuffer]") {
    dsp::buffer::Prebuffer<float> pb;
    pb.init(nullptr);
    pb.out.setBufferSize(4096);

    pb.setSampleRate(48000);
    pb.setPrebufferMsec(100);
    pb.buffer.resize(1000);
    pb.bufferReached = true;

    pb.clear();
    REQUIRE(pb.buffer.empty());
    REQUIRE_FALSE(pb.bufferReached);
}

TEST_CASE("Prebuffer setSampleRate clears the buffer", "[dsp][buffer][prebuffer]") {
    // Changing the rate invalidates everything buffered, since the buffer is
    // measured in samples but configured in milliseconds.
    dsp::buffer::Prebuffer<float> pb;
    pb.init(nullptr);
    pb.out.setBufferSize(4096);

    pb.buffer.resize(500);
    pb.setSampleRate(96000);
    REQUIRE(pb.buffer.empty());
    REQUIRE(pb.sampleRate == 96000);
}

TEST_CASE("Prebuffer run accumulates input without emitting", "[dsp][buffer][prebuffer]") {
    // run() only fills the buffer; the detached worker started by doStart() is
    // what emits. Calling run() directly keeps the test deterministic.
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::buffer::Prebuffer<float> pb;
    pb.init(&in);
    pb.out.setBufferSize(1024);
    pb.setSampleRate(48000);
    pb.setPrebufferMsec(100);

    StreamFeeder<float> feeder(&in);
    REQUIRE(feeder.feed(ramp(256)));
    REQUIRE(pb.run() == 0);
    REQUIRE(pb.buffer.size() == 256);

    REQUIRE(feeder.feed(ramp(128, 256.0f)));
    REQUIRE(pb.run() == 0);
    REQUIRE(pb.buffer.size() == 384);

    // Order is preserved.
    for (size_t i = 0; i < pb.buffer.size(); i++) {
        INFO("sample " << i);
        REQUIRE(pb.buffer[i] == Approx((float)i));
    }
}
