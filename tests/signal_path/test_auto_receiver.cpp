// AutoReceiverManager: calibration lifecycle and its relationship with source changes.
//
// The invariant under test throughout is the one the whole design hangs off: the user owns the
// SDR centre frequency. The manager observes the captured spectrum and rebuilds its noise model
// whenever that spectrum changes meaning; it never asks the source to move.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <signal_path/auto_receiver.h>

#include <vector>

using namespace dsp::detector;

namespace {
    const int BINS = 64;
    const double CENTER = 100e6;
    const double SPAN = 1e6;
    const double FRAME_RATE = 20.0; // 20 frames per second
    const double USABLE = 1.0;

    AutoReceiverManager::Config fastConfig() {
        AutoReceiverManager::Config c;
        c.enabled = true;
        c.calibrationSeconds = 1.0; // 20 frames
        c.minSnrDb = 10.0f;
        c.activationMs = 300;
        c.releaseMs = 2000;
        c.minDetectionBins = 2;
        return c;
    }

    std::vector<float> quietFrame() { return std::vector<float>(BINS, -100.0f); }

    // Drive `frames` quiet frames through, 50 ms apart, starting at `startMs`.
    uint64_t feedQuiet(AutoReceiverManager& mgr, int frames, uint64_t startMs) {
        auto frame = quietFrame();
        uint64_t t = startMs;
        for (int i = 0; i < frames; i++) {
            mgr.onFFTFrame(frame.data(), BINS, CENTER, SPAN, USABLE, FRAME_RATE, t);
            t += 50;
        }
        return t;
    }

    SourceManager::State stateAt(double centerFrequency) {
        SourceManager::State s;
        s.sourceName = "Test Source";
        s.centerFrequency = centerFrequency;
        s.sampleRate = SPAN;
        s.decimation = 1;
        return s;
    }
}

TEST_CASE("a disabled manager ignores the spectrum entirely", "[auto_receiver]") {
    AutoReceiverManager mgr;
    REQUIRE_FALSE(mgr.isEnabled());

    feedQuiet(mgr, 100, 0);
    CHECK(mgr.getCalibrationState() == CalibrationState::UNCALIBRATED);
    CHECK(mgr.getTrackedSignals().empty());
}

TEST_CASE("enabling starts calibration on the first frame", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());

    feedQuiet(mgr, 1, 0);
    CHECK(mgr.getCalibrationState() == CalibrationState::CALIBRATING);
    CHECK(mgr.getCalibrationProgress() > 0.0f);
    CHECK(mgr.getCalibrationProgress() < 1.0f);
}

TEST_CASE("calibration completes after the configured duration", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());

    feedQuiet(mgr, 20, 0);
    CHECK(mgr.getCalibrationState() == CalibrationState::READY);
    CHECK(mgr.getCalibrationProgress() == 1.0f);
}

TEST_CASE("no detections are produced while calibrating", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());

    // A loud signal present from the very start must not be reported before the model exists.
    auto frame = quietFrame();
    for (int b = 30; b <= 35; b++) { frame[b] = -40.0f; }

    uint64_t t = 0;
    for (int i = 0; i < 10; i++) {
        mgr.onFFTFrame(frame.data(), BINS, CENTER, SPAN, USABLE, FRAME_RATE, t);
        CHECK(mgr.getTrackedSignals().empty());
        t += 50;
    }
    CHECK(mgr.getCalibrationState() == CalibrationState::CALIBRATING);
}

TEST_CASE("a signal is detected and confirmed after calibration", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());

    uint64_t t = feedQuiet(mgr, 20, 0);
    REQUIRE(mgr.getCalibrationState() == CalibrationState::READY);

    auto frame = quietFrame();
    for (int b = 30; b <= 35; b++) { frame[b] = -40.0f; }

    mgr.onFFTFrame(frame.data(), BINS, CENTER, SPAN, USABLE, FRAME_RATE, t);
    REQUIRE(mgr.getTrackedSignals().size() == 1);
    CHECK(mgr.getTrackedSignals()[0].state == SignalState::CANDIDATE);
    CHECK(mgr.getActiveSignalCount() == 0);

    // Past the 300 ms activation delay.
    for (int i = 0; i < 8; i++) {
        t += 50;
        mgr.onFFTFrame(frame.data(), BINS, CENTER, SPAN, USABLE, FRAME_RATE, t);
    }
    CHECK(mgr.getActiveSignalCount() == 1);

    const auto& sig = mgr.getTrackedSignals()[0].signal;
    CHECK(sig.snrDb == Catch::Approx(60.0f));
    CHECK(sig.centerFrequency > CENTER);
}

