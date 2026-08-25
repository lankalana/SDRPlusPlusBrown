// Buffering and reframing blocks: the ring buffer, the packer and the reshaper
// that produces FFT frames.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <dsp/buffer/buffer.h>
#include <dsp/buffer/packer.h>
#include <dsp/buffer/reshaper.h>
#include <dsp/buffer/ring_buffer.h>

#include "support/dsp_test_helpers.h"

using namespace std::chrono_literals;
using namespace sdrpp_test;

TEST_CASE("buffer alloc and clear", "[dsp][buffer]") {
    float* buf = dsp::buffer::alloc<float>(64);
    REQUIRE(buf != nullptr);

    for (int i = 0; i < 64; i++) { buf[i] = 1.0f; }
    dsp::buffer::clear(buf, 32);
    for (int i = 0; i < 32; i++) { REQUIRE(buf[i] == 0.0f); }
    for (int i = 32; i < 64; i++) { REQUIRE(buf[i] == 1.0f); }

    dsp::buffer::clear(buf, 16, 48);
    for (int i = 32; i < 48; i++) { REQUIRE(buf[i] == 1.0f); }
    for (int i = 48; i < 64; i++) { REQUIRE(buf[i] == 0.0f); }

    dsp::buffer::free(buf);
}

TEST_CASE("RingBuffer round-trips data", "[dsp][buffer][ring]") {
    dsp::buffer::RingBuffer<float> ring;
    ring.init(1024);

    auto data = ramp(256);
    REQUIRE(ring.write(data.data(), 256) == 256);
    REQUIRE(ring.getReadable() == 256);

    std::vector<float> out(256);
    REQUIRE(ring.read(out.data(), 256) == 256);
    REQUIRE(out == data);
    REQUIRE(ring.getReadable() == 0);
}

TEST_CASE("RingBuffer readAndSkip drops samples after the ones it keeps", "[dsp][buffer][ring]") {
    dsp::buffer::RingBuffer<float> ring;
    ring.init(1024);

    auto data = ramp(100);
    ring.write(data.data(), 100);

    std::vector<float> out(10);
    REQUIRE(ring.readAndSkip(out.data(), 10, 20) == 10);
    for (int i = 0; i < 10; i++) { REQUIRE(out[i] == (float)i); }
    REQUIRE(ring.getReadable() == 70);

    // The next read starts after the skipped block.
    std::vector<float> next(1);
    ring.read(next.data(), 1);
    REQUIRE(next[0] == 30.0f);
}

TEST_CASE("RingBuffer maxLatency limits how much can be written", "[dsp][buffer][ring]") {
    dsp::buffer::RingBuffer<float> ring;
    ring.init(64);

    auto data = constant(64, 1.0f);
    ring.write(data.data(), 64);
    REQUIRE(ring.getWritable() == 0);

    std::vector<float> out(32);
    ring.read(out.data(), 32);
    REQUIRE(ring.getWritable() == 32);
}

TEST_CASE("RingBuffer blocks the writer until the reader drains", "[dsp][buffer][ring]") {
    dsp::buffer::RingBuffer<float> ring;
    ring.init(16);

    auto data = constant(16, 1.0f);
    ring.write(data.data(), 16);

    std::atomic<bool> done{ false };
    std::thread writer([&]() {
        auto more = constant(8, 2.0f);
        ring.write(more.data(), 8);
        done = true;
    });

    std::this_thread::sleep_for(30ms);
    REQUIRE_FALSE(done.load());

    std::vector<float> out(16);
    ring.read(out.data(), 16);
    writer.join();
    REQUIRE(done.load());

    std::vector<float> rest(8);
    ring.read(rest.data(), 8);
    REQUIRE(rest[0] == 2.0f);
}

