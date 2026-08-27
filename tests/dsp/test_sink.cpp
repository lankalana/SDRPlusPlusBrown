// Terminal blocks and the audio volume stage.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <vector>

#include <dsp/audio/volume.h>
#include <dsp/sink/handler_sink.h>
#include <dsp/sink/null_sink.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace std::chrono_literals;
using namespace sdrpp_test;

namespace {
    struct HandlerState {
        std::atomic<int> calls{ 0 };
        std::atomic<int> samples{ 0 };
        std::vector<float> data;
        std::mutex mtx;
    };

    void collect(float* data, int count, void* ctx) {
        auto* st = (HandlerState*)ctx;
        std::lock_guard<std::mutex> lck(st->mtx);
        st->data.insert(st->data.end(), data, data + count);
        st->calls++;
        st->samples += count;
    }
}

TEST_CASE("Handler sink invokes the callback for each block", "[dsp][sink]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    HandlerState state;
    dsp::sink::Handler<float> sink;
    sink.init(&in, collect, &state);

    StreamFeeder<float> feeder(&in);
    sink.start();

    REQUIRE(feeder.feed(ramp(32)));
    REQUIRE(feeder.feed(ramp(16, 100.0f)));

    for (int i = 0; i < 200 && state.samples.load() < 48; i++) { std::this_thread::sleep_for(1ms); }
    sink.stop();

    REQUIRE(state.calls.load() == 2);
    REQUIRE(state.samples.load() == 48);

    std::lock_guard<std::mutex> lck(state.mtx);
    REQUIRE(state.data[0] == 0.0f);
    REQUIRE(state.data[32] == 100.0f);
}

TEST_CASE("Null sink consumes without producing", "[dsp][sink]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    dsp::sink::Null<float> sink;
    sink.init(&in);

    StreamFeeder<float> feeder(&in);
    sink.start();

    // Three feeds in a row only succeed if the sink keeps flushing.
    for (int i = 0; i < 3; i++) { REQUIRE(feeder.feed(constant(8, 1.0f))); }

    sink.stop();
}

TEST_CASE("Volume scales by the square of the setting", "[dsp][audio][volume]") {
    dsp::stream<dsp::stereo_t> in;
    in.setBufferSize(256);

    dsp::audio::Volume vol;
    vol.init(&in, 0.5, false);
    vol.out.setBufferSize(256);

    std::vector<dsp::stereo_t> input(16, dsp::stereo_t{ 1.0f, -1.0f });
    std::vector<dsp::stereo_t> out(16);
    REQUIRE(vol.process(16, input.data(), out.data()) == 16);

    // The UI slider is perceptual: 0.5 means 0.25 linear gain.
    REQUIRE(out[0].l == Approx(0.25f));
    REQUIRE(out[0].r == Approx(-0.25f));
}

TEST_CASE("Volume unity setting passes audio through", "[dsp][audio][volume]") {
    dsp::stream<dsp::stereo_t> in;
    in.setBufferSize(256);

    dsp::audio::Volume vol;
    vol.init(&in, 1.0, false);
    vol.out.setBufferSize(256);

    std::vector<dsp::stereo_t> input(8, dsp::stereo_t{ 0.3f, 0.7f });
    std::vector<dsp::stereo_t> out(8);
    vol.process(8, input.data(), out.data());

    REQUIRE(out[0].l == Approx(0.3f));
    REQUIRE(out[0].r == Approx(0.7f));
}

TEST_CASE("Volume mute and tempMute both silence the output", "[dsp][audio][volume]") {
    dsp::stream<dsp::stereo_t> in;
    in.setBufferSize(256);

    dsp::audio::Volume vol;
    vol.init(&in, 1.0, false);
    vol.out.setBufferSize(256);

    std::vector<dsp::stereo_t> input(4, dsp::stereo_t{ 1.0f, 1.0f });
    std::vector<dsp::stereo_t> out(4);

    REQUIRE_FALSE(vol.getMuted());

    vol.setMuted(true);
    REQUIRE(vol.getMuted());
    vol.process(4, input.data(), out.data());
    REQUIRE(out[0].l == 0.0f);

    vol.setMuted(false);
    vol.setTempMuted(true);
    REQUIRE(vol.getMuted()); // getMuted() reports the combined state
    vol.process(4, input.data(), out.data());
    REQUIRE(out[0].r == 0.0f);

    vol.setTempMuted(false);
    vol.process(4, input.data(), out.data());
    REQUIRE(out[0].l == Approx(1.0f));
}

TEST_CASE("Volume setVolume takes effect on the next block", "[dsp][audio][volume]") {
    dsp::stream<dsp::stereo_t> in;
    in.setBufferSize(256);

    dsp::audio::Volume vol;
    vol.init(&in, 1.0, false);
    vol.out.setBufferSize(256);

    std::vector<dsp::stereo_t> input(4, dsp::stereo_t{ 1.0f, 1.0f });
    std::vector<dsp::stereo_t> out(4);

    vol.setVolume(0.0);
    vol.process(4, input.data(), out.data());
    REQUIRE(out[0].l == 0.0f);

    vol.setVolume(2.0);
    vol.process(4, input.data(), out.data());
    REQUIRE(out[0].l == Approx(4.0f));
}
