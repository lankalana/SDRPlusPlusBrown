// dsp::stream is the transport primitive the whole DSP graph is built on.
// These tests pin down the double-buffer handshake, the stop protocol and the
// backpressure guarantee, since a refactor of the graph must preserve them.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <dsp/stream.h>
#include <dsp/types.h>

#include "support/dsp_test_helpers.h"

using namespace std::chrono_literals;

TEST_CASE("stream allocates the default buffer size", "[dsp][stream]") {
    dsp::stream<float> str;
    REQUIRE(str.getBufferSize() == STREAM_BUFFER_SIZE);
    REQUIRE(str.readBuf != nullptr);
    REQUIRE(str.writeBuf != nullptr);
    REQUIRE(str.readBuf != str.writeBuf);
}

TEST_CASE("stream setBufferSize reallocates both buffers", "[dsp][stream]") {
    dsp::stream<float> str;
    str.setBufferSize(128);
    REQUIRE(str.getBufferSize() == 128);
    REQUIRE(str.readBuf != nullptr);
    REQUIRE(str.writeBuf != nullptr);
    REQUIRE(str.readBuf != str.writeBuf);
}

TEST_CASE("stream free() leaves the stream in a readable-but-empty state", "[dsp][stream]") {
    dsp::stream<float> str;
    str.free();
    REQUIRE(str.getBufferSize() == 0);
    REQUIRE(str.readBuf == nullptr);
    REQUIRE(str.writeBuf == nullptr);
    // read() must not dereference the freed buffers.
    REQUIRE(str.read() == -1);
}

TEST_CASE("stream swap hands the written data to the reader", "[dsp][stream]") {
    dsp::stream<float> str;
    str.setBufferSize(16);

    for (int i = 0; i < 4; i++) { str.writeBuf[i] = (float)i; }
    REQUIRE(str.swap(4));

    REQUIRE(str.isDataReady());
    REQUIRE(str.read() == 4);
    for (int i = 0; i < 4; i++) { REQUIRE(str.readBuf[i] == (float)i); }
    str.flush();
    REQUIRE_FALSE(str.isDataReady());
}

TEST_CASE("stream applies backpressure until the reader flushes", "[dsp][stream]") {
    dsp::stream<float> str;
    str.setBufferSize(16);

    str.writeBuf[0] = 1.0f;
    REQUIRE(str.swap(1));

    std::atomic<bool> secondSwapDone{ false };
    std::thread writer([&]() {
        str.writeBuf[0] = 2.0f;
        str.swap(1);
        secondSwapDone = true;
    });

    // The second swap must block: the reader has not flushed the first buffer.
    std::this_thread::sleep_for(50ms);
    REQUIRE_FALSE(secondSwapDone.load());

    REQUIRE(str.read() == 1);
    REQUIRE(str.readBuf[0] == 1.0f);
    str.flush();

    writer.join();
    REQUIRE(secondSwapDone.load());
    REQUIRE(str.read() == 1);
    REQUIRE(str.readBuf[0] == 2.0f);
    str.flush();
}

TEST_CASE("stream tryWrite drops data instead of applying backpressure", "[dsp][stream]") {
    dsp::stream<float> str;
    str.setBufferSize(16);

    const float first[] = { 1.0f, 2.0f };
    const float dropped[] = { 3.0f, 4.0f };
    REQUIRE(str.tryWrite(first, 2));
    REQUIRE_FALSE(str.tryWrite(dropped, 2));

    REQUIRE(str.read() == 2);
    REQUIRE(str.readBuf[0] == 1.0f);
    REQUIRE(str.readBuf[1] == 2.0f);
    str.flush();

    REQUIRE(str.tryWrite(dropped, 2));
    REQUIRE(str.read() == 2);
    REQUIRE(str.readBuf[0] == 3.0f);
    REQUIRE(str.readBuf[1] == 4.0f);
    str.flush();
}

