// AutoReceiverManager: the detection floor and its relationship with source changes.
//
// The invariant under test throughout is the one the whole design hangs off: the user owns the
// SDR centre frequency. The manager observes the captured spectrum and never asks the source to
// move -- and, since the floor is either set by the user or measured on request, it never starts
// measuring by itself either.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <signal_path/auto_receiver.h>

#include <cmath>
#include <vector>

using namespace dsp::detector;

namespace {
    const int BINS = 64;
    const double CENTER = 100e6;
    const double SPAN = 1e6; // 15.625 kHz per bin
    const double FRAME_RATE = 20.0;
    const double USABLE = 1.0;

    AutoReceiverManager::Config manualConfig() {
        AutoReceiverManager::Config c;
        c.enabled = true;
        c.floorMode = NoiseFloorMode::MANUAL;
        c.manualFloorDb = -100.0f;
        c.marginDb = 10.0f;
        c.activationMs = 300;
        c.releaseMs = 2000;
        c.detectionAveragingFrames = 1;
        c.minDetectionBins = 2;
        return c;
    }

    std::vector<float> quietFrame() { return std::vector<float>(BINS, -100.0f); }

    std::vector<float> frameWithSignal() {
        auto f = quietFrame();
        for (int b = 30; b <= 35; b++) { f[b] = -40.0f; }
        return f;
    }

