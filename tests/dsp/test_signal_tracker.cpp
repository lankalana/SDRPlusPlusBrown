// Activation/release hysteresis and frame-to-frame association.
//
// The behaviour these tests pin down is what stops the automatic subsystem from thrashing:
// a single frame never confirms a signal, a short fade never ends one, and a transient that
// vanishes before confirmation never becomes ACTIVE at all.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <dsp/detector/signal_tracker.h>

#include <cmath>

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

TEST_CASE("same-channel fragments are one signal even when disjoint", "[detector][tracker]") {
    // Two above-threshold runs from opposite sides of one FM channel, seen in different frames.
    // They do not overlap at all, so only the channel tag can tie them together.
    SignalTracker tracker;
    tracker.params.activationMs = 0;

    auto lower = at(103.62e6, 103.66e6);
    lower.channelFrequency = 103.7e6;
    lower.channelIndex = 1037;
    auto upper = at(103.74e6, 103.78e6);
    upper.channelFrequency = 103.7e6;
    upper.channelIndex = 1037;

    REQUIRE(SignalTracker::overlapRatio(lower, upper) == 0.0);

    tracker.update({ lower }, 0);
    uint64_t id = tracker.getTracked()[0].signal.id;
    tracker.update({ upper }, 100);

    REQUIRE(tracker.getTracked().size() == 1);
    CHECK(tracker.getTracked()[0].signal.id == id);
}

TEST_CASE("different channels stay separate even when they overlap", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.params.activationMs = 0;

    auto a = at(103.65e6, 103.75e6);
    a.channelFrequency = 103.7e6;
    a.channelIndex = 1037;
    tracker.update({ a }, 0);

    // Overlapping in frequency, but a different channel: adjacent stations must not be merged.
    auto b = at(103.70e6, 103.80e6);
    b.channelFrequency = 103.8e6;
    b.channelIndex = 1038;
    tracker.update({ a, b }, 100);

    CHECK(tracker.getTracked().size() == 2);
}

TEST_CASE("untagged signals still associate by overlap", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.params.activationMs = 0;

    // No raster configured, so channelFrequency stays zero and overlap decides.
    tracker.update({ at(14.1950e6, 14.1978e6) }, 0);
    uint64_t id = tracker.getTracked()[0].signal.id;
    tracker.update({ at(14.1951e6, 14.1979e6) }, 100);

    REQUIRE(tracker.getTracked().size() == 1);
    CHECK(tracker.getTracked()[0].signal.id == id);
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

TEST_CASE("channel identity survives a float round trip", "[detector][tracker]") {
    // Identity is the integer channel index precisely so that two ways of computing the same
    // channel frequency, differing in the last bit, cannot split one station into a new track
    // every frame.
    SignalTracker tracker;
    tracker.params.activationMs = 0;

    auto a = at(103.65e6, 103.75e6);
    a.channelFrequency = 103.7e6;
    a.channelIndex = 1037;

    auto b = at(103.65e6, 103.75e6);
    b.channelFrequency = std::nextafter(103.7e6, 1e9); // one ulp away
    b.channelIndex = 1037;
    REQUIRE(a.channelFrequency != b.channelFrequency);

    tracker.update({ a }, 0);
    uint64_t id = tracker.getTracked()[0].signal.id;
    tracker.update({ b }, 100);

    REQUIRE(tracker.getTracked().size() == 1);
    CHECK(tracker.getTracked()[0].signal.id == id);
}

TEST_CASE("a track keeps its channel when the centroid drifts across a boundary",
          "[detector][tracker]") {
    // A wide station whose centroid sits near a raster boundary snaps to the neighbouring slot in
    // the occasional frame. That is one station, and its tuned frequency must not jump.
    SignalTracker tracker;
    tracker.params.activationMs = 0;

    auto onChannel = at(87.725e6, 87.875e6);
    onChannel.channelFrequency = 87.8e6;
    onChannel.channelIndex = 878;
    tracker.update({ onChannel }, 0);

    uint64_t id = tracker.getTracked()[0].signal.id;

    // Next frame the same emission lands one slot up.
    auto flipped = at(87.825e6, 87.975e6);
    flipped.channelFrequency = 87.9e6;
    flipped.channelIndex = 879;
    tracker.update({ flipped }, 100);

    REQUIRE(tracker.getTracked().size() == 1);
    CHECK(tracker.getTracked()[0].signal.id == id);
    // Sticky: the track stays on the channel it was first assigned.
    CHECK(tracker.getTracked()[0].signal.channelIndex == 878);
    CHECK(tracker.getTracked()[0].signal.channelFrequency == Catch::Approx(87.8e6));
}

TEST_CASE("two real adjacent stations are not merged by stickiness", "[detector][tracker]") {
    // Both are present every frame, so each matches its own channel before anything is considered
    // by overlap, and neither can absorb the other.
    SignalTracker tracker;
    tracker.params.activationMs = 0;

    auto lower = at(87.725e6, 87.875e6);
    lower.channelFrequency = 87.8e6;
    lower.channelIndex = 878;
    auto upper = at(87.825e6, 87.975e6);
    upper.channelFrequency = 87.9e6;
    upper.channelIndex = 879;

    tracker.update({ lower, upper }, 0);
    REQUIRE(tracker.getTracked().size() == 2);

    tracker.update({ lower, upper }, 100);
    CHECK(tracker.getTracked().size() == 2);

    tracker.update({ upper, lower }, 200); // order must not matter
    CHECK(tracker.getTracked().size() == 2);
}

TEST_CASE("stickiness does not reach beyond the neighbouring channel", "[detector][tracker]") {
    SignalTracker tracker;
    tracker.params.activationMs = 0;

    auto a = at(87.725e6, 87.875e6);
    a.channelFrequency = 87.8e6;
    a.channelIndex = 878;
    tracker.update({ a }, 0);

    // Three slots away and overlapping is a different signal, not a boundary flip.
    auto far = at(87.825e6, 87.975e6);
    far.channelFrequency = 88.1e6;
    far.channelIndex = 881;
    tracker.update({ far }, 100);

    CHECK(tracker.getTracked().size() == 2);
}
