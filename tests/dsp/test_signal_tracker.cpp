// Activation/release hysteresis and frame-to-frame association.
//
// The behaviour these tests pin down is what stops the automatic subsystem from thrashing:
// a single frame never confirms a signal, a short fade never ends one, and a transient that
// vanishes before confirmation never becomes ACTIVE at all.

#include <catch2/catch_test_macros.hpp>

#include <dsp/detector/signal_tracker.h>

#include <vector>

using namespace dsp::detector;

namespace {
    DetectedSignal at(double lower, double upper, float snr = 20.0f) {
        DetectedSignal s;
        s.lowerFrequency = lower;
        s.upperFrequency = upper;
        s.centerFrequency = (lower + upper) / 2.0;
        s.bandwidth = upper - lower;
        s.snrDb = snr;
        s.peakDb = -80.0f + snr;
        s.noiseFloorDb = -80.0f;
        return s;
    }
}

TEST_CASE("a new detection starts as a candidate", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.update({ at(100e6, 100.012e6) }, 0);

    REQUIRE(tracker.getTracked().size() == 1);
    CHECK(tracker.getTracked()[0].state == SignalState::CANDIDATE);
    CHECK(tracker.getTracked()[0].signal.id != 0);
    CHECK(tracker.getActiveSignals().empty());
}

TEST_CASE("a candidate is promoted after the activation delay", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.params.activationMs = 300;

    auto sig = at(100e6, 100.012e6);
    tracker.update({ sig }, 0);
    tracker.update({ sig }, 200);
    CHECK(tracker.getTracked()[0].state == SignalState::CANDIDATE);

    tracker.update({ sig }, 300);
    CHECK(tracker.getTracked()[0].state == SignalState::ACTIVE);
    CHECK(tracker.getActiveSignals().size() == 1);
}

TEST_CASE("a short transient never becomes active", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.params.activationMs = 300;
    tracker.params.releaseMs = 2000;

    auto sig = at(100e6, 100.012e6);
    tracker.update({ sig }, 0);
    tracker.update({ sig }, 100);
    // Gone from here on. Its age passes the activation delay while it is absent, which must not
    // promote it.
    for (uint64_t t = 200; t <= 2100; t += 100) {
        tracker.update({}, t);
        for (const auto& track : tracker.getTracked()) {
            CHECK(track.state != SignalState::ACTIVE);
        }
    }
    CHECK(tracker.getTracked().empty());
    // A candidate that was never confirmed is dropped, not reported as a finished signal.
    for (const auto& ended : tracker.getRecentlyEnded()) {
        CHECK_FALSE(ended.wasActive);
    }
}

TEST_CASE("an active signal survives a fade shorter than the release delay", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.params.activationMs = 300;
    tracker.params.releaseMs = 2000;

    auto sig = at(100e6, 100.012e6);
    tracker.update({ sig }, 0);
    tracker.update({ sig }, 400);
    REQUIRE(tracker.getTracked()[0].state == SignalState::ACTIVE);
    uint64_t id = tracker.getTracked()[0].signal.id;

    tracker.update({}, 900);
    CHECK(tracker.getTracked()[0].state == SignalState::RELEASING);

    // It comes back within the release window: same signal, still active.
    tracker.update({ sig }, 1400);
    REQUIRE(tracker.getTracked().size() == 1);
    CHECK(tracker.getTracked()[0].state == SignalState::ACTIVE);
    CHECK(tracker.getTracked()[0].signal.id == id);
}

TEST_CASE("an active signal ends after the release delay", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.params.activationMs = 300;
    tracker.params.releaseMs = 2000;

    auto sig = at(100e6, 100.012e6);
    tracker.update({ sig }, 0);
    tracker.update({ sig }, 400);
    uint64_t id = tracker.getTracked()[0].signal.id;

    tracker.update({}, 1000);
    CHECK(tracker.getTracked()[0].state == SignalState::RELEASING);

    tracker.update({}, 2400);
    CHECK(tracker.getTracked().empty());
    REQUIRE(tracker.getRecentlyEnded().size() == 1);
    CHECK(tracker.getRecentlyEnded()[0].signal.id == id);
    CHECK(tracker.getRecentlyEnded()[0].wasActive);

    // recentlyEnded only reports the most recent update.
    tracker.update({}, 2500);
    CHECK(tracker.getRecentlyEnded().empty());
}

TEST_CASE("a drifting signal keeps its identity", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.params.activationMs = 0;

    tracker.update({ at(100.000e6, 100.012e6) }, 0);
    uint64_t id = tracker.getTracked()[0].signal.id;
    uint64_t firstSeen = tracker.getTracked()[0].signal.firstSeen;

    // Shifts and widens slightly from frame to frame, as a real detection does.
    tracker.update({ at(100.001e6, 100.014e6) }, 100);
    tracker.update({ at(100.0005e6, 100.013e6) }, 200);

    REQUIRE(tracker.getTracked().size() == 1);
    CHECK(tracker.getTracked()[0].signal.id == id);
    CHECK(tracker.getTracked()[0].signal.firstSeen == firstSeen);
    CHECK(tracker.getTracked()[0].signal.lastSeen == 200);
    // The geometry follows the newest detection.
    CHECK(tracker.getTracked()[0].signal.lowerFrequency == 100.0005e6);
}

TEST_CASE("a detection far away is a different signal", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.update({ at(100.000e6, 100.012e6) }, 0);
    tracker.update({ at(100.000e6, 100.012e6), at(100.500e6, 100.512e6) }, 100);

    CHECK(tracker.getTracked().size() == 2);
    CHECK(tracker.getTracked()[0].signal.id != tracker.getTracked()[1].signal.id);
}

TEST_CASE("simultaneous signals are tracked independently", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.params.activationMs = 300;

    auto a = at(145.500e6, 145.512e6);
    auto b = at(145.575e6, 145.587e6);
    auto c = at(145.650e6, 145.662e6);

    tracker.update({ a, b, c }, 0);
    tracker.update({ a, b, c }, 400);
    CHECK(tracker.getActiveSignals().size() == 3);

    // One of them stops; the other two are unaffected.
    tracker.update({ a, c }, 500);
    CHECK(tracker.getActiveSignals().size() == 2);
    tracker.update({ a, c }, 3000);
    CHECK(tracker.getActiveSignals().size() == 2);
    CHECK(tracker.getTracked().size() == 2);
}

TEST_CASE("overlapRatio measures against the narrower range", "[detector][tracker]") {
    // A narrow signal fully inside a wide one overlaps it completely.
    CHECK(SignalTracker::overlapRatio(at(100e6, 100.1e6), at(100.02e6, 100.04e6)) == 1.0);
    // Disjoint ranges do not overlap at all.
    CHECK(SignalTracker::overlapRatio(at(100e6, 100.1e6), at(100.2e6, 100.3e6)) == 0.0);
    // Touching exactly is not overlapping.
    CHECK(SignalTracker::overlapRatio(at(100e6, 100.1e6), at(100.1e6, 100.2e6)) == 0.0);
}

TEST_CASE("clear forgets everything", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.params.activationMs = 0;
    tracker.update({ at(100e6, 100.012e6) }, 0);
    REQUIRE_FALSE(tracker.getTracked().empty());

    tracker.clear();
    CHECK(tracker.getTracked().empty());
    CHECK(tracker.getRecentlyEnded().empty());
}