TEST_CASE("RingBuffer stopReader unblocks a waiting reader", "[dsp][buffer][ring]") {
    dsp::buffer::RingBuffer<float> ring;
    ring.init(64);

    std::atomic<int> result{ -99 };
    std::thread reader([&]() {
        std::vector<float> out(8);
        result = ring.read(out.data(), 8);
    });

    std::this_thread::sleep_for(20ms);
    ring.stopReader();
    reader.join();

    REQUIRE(result.load() == -1);
    REQUIRE(ring.getReadStop());
    ring.clearReadStop();
    REQUIRE_FALSE(ring.getReadStop());
}

TEST_CASE("RingBuffer stopWriter unblocks a waiting writer", "[dsp][buffer][ring]") {
    dsp::buffer::RingBuffer<float> ring;
    ring.init(8);

    auto fill = constant(8, 1.0f);
    ring.write(fill.data(), 8);

    std::atomic<int> result{ -99 };
    std::thread writer([&]() {
        auto more = constant(4, 2.0f);
        result = ring.write(more.data(), 4);
    });

    std::this_thread::sleep_for(20ms);
    ring.stopWriter();
    writer.join();

    REQUIRE(result.load() == -1);
    REQUIRE(ring.getWriteStop());
    ring.clearWriteStop();
}

TEST_CASE("RingBuffer wraps around the end of its storage", "[dsp][buffer][ring]") {
    // Push more than RING_BUF_SZ samples through so the read and write cursors
    // wrap past the end of the backing allocation at least once.
    dsp::buffer::RingBuffer<float> ring;
    const int chunk = 4096;
    ring.init(4096, 2 * chunk);

    const int chunks = (2 * chunk) + 8;
    const long long total = (long long)chunk * chunks;

    std::atomic<long long> verified{ 0 };
    std::atomic<bool> mismatch{ false };
    std::thread reader([&]() {
        std::vector<float> buf(chunk);
        long long expected = 0;
        while (verified.load() < total) {
            if (ring.read(buf.data(), chunk) < 0) { break; }
            for (int i = 0; i < chunk; i++) {
                if (buf[i] != (float)((expected + i) % 1000)) { mismatch = true; }
            }
            expected += chunk;
            verified.store(expected);
        }
    });

    std::vector<float> data(chunk);
    for (long long off = 0; off < total; off += chunk) {
        for (int i = 0; i < chunk; i++) { data[i] = (float)((off + i) % 1000); }
        ring.write(data.data(), chunk);
    }

    for (int i = 0; i < 2000 && verified.load() < total; i++) {
        std::this_thread::sleep_for(1ms);
    }
    ring.stopReader();
    reader.join();

    REQUIRE_FALSE(mismatch.load());
    REQUIRE(verified.load() == total);
}

TEST_CASE("Packer emits fixed size blocks", "[dsp][buffer][packer]") {
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::buffer::Packer<float> packer;
    packer.init(&in, 100);
    packer.out.setBufferSize(1024);

    StreamCollector<float> collector(&packer.out);
    StreamFeeder<float> feeder(&in);

    packer.start();
    // 7 chunks of 30 samples: 210 samples in, two full blocks of 100 out.
    for (int i = 0; i < 7; i++) { REQUIRE(feeder.feed(constant(30, (float)i))); }
    REQUIRE(collector.waitFor(200));
    collector.waitIdle(20ms, 300ms);

    REQUIRE(collector.size() == 200);
    for (int chunk : collector.chunks()) { REQUIRE(chunk == 100); }

    packer.stop();
    collector.stop();
}

TEST_CASE("Packer preserves sample order across block boundaries", "[dsp][buffer][packer]") {
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::buffer::Packer<float> packer;
    packer.init(&in, 64);
    packer.out.setBufferSize(1024);

    StreamCollector<float> collector(&packer.out);
    StreamFeeder<float> feeder(&in);

    packer.start();
    auto data = ramp(192);
    REQUIRE(feeder.feed(data, 17)); // deliberately not a multiple of 64
    REQUIRE(collector.waitFor(192));
    collector.waitIdle(20ms, 300ms);

    auto got = collector.data();
    REQUIRE(got.size() == 192);
    for (size_t i = 0; i < got.size(); i++) { REQUIRE(got[i] == (float)i); }

    packer.stop();
    collector.stop();
}