TEST_CASE("noise after calibration does not trigger repeatedly", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());

    // Calibrate on noisy but signal-free spectrum, then keep feeding the same kind of noise.
    uint32_t s = 999;
    auto noise = [&]() {
        s = s * 1664525u + 1013904223u;
        return (((float)(s >> 8) / (float)(1 << 24)) - 0.5f) * 4.0f;
    };

    std::vector<float> frame(BINS);
    uint64_t t = 0;
    for (int i = 0; i < 20; i++) {
        for (int b = 0; b < BINS; b++) { frame[b] = -100.0f + noise(); }
        mgr.onFFTFrame(frame.data(), BINS, CENTER, SPAN, USABLE, FRAME_RATE, t);
        t += 50;
    }
    REQUIRE(mgr.getCalibrationState() == CalibrationState::READY);

    for (int i = 0; i < 200; i++) {
        for (int b = 0; b < BINS; b++) { frame[b] = -100.0f + noise(); }
        mgr.onFFTFrame(frame.data(), BINS, CENTER, SPAN, USABLE, FRAME_RATE, t);
        t += 50;
        CHECK(mgr.getActiveSignalCount() == 0);
    }
}

TEST_CASE("changing the center frequency invalidates calibration", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());
    mgr.updateSourceState(stateAt(CENTER));

    feedQuiet(mgr, 20, 0);
    REQUIRE(mgr.getCalibrationState() == CalibrationState::READY);

    mgr.updateSourceState(stateAt(CENTER + 1e6));
    CHECK(mgr.getCalibrationState() == CalibrationState::UNCALIBRATED);
    CHECK(mgr.getLastInvalidationReason() == CalibrationInvalidationReason::CENTER_FREQUENCY);
    CHECK(mgr.getTrackedSignals().empty());
}

TEST_CASE("changing the sample rate or source invalidates calibration", "[auto_receiver]") {
    SECTION("sample rate") {
        AutoReceiverManager mgr;
        mgr.setConfig(fastConfig());
        mgr.updateSourceState(stateAt(CENTER));
        feedQuiet(mgr, 20, 0);
        REQUIRE(mgr.getCalibrationState() == CalibrationState::READY);

        auto next = stateAt(CENTER);
        next.sampleRate = SPAN * 2;
        mgr.updateSourceState(next);
        CHECK(mgr.getCalibrationState() == CalibrationState::UNCALIBRATED);
        CHECK(mgr.getLastInvalidationReason() == CalibrationInvalidationReason::SAMPLE_RATE);
    }

    SECTION("source device") {
        AutoReceiverManager mgr;
        mgr.setConfig(fastConfig());
        mgr.updateSourceState(stateAt(CENTER));
        feedQuiet(mgr, 20, 0);
        REQUIRE(mgr.getCalibrationState() == CalibrationState::READY);

        auto next = stateAt(CENTER);
        next.sourceName = "Another Source";
        mgr.updateSourceState(next);
        CHECK(mgr.getCalibrationState() == CalibrationState::UNCALIBRATED);
        CHECK(mgr.getLastInvalidationReason() == CalibrationInvalidationReason::SOURCE_DEVICE);
    }

    SECTION("decimation") {
        AutoReceiverManager mgr;
        mgr.setConfig(fastConfig());
        mgr.updateSourceState(stateAt(CENTER));
        feedQuiet(mgr, 20, 0);
        REQUIRE(mgr.getCalibrationState() == CalibrationState::READY);

        auto next = stateAt(CENTER);
        next.decimation = 2;
        mgr.updateSourceState(next);
        CHECK(mgr.getCalibrationState() == CalibrationState::UNCALIBRATED);
        CHECK(mgr.getLastInvalidationReason() == CalibrationInvalidationReason::DECIMATION);
    }
}

TEST_CASE("an unchanged source state does not invalidate calibration", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());
    mgr.updateSourceState(stateAt(CENTER));
    feedQuiet(mgr, 20, 0);
    REQUIRE(mgr.getCalibrationState() == CalibrationState::READY);

    mgr.updateSourceState(stateAt(CENTER));
    CHECK(mgr.getCalibrationState() == CalibrationState::READY);
}

