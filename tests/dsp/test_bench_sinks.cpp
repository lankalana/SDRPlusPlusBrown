// dsp::bench::PeakLevelMeter and dsp::sink::RingBuffer.
//
// The meter drives the S-meter and the audio level bars; the ring buffer sink
// is what audio backends pull from. The sink's own header says it is
// "COMPLETELY UNTESTED AND PROBABLY BROKEN", so these tests establish what it
// actually does today.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <dsp/bench/peak_level_meter.h>
#include <dsp/sink/ring_buffer.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace std::chrono_literals;
using namespace sdrpp_test;

// ------------------------------------------------------------ PeakLevelMeter

TEST_CASE("PeakLevelMeter tracks the peak of a float stream", "[dsp][bench][meter]") {
    dsp::bench::PeakLevelMeter<float> meter;
    meter.init(nullptr);

    REQUIRE(meter.getLevel() == 0.0f);

    std::vector<float> in = { 0.1f, -0.7f, 0.3f, 0.5f };
    meter.process((int)in.size(), in.data());
    REQUIRE(meter.getLevel() == Approx(0.7f)); // magnitude, so the -0.7 wins
}

TEST_CASE("PeakLevelMeter holds the peak across calls", "[dsp][bench][meter]") {
    dsp::bench::PeakLevelMeter<float> meter;
    meter.init(nullptr);

    std::vector<float> loud = { 0.9f };
    std::vector<float> quiet = { 0.1f, 0.05f };
    meter.process(1, loud.data());
    meter.process(2, quiet.data());
    REQUIRE(meter.getLevel() == Approx(0.9f)); // peak hold, not a running max

    meter.resetLevel();
    REQUIRE(meter.getLevel() == 0.0f);
    meter.process(2, quiet.data());
    REQUIRE(meter.getLevel() == Approx(0.1f));
}

TEST_CASE("PeakLevelMeter tracks complex channels independently", "[dsp][bench][meter]") {
    dsp::bench::PeakLevelMeter<dsp::complex_t> meter;
    meter.init(nullptr);

    REQUIRE(meter.getLevel().re == 0.0f);
    REQUIRE(meter.getLevel().im == 0.0f);

    std::vector<dsp::complex_t> in = { { 0.2f, -0.8f }, { -0.6f, 0.1f } };
    meter.process(2, in.data());
    REQUIRE(meter.getLevel().re == Approx(0.6f));
    REQUIRE(meter.getLevel().im == Approx(0.8f));
}

TEST_CASE("PeakLevelMeter tracks stereo channels independently", "[dsp][bench][meter]") {
    dsp::bench::PeakLevelMeter<dsp::stereo_t> meter;
    meter.init(nullptr);

    std::vector<dsp::stereo_t> in = { { 0.4f, 0.1f }, { -0.2f, -0.9f } };
    meter.process(2, in.data());
    REQUIRE(meter.getLevel().l == Approx(0.4f));
    REQUIRE(meter.getLevel().r == Approx(0.9f));

    meter.resetLevel();
    REQUIRE(meter.getLevel().l == 0.0f);
    REQUIRE(meter.getLevel().r == 0.0f);
}

TEST_CASE("PeakLevelMeter runs as a sink", "[dsp][bench][meter]") {
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::bench::PeakLevelMeter<float> meter;
    meter.init(&in);

    StreamFeeder<float> feeder(&in);
    meter.start();

    auto data = cosine(512, 1000.0, 48000.0, 0.6);
    REQUIRE(feeder.feed(data));

    // Poll: the worker thread consumes asynchronously.
    for (int i = 0; i < 100 && meter.getLevel() < 0.5f; i++) {
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(meter.getLevel() == Approx(0.6f).margin(0.01f));

    meter.stop();
}

// -------------------------------------------------------------- sink::RingBuffer

TEST_CASE("sink::RingBuffer collects what the stream carries", "[dsp][sink][ring]") {
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::sink::RingBuffer<float> sink;
    sink.init(&in, 4096);

    StreamFeeder<float> feeder(&in);
    sink.start();

    auto data = ramp(512);
    REQUIRE(feeder.feed(data));

    for (int i = 0; i < 200 && sink.data.getReadable() < 512; i++) {
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(sink.data.getReadable() >= 512);

    std::vector<float> out(512);
    REQUIRE(sink.data.read(out.data(), 512) == 512);
    for (int i = 0; i < 512; i++) {
        INFO("sample " << i);
        REQUIRE(out[i] == Approx((float)i));
    }

    sink.stop();
}

TEST_CASE("sink::RingBuffer stops cleanly while the reader waits", "[dsp][sink][ring]") {
    // doStop() stops the writer side of the ring buffer, so a consumer blocked
    // in read() must be released rather than deadlocking the shutdown.
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::sink::RingBuffer<float> sink;
    sink.init(&in, 1024);
    sink.start();

    std::atomic<int> result{ -99 };
    std::thread reader([&]() {
        std::vector<float> buf(64);
        result = sink.data.read(buf.data(), 64);
    });

    std::this_thread::sleep_for(30ms);
    sink.stop();
    sink.data.stopReader();
    reader.join();

    REQUIRE(result.load() == -1);
}

TEST_CASE("sink::RingBuffer respects its latency limit", "[dsp][sink][ring]") {
    // The ring is created with maxLatency samples of room. Once full, the sink's
    // run() blocks in write() instead of overwriting unread data.
    dsp::stream<float> in;
    in.setBufferSize(2048);

    dsp::sink::RingBuffer<float> sink;
    sink.init(&in, 256);
    sink.start();

    StreamFeeder<float> feeder(&in);
    REQUIRE(feeder.feed(constant(256, 1.0f)));

    for (int i = 0; i < 200 && sink.data.getReadable() < 256; i++) {
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(sink.data.getReadable() == 256);
    REQUIRE(sink.data.getWritable() == 0);

    std::vector<float> out(256);
    sink.data.read(out.data(), 256);
    REQUIRE(sink.data.getWritable() == 256);

    sink.stop();
    sink.data.stopReader();
}