TEST_CASE("Packer setSampleCount changes the block size", "[dsp][buffer][packer]") {
    dsp::stream<float> in;
    in.setBufferSize(1024);

    dsp::buffer::Packer<float> packer;
    packer.init(&in, 32);
    packer.out.setBufferSize(1024);

    StreamCollector<float> collector(&packer.out);
    StreamFeeder<float> feeder(&in);

    packer.start();
    REQUIRE(feeder.feed(constant(32, 1.0f)));
    REQUIRE(collector.waitFor(32));

    packer.setSampleCount(16);
    REQUIRE(feeder.feed(constant(32, 2.0f)));
    REQUIRE(collector.waitFor(64));
    collector.waitIdle(20ms, 300ms);

    auto chunks = collector.chunks();
    REQUIRE(chunks.front() == 32);
    REQUIRE(chunks.back() == 16);

    packer.stop();
    collector.stop();
}

TEST_CASE("Reshaper emits frames of the requested size", "[dsp][buffer][reshaper]") {
    // This is how the FFT branch turns a continuous stream into FFT frames.
    dsp::stream<float> in;
    in.setBufferSize(4096);

    dsp::buffer::Reshaper<float> reshaper;
    reshaper.init(&in, 128, 0);
    reshaper.out.setBufferSize(4096);

    StreamCollector<float> collector(&reshaper.out);
    StreamFeeder<float> feeder(&in);

    reshaper.start();
    auto data = ramp(1024);
    REQUIRE(feeder.feed(data, 256));
    REQUIRE(collector.waitFor(512));

    for (int chunk : collector.chunks()) { REQUIRE(chunk == 128); }

    auto got = collector.data();
    // With skip == 0 the frames are contiguous.
    for (size_t i = 0; i < got.size(); i++) { REQUIRE(got[i] == (float)i); }

    reshaper.stop();
    collector.stop();
}

TEST_CASE("Reshaper skip drops samples between frames", "[dsp][buffer][reshaper]") {
    dsp::stream<float> in;
    in.setBufferSize(8192);

    dsp::buffer::Reshaper<float> reshaper;
    reshaper.init(&in, 64, 64); // keep 64, then drop 64
    reshaper.out.setBufferSize(8192);

    StreamCollector<float> collector(&reshaper.out);
    StreamFeeder<float> feeder(&in);

    reshaper.start();
    auto data = ramp(2048);
    REQUIRE(feeder.feed(data, 256));
    REQUIRE(collector.waitFor(128));

    auto got = collector.data();
    // Frame 0 is samples 0..63, frame 1 starts at 128.
    REQUIRE(got[0] == 0.0f);
    REQUIRE(got[63] == 63.0f);
    REQUIRE(got[64] == 128.0f);

    reshaper.stop();
    collector.stop();
}

TEST_CASE("Reshaper negative skip overlaps consecutive frames", "[dsp][buffer][reshaper]") {
    dsp::stream<float> in;
    in.setBufferSize(4096);

    dsp::buffer::Reshaper<float> reshaper;
    reshaper.init(&in, 8, -6); // advance two samples per eight-sample frame
    reshaper.out.setBufferSize(4096);

    StreamCollector<float> collector(&reshaper.out);
    StreamFeeder<float> feeder(&in);

    reshaper.start();
    REQUIRE(feeder.feed(ramp(64), 64));
    REQUIRE(collector.waitFor(24));

    auto got = collector.data();
    for (int frame = 0; frame < 3; frame++) {
        for (int sample = 0; sample < 8; sample++) {
            REQUIRE(got[(frame * 8) + sample] == (float)((frame * 2) + sample));
        }
    }

    reshaper.stop();
    collector.stop();
}