TEST_CASE("detection resumes only after the new calibration completes", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());
    mgr.updateSourceState(stateAt(CENTER));

    uint64_t t = feedQuiet(mgr, 20, 0);
    REQUIRE(mgr.getCalibrationState() == CalibrationState::READY);

    mgr.updateSourceState(stateAt(CENTER + 1e6));

    auto frame = quietFrame();
    for (int b = 30; b <= 35; b++) { frame[b] = -40.0f; }

    // The signal is right there, but nothing may be reported until the model is rebuilt.
    for (int i = 0; i < 19; i++) {
        mgr.onFFTFrame(frame.data(), BINS, CENTER + 1e6, SPAN, USABLE, FRAME_RATE, t);
        CHECK(mgr.getTrackedSignals().empty());
        CHECK(mgr.getCalibrationState() == CalibrationState::CALIBRATING);
        t += 50;
    }

    mgr.onFFTFrame(frame.data(), BINS, CENTER + 1e6, SPAN, USABLE, FRAME_RATE, t);
    CHECK(mgr.getCalibrationState() == CalibrationState::READY);
}

TEST_CASE("an FFT size change invalidates calibration", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());
    feedQuiet(mgr, 20, 0);
    REQUIRE(mgr.getCalibrationState() == CalibrationState::READY);

    std::vector<float> wider(BINS * 2, -100.0f);
    mgr.onFFTFrame(wider.data(), BINS * 2, CENTER, SPAN, USABLE, FRAME_RATE, 2000);
    CHECK(mgr.getLastInvalidationReason() == CalibrationInvalidationReason::SPECTRUM_SHAPE);
    CHECK(mgr.getCalibrationState() == CalibrationState::CALIBRATING);
}

TEST_CASE("disabling clears the model and tracked signals", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());

    uint64_t t = feedQuiet(mgr, 20, 0);
    auto frame = quietFrame();
    for (int b = 30; b <= 35; b++) { frame[b] = -40.0f; }
    mgr.onFFTFrame(frame.data(), BINS, CENTER, SPAN, USABLE, FRAME_RATE, t);
    REQUIRE_FALSE(mgr.getTrackedSignals().empty());

    mgr.setEnabled(false);
    CHECK(mgr.getCalibrationState() == CalibrationState::UNCALIBRATED);
    CHECK(mgr.getTrackedSignals().empty());
    CHECK(mgr.getLastInvalidationReason() == CalibrationInvalidationReason::NOT_ENABLED);
}

TEST_CASE("manual recalibration restarts the model", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());
    feedQuiet(mgr, 20, 0);
    REQUIRE(mgr.getCalibrationState() == CalibrationState::READY);

    mgr.invalidateCalibration(CalibrationInvalidationReason::MANUAL);
    CHECK(mgr.getCalibrationState() == CalibrationState::UNCALIBRATED);

    feedQuiet(mgr, 20, 5000);
    CHECK(mgr.getCalibrationState() == CalibrationState::READY);
}

TEST_CASE("onDetectionUpdate reports the active signals", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(fastConfig());

    std::vector<DetectedSignal> lastUpdate;
    int updateCount = 0;
    struct Ctx {
        std::vector<DetectedSignal>* out;
        int* count;
    } ctx{ &lastUpdate, &updateCount };

    EventHandler<std::vector<DetectedSignal>> handler(
        [](std::vector<DetectedSignal> signals, void* c) {
            auto* cc = (Ctx*)c;
            *cc->out = signals;
            (*cc->count)++;
        },
        &ctx);
    mgr.onDetectionUpdate.bindHandler(&handler);

    // Nothing is emitted while calibrating.
    uint64_t t = feedQuiet(mgr, 20, 0);
    CHECK(updateCount == 0);

    auto frame = quietFrame();
    for (int b = 30; b <= 35; b++) { frame[b] = -40.0f; }
    for (int i = 0; i < 10; i++) {
        mgr.onFFTFrame(frame.data(), BINS, CENTER, SPAN, USABLE, FRAME_RATE, t);
        t += 50;
    }

    CHECK(updateCount == 10);
    REQUIRE(lastUpdate.size() == 1);
    CHECK(lastUpdate[0].snrDb == Catch::Approx(60.0f));

    mgr.onDetectionUpdate.unbindHandler(&handler);
}