TEST_CASE("stream stopReader unblocks a waiting reader", "[dsp][stream]") {
    dsp::stream<float> str;
    str.setBufferSize(16);

    std::atomic<int> result{ -99 };
    std::thread reader([&]() { result = str.read(); });

    std::this_thread::sleep_for(20ms);
    REQUIRE(result.load() == -99);

    str.stopReader();
    reader.join();
    REQUIRE(result.load() == -1);

    // After clearing the stop, the stream works again.
    str.clearReadStop();
    str.writeBuf[0] = 7.0f;
    REQUIRE(str.swap(1));
    REQUIRE(str.read() == 1);
    REQUIRE(str.readBuf[0] == 7.0f);
    str.flush();
}

TEST_CASE("stream stopWriter aborts a blocked swap", "[dsp][stream]") {
    dsp::stream<float> str;
    str.setBufferSize(16);

    REQUIRE(str.swap(1)); // fills the pending slot

    std::atomic<int> swapResult{ -1 };
    std::thread writer([&]() { swapResult = str.swap(1) ? 1 : 0; });

    std::this_thread::sleep_for(20ms);
    str.stopWriter();
    writer.join();

    REQUIRE(swapResult.load() == 0);

    str.clearWriteStop();
}

TEST_CASE("stream stopReader waits for the reader to leave read()", "[dsp][stream]") {
    // stopReader() spins until nReaders drops back to zero. Without that, a
    // block could join its worker thread while it is still inside read().
    dsp::stream<float> str;
    str.setBufferSize(16);

    std::atomic<bool> inRead{ false };
    std::atomic<bool> leftRead{ false };
    std::thread reader([&]() {
        inRead = true;
        str.read();
        leftRead = true;
    });

    while (!inRead.load()) { std::this_thread::sleep_for(1ms); }
    std::this_thread::sleep_for(10ms);

    str.stopReader();
    REQUIRE(leftRead.load());
    reader.join();
    str.clearReadStop();
}

TEST_CASE("stream carries complex and stereo samples intact", "[dsp][stream]") {
    SECTION("complex") {
        dsp::stream<dsp::complex_t> str;
        str.setBufferSize(8);
        str.writeBuf[0] = { 1.5f, -2.5f };
        REQUIRE(str.swap(1));
        REQUIRE(str.read() == 1);
        REQUIRE(str.readBuf[0].re == 1.5f);
        REQUIRE(str.readBuf[0].im == -2.5f);
        str.flush();
    }
    SECTION("stereo") {
        dsp::stream<dsp::stereo_t> str;
        str.setBufferSize(8);
        str.writeBuf[0] = { 0.25f, 0.75f };
        REQUIRE(str.swap(1));
        REQUIRE(str.read() == 1);
        REQUIRE(str.readBuf[0].l == 0.25f);
        REQUIRE(str.readBuf[0].r == 0.75f);
        str.flush();
    }
}

TEST_CASE("stream gives every instance a distinct default origin", "[dsp][stream]") {
    dsp::stream<float> a, b;
    REQUIRE(std::string(a.origin) != std::string(b.origin));

    dsp::stream<float> named("my-origin");
    REQUIRE(std::string(named.origin) == "my-origin");
}

TEST_CASE("queue accumulates from a raw pointer", "[dsp][stream][queue]") {
    dsp::queue<float> q;
    std::vector<float> src = { 1.0f, 2.0f, 3.0f, 4.0f };

    q.fillFrom(src.data(), (int)src.size());
    REQUIRE(q.dataSize.load() == 4);
    REQUIRE(q.isDataReady(4));
    REQUIRE_FALSE(q.isDataReady(5));

    float out[2] = {};
    REQUIRE(q.consume(out, 2));
    REQUIRE(out[0] == 1.0f);
    REQUIRE(out[1] == 2.0f);
    REQUIRE(q.dataSize.load() == 2);

    // Not enough data left, must be refused without consuming anything.
    float big[4] = {};
    REQUIRE_FALSE(q.consume(big, 4));
    REQUIRE(q.dataSize.load() == 2);
}

TEST_CASE("queue consume with a null destination just drops samples", "[dsp][stream][queue]") {
    dsp::queue<float> q;
    std::vector<float> src(10, 1.0f);
    q.fillFrom(src.data(), (int)src.size());

    REQUIRE(q.consume(nullptr, 6));
    REQUIRE(q.dataSize.load() == 4);
}