    uint64_t feed(AutoReceiverManager& mgr, const std::vector<float>& frame, int count,
                  uint64_t startMs, double center = CENTER) {
        uint64_t t = startMs;
        for (int i = 0; i < count; i++) {
            mgr.onFFTFrame(frame.data(), BINS, center, SPAN, USABLE, FRAME_RATE, t);
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

    feed(mgr, frameWithSignal(), 100, 0);
    CHECK(mgr.getTrackedSignals().empty());
}

TEST_CASE("a manual floor detects from the very first frame", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(manualConfig());

    // No warm-up, no calibration: the user already said where the floor is.
    feed(mgr, frameWithSignal(), 1, 0);
    REQUIRE(mgr.getTrackedSignals().size() == 1);
    CHECK(mgr.getTrackedSignals()[0].state == SignalState::CANDIDATE);
    CHECK(mgr.isFloorUsable());
}

TEST_CASE("a manual floor confirms a signal after the activation delay", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(manualConfig());

    feed(mgr, frameWithSignal(), 8, 0);
    CHECK(mgr.getActiveSignalCount() == 1);
    CHECK(mgr.getTrackedSignals()[0].signal.snrDb == Catch::Approx(60.0f));
}

TEST_CASE("raising the manual floor above a signal hides it", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    mgr.setConfig(c);
    feed(mgr, frameWithSignal(), 8, 0);
    REQUIRE(mgr.getActiveSignalCount() == 1);

    // The signal peaks at -40 dB; put the threshold above it.
    c.manualFloorDb = -20.0f;
    mgr.setConfig(c);
    feed(mgr, frameWithSignal(), 60, 1000);
    CHECK(mgr.getActiveSignalCount() == 0);
}

TEST_CASE("nothing is measured unless the user asks", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.floorMode = NoiseFloorMode::MEASURED;
    mgr.setConfig(c);

    feed(mgr, frameWithSignal(), 200, 0);
    CHECK(mgr.getMeasurementState() == MeasurementState::IDLE);
    CHECK_FALSE(mgr.isFloorUsable());
    CHECK(mgr.getTrackedSignals().empty());
}

TEST_CASE("a requested measurement completes and then detects", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.measurementSeconds = 0.5; // 10 frames at 20 fps
    c.spectralWindowHz = SPAN / 4.0;
    mgr.setConfig(c);

    // The manager needs to have seen a frame before it knows the spectrum shape.
    uint64_t t = feed(mgr, quietFrame(), 1, 0);
    mgr.startMeasurement();
    CHECK(mgr.getMeasurementState() == MeasurementState::MEASURING);

    t = feed(mgr, quietFrame(), 9, t);
    CHECK(mgr.getMeasurementState() == MeasurementState::MEASURING);
    CHECK(mgr.getTrackedSignals().empty());

    t = feed(mgr, quietFrame(), 1, t);
    CHECK(mgr.getMeasurementState() == MeasurementState::READY);
    CHECK(mgr.isFloorUsable());

    t = feed(mgr, frameWithSignal(), 8, t);
    CHECK(mgr.getActiveSignalCount() == 1);
}

TEST_CASE("a measurement cannot start before any spectrum has arrived", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(manualConfig());
    mgr.startMeasurement();
    CHECK(mgr.getMeasurementState() == MeasurementState::IDLE);
}

TEST_CASE("a manual floor survives a retune", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(manualConfig());
    mgr.updateSourceState(stateAt(CENTER));
    feed(mgr, frameWithSignal(), 8, 0);
    REQUIRE(mgr.getActiveSignalCount() == 1);

    // A flat level means something at any tuning, so detection keeps working immediately.
    mgr.updateSourceState(stateAt(CENTER + 5e6));
    CHECK(mgr.isFloorUsable());
    feed(mgr, frameWithSignal(), 8, 1000, CENTER + 5e6);
    CHECK(mgr.getActiveSignalCount() == 1);
}

TEST_CASE("a measured floor is discarded on a retune", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.measurementSeconds = 0.25; // 5 frames
    mgr.setConfig(c);
    mgr.updateSourceState(stateAt(CENTER));

    uint64_t t = feed(mgr, quietFrame(), 1, 0);
    mgr.startMeasurement();
    t = feed(mgr, quietFrame(), 5, t);
    REQUIRE(mgr.getMeasurementState() == MeasurementState::READY);

    // A per-bin floor is tied to the spectrum it was measured on.
    mgr.updateSourceState(stateAt(CENTER + 5e6));
    CHECK(mgr.getMeasurementState() == MeasurementState::IDLE);
    CHECK_FALSE(mgr.isFloorUsable());

    // And it does not quietly re-measure itself.
    feed(mgr, frameWithSignal(), 100, 2000, CENTER + 5e6);
    CHECK(mgr.getMeasurementState() == MeasurementState::IDLE);
    CHECK(mgr.getTrackedSignals().empty());
}

TEST_CASE("setManualFloorFromSpectrum uses the last frame seen", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.manualFloorDb = -10.0f; // deliberately useless
    mgr.setConfig(c);

    CHECK_FALSE(mgr.setManualFloorFromSpectrum()); // nothing seen yet

    feed(mgr, frameWithSignal(), 1, 0);
    REQUIRE(mgr.setManualFloorFromSpectrum());
    // The quiet part of the frame is at -100 dB, and only 6 of 64 bins are occupied.
    CHECK(mgr.getConfig().manualFloorDb == Catch::Approx(-100.0f).margin(1.0));
    CHECK(mgr.getFloorMode() == NoiseFloorMode::MANUAL);
}

TEST_CASE("detection averaging smooths a notched signal into one detection", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.detectionAveragingFrames = 4;
    c.activationMs = 0;
    mgr.setConfig(c);

    // A wide signal whose notch moves between frames, as broadcast FM does. Averaged, the whole
    // channel sits above the floor; frame by frame it would break into pieces.
    std::vector<std::vector<float>> frames;
    for (int k = 0; k < 4; k++) {
        auto f = quietFrame();
        for (int b = 20; b <= 43; b++) { f[b] = -50.0f; }
        for (int b = 22 + (k * 5); b <= 24 + (k * 5); b++) { f[b] = -100.0f; }
        frames.push_back(f);
    }

    uint64_t t = 0;
    for (int k = 0; k < 4; k++) {
        mgr.onFFTFrame(frames[k].data(), BINS, CENTER, SPAN, USABLE, FRAME_RATE, t);
        t += 50;
    }

    REQUIRE(mgr.getTrackedSignals().size() == 1);
    CHECK(mgr.getTrackedSignals()[0].signal.bandwidth == Catch::Approx(24 * SPAN / BINS));
}

TEST_CASE("the floor curve describes a flat manual level", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(manualConfig());

    auto curve = mgr.getFloorCurve();
    CHECK(curve.usable);
    CHECK(curve.flat);
    REQUIRE(curve.floorDb.size() == 1);
    CHECK(curve.floorDb[0] == -100.0f);
    CHECK(curve.thresholdDb[0] == -90.0f);
}

TEST_CASE("the floor curve describes a measured per-bin floor", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.measurementSeconds = 0.25;
    c.spectralWindowHz = SPAN / 4.0;
    mgr.setConfig(c);

    uint64_t t = feed(mgr, quietFrame(), 1, 0);
    mgr.startMeasurement();
    feed(mgr, quietFrame(), 5, t);
    REQUIRE(mgr.getMeasurementState() == MeasurementState::READY);

    auto curve = mgr.getFloorCurve(128);
    CHECK(curve.usable);
    CHECK_FALSE(curve.flat);
    CHECK(curve.floorDb.size() == 64); // capped at the bin count
    CHECK(curve.lowFrequency == Catch::Approx(CENTER - (SPAN / 2.0)));
    CHECK(curve.highFrequency == Catch::Approx(CENTER + (SPAN / 2.0)));
    for (size_t i = 0; i < curve.thresholdDb.size(); i++) {
        CHECK(curve.thresholdDb[i] == Catch::Approx(curve.floorDb[i] + 10.0f));
    }
}

TEST_CASE("the floor version changes only when the model does", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(manualConfig());

    // The first frame establishes the spectrum geometry the curve is drawn against, so it does
    // bump the version. Steady-state frames afterwards must not.
    feed(mgr, frameWithSignal(), 1, 0);
    uint64_t v0 = mgr.getFloorVersion();

    feed(mgr, frameWithSignal(), 10, 50);
    CHECK(mgr.getFloorVersion() == v0);

    auto c = manualConfig();
    c.manualFloorDb = -95.0f;
    mgr.setConfig(c);
    CHECK(mgr.getFloorVersion() != v0);
}

TEST_CASE("an ignore rule suppresses a signal entirely", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(manualConfig());

    // Bins 30..35 of a 1 MHz span centred at 100 MHz.
    double binHz = SPAN / BINS;
    double low = CENTER - (SPAN / 2.0) + (30 * binHz);
    double high = CENTER - (SPAN / 2.0) + (36 * binHz);

    IgnoreRuleSet rules;
    rules.addForSignal(low, high, "test", 0.0);
    mgr.setIgnoreRules(rules);

    feed(mgr, frameWithSignal(), 20, 0);
    CHECK(mgr.getTrackedSignals().empty());
    CHECK(mgr.getActiveSignalCount() == 0);
}

TEST_CASE("ignoreSignal creates a rule from a tracked signal", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(manualConfig());
    feed(mgr, frameWithSignal(), 8, 0);
    REQUIRE(mgr.getActiveSignalCount() == 1);

    uint64_t id = mgr.getTrackedSignals()[0].signal.id;
    mgr.ignoreSignal(id, "operator", 1e3);

    CHECK(mgr.getIgnoreRules().rules.size() == 1);
    CHECK(mgr.getIgnoreRules().rules[0].reason == "operator");

    // It stays gone as long as the rule is there.
    feed(mgr, frameWithSignal(), 20, 1000);
    CHECK(mgr.getTrackedSignals().empty());
}

TEST_CASE("profiles gate detection when enabled", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.restrictToProfiles = true;
    mgr.setConfig(c);

    ReceptionProfileSet set;
    ReceptionProfile p;
    p.name = "Elsewhere";
    p.minFrequency = 400e6;
    p.maxFrequency = 410e6;
    set.profiles.push_back(p);
    mgr.setProfiles(set);

    // The signal is at 100 MHz, nowhere near the only profile.
    feed(mgr, frameWithSignal(), 20, 0);
    CHECK(mgr.getTrackedSignals().empty());

    // Cover it, and it comes through.
    set.profiles[0].minFrequency = CENTER - SPAN;
    set.profiles[0].maxFrequency = CENTER + SPAN;
    mgr.setProfiles(set);
    feed(mgr, frameWithSignal(), 20, 2000);
    CHECK(mgr.getActiveSignalCount() == 1);
}

TEST_CASE("a profile's bandwidth limits reject a too-narrow detection", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.restrictToProfiles = true;
    mgr.setConfig(c);

    ReceptionProfileSet set;
    ReceptionProfile p;
    p.name = "Wide only";
    p.minFrequency = CENTER - SPAN;
    p.maxFrequency = CENTER + SPAN;
    // The test signal is 6 bins = ~94 kHz wide.
    p.minDetectionBandwidth = 200e3;
    set.profiles.push_back(p);
    mgr.setProfiles(set);

    feed(mgr, frameWithSignal(), 20, 0);
    CHECK(mgr.getTrackedSignals().empty());

    set.profiles[0].minDetectionBandwidth = 50e3;
    mgr.setProfiles(set);
    feed(mgr, frameWithSignal(), 20, 2000);
    CHECK(mgr.getActiveSignalCount() == 1);
}

TEST_CASE("classified signals report the matching profile", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(manualConfig());

    ReceptionProfileSet set;
    ReceptionProfile p;
    p.name = "Here";
    p.minFrequency = CENTER - SPAN;
    p.maxFrequency = CENTER + SPAN;
    p.demod = ProfileDemod::WFM;
    p.bandwidth = 150e3;
    set.profiles.push_back(p);
    mgr.setProfiles(set);

    feed(mgr, frameWithSignal(), 8, 0);
    auto classified = mgr.getClassifiedSignals();
    REQUIRE(classified.size() == 1);
    CHECK(classified[0].hasProfile);
    CHECK(classified[0].profileName == "Here");
    CHECK(classified[0].demod == ProfileDemod::WFM);
    CHECK(classified[0].receiverBandwidth == Catch::Approx(150e3));
    CHECK_FALSE(classified[0].ignored);
}

TEST_CASE("a profile's raster applies without restrictToProfiles", "[auto_receiver]") {
    // Regression: the raster used to be gated behind the same flag that drops uncovered
    // detections, so anyone whose config had that flag off got no rounding and no fragment
    // merging at all -- detections jumped around and never landed on channel.
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.restrictToProfiles = false;
    c.activationMs = 0;
    mgr.setConfig(c);

    ReceptionProfileSet set;
    ReceptionProfile p;
    p.name = "FM";
    p.minFrequency = CENTER - SPAN;
    p.maxFrequency = CENTER + SPAN;
    p.demod = ProfileDemod::WFM;
    p.bandwidth = 150e3;
    p.frequencyStep = 100e3;
    set.profiles.push_back(p);
    mgr.setProfiles(set);

    feed(mgr, frameWithSignal(), 4, 0);
    REQUIRE(mgr.getTrackedSignals().size() == 1);
    CHECK(mgr.getTrackedSignals()[0].signal.channelFrequency != 0.0);

    auto classified = mgr.getClassifiedSignals();
    REQUIRE(classified.size() == 1);
    double tune = classified[0].tuneFrequency;
    CHECK(std::fabs(tune - (std::round(tune / 100e3) * 100e3)) < 1.0);
}

TEST_CASE("an uncovered detection survives unless restricted", "[auto_receiver]") {
    ReceptionProfileSet set;
    ReceptionProfile p;
    p.name = "Elsewhere";
    p.minFrequency = 400e6;
    p.maxFrequency = 410e6;
    set.profiles.push_back(p);

    SECTION("kept by default") {
        AutoReceiverManager mgr;
        auto c = manualConfig();
        c.restrictToProfiles = false;
        mgr.setConfig(c);
        mgr.setProfiles(set);

        feed(mgr, frameWithSignal(), 8, 0);
        CHECK(mgr.getActiveSignalCount() == 1);
    }

    SECTION("dropped when restricted") {
        AutoReceiverManager mgr;
        auto c = manualConfig();
        c.restrictToProfiles = true;
        mgr.setConfig(c);
        mgr.setProfiles(set);

        feed(mgr, frameWithSignal(), 8, 0);
        CHECK(mgr.getTrackedSignals().empty());
    }
}

TEST_CASE("a channel raster merges fragments of one transmission", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.restrictToProfiles = true;
    c.activationMs = 0;
    mgr.setConfig(c);

    ReceptionProfileSet set;
    ReceptionProfile p;
    p.name = "Rastered";
    p.minFrequency = CENTER - SPAN;
    p.maxFrequency = CENTER + SPAN;
    p.demod = ProfileDemod::WFM;
    p.bandwidth = 150e3;
    p.frequencyStep = SPAN / 4.0; // 250 kHz raster
    set.profiles.push_back(p);
    mgr.setProfiles(set);

    // Two separate above-threshold runs, both inside the same 250 kHz channel.
    auto frame = quietFrame();
    for (int b = 28; b <= 30; b++) { frame[b] = -40.0f; }
    for (int b = 34; b <= 36; b++) { frame[b] = -40.0f; }

    feed(mgr, frame, 4, 0);

    // One station, not two.
    REQUIRE(mgr.getTrackedSignals().size() == 1);
    const auto& sig = mgr.getTrackedSignals()[0].signal;
    CHECK(sig.channelFrequency != 0.0);
    // The merged extent spans both fragments.
    CHECK(sig.bandwidth >= 8 * (SPAN / BINS));
}

TEST_CASE("the tune frequency is snapped onto the channel raster", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.restrictToProfiles = true;
    c.activationMs = 0;
    mgr.setConfig(c);

    ReceptionProfileSet set;
    ReceptionProfile p;
    p.name = "FM";
    p.minFrequency = CENTER - SPAN;
    p.maxFrequency = CENTER + SPAN;
    p.demod = ProfileDemod::WFM;
    p.bandwidth = 150e3;
    p.frequencyStep = 100e3;
    set.profiles.push_back(p);
    mgr.setProfiles(set);

    feed(mgr, frameWithSignal(), 4, 0);
    auto classified = mgr.getClassifiedSignals();
    REQUIRE(classified.size() == 1);

    // Whatever the detected centre, the receiver goes on channel.
    double tune = classified[0].tuneFrequency;
    CHECK(std::fabs(tune - (std::round(tune / 100e3) * 100e3)) < 1.0);
    CHECK(std::fabs(tune - classified[0].tracked.signal.centroidFrequency) <= 50e3);
}

TEST_CASE("SSB tunes to the signal edge, not its centre", "[auto_receiver]") {
    AutoReceiverManager mgr;
    auto c = manualConfig();
    c.restrictToProfiles = true;
    c.activationMs = 0;
    mgr.setConfig(c);

    ReceptionProfileSet set;
    ReceptionProfile usb;
    usb.name = "USB";
    usb.minFrequency = CENTER - SPAN;
    usb.maxFrequency = CENTER + SPAN;
    usb.demod = ProfileDemod::USB;
    usb.bandwidth = 2.8e3;
    set.profiles.push_back(usb);
    mgr.setProfiles(set);

    feed(mgr, frameWithSignal(), 4, 0);
    auto classified = mgr.getClassifiedSignals();
    REQUIRE(classified.size() == 1);
    CHECK(classified[0].tuneFrequency ==
          Catch::Approx(classified[0].tracked.signal.lowerFrequency));

    // LSB takes the other edge.
    set.profiles[0].demod = ProfileDemod::LSB;
    mgr.setProfiles(set);
    feed(mgr, frameWithSignal(), 4, 5000);
    classified = mgr.getClassifiedSignals();
    REQUIRE(classified.size() == 1);
    CHECK(classified[0].tuneFrequency ==
          Catch::Approx(classified[0].tracked.signal.upperFrequency));
}

TEST_CASE("onDetectionUpdate reports the active signals", "[auto_receiver]") {
    AutoReceiverManager mgr;
    mgr.setConfig(manualConfig());

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

    feed(mgr, frameWithSignal(), 10, 0);
    CHECK(updateCount == 10);
    REQUIRE(lastUpdate.size() == 1);
    CHECK(lastUpdate[0].snrDb == Catch::Approx(60.0f));

    mgr.onDetectionUpdate.unbindHandler(&handler);
}
