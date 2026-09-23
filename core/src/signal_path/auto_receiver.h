#pragma once
#include "../dsp/detector/detected_signal.h"
#include "../dsp/detector/noise_floor.h"
#include "../dsp/detector/signal_tracker.h"
#include "../dsp/detector/spectrum_detector.h"
#include "source.h"
#include "reception_profile.h"
#include "ignore_rules.h"
#include <utils/event.h>
#include <mutex>
#include <string>
#include <vector>

/**
 * Owns automatic reception.
 *
 * At this stage it covers the detection floor, structured detection and signal tracking.
 * Receiver allocation, recording and frequency history hang off the same object later.
 *
 * The one invariant that shapes everything here: the user owns the SDR centre frequency. This
 * class only ever observes the spectrum that is already being captured, and it never calls
 * sigpath::sourceManager.tune().
 *
 * Nothing measures itself. The floor is either a flat level the user set or the result of a
 * measurement the user asked for; retuning never silently starts one.
 *
 * onFFTFrame() runs on the DSP thread; every getter is safe to call from the GUI thread.
 */
class AutoReceiverManager {
public:
    struct Config {
        // Off by default: an existing installation should see no change until the user opts in.
        bool enabled = false;

        dsp::detector::NoiseFloorMode floorMode = dsp::detector::NoiseFloorMode::MANUAL;
        float manualFloorDb = dsp::detector::NoiseFloorModel::DEFAULT_MANUAL_FLOOR_DB;
        float marginDb = dsp::detector::NoiseFloorModel::DEFAULT_MARGIN_DB;

        double spectralWindowHz = dsp::detector::NoiseFloorModel::DEFAULT_WINDOW_HZ;
        float spectralPercentile = dsp::detector::NoiseFloorModel::DEFAULT_PERCENTILE;
        double measurementSeconds = 1.0;

        uint64_t activationMs = 300;
        uint64_t releaseMs = 2000;

        /**
         * Frames averaged before thresholding. A single frame of a wideband FM signal is deeply
         * notched and fragments into many narrow detections; averaging a few frames recovers the
         * channel shape.
         */
        int detectionAveragingFrames = 4;

        double minBandwidthHz = 0.0;
        double maxBandwidthHz = 0.0;
        double mergeGapHz = 0.0;
        int minDetectionBins = 2;

        /**
         * When set, only detections inside an enabled reception profile are kept, and each
         * profile's own bandwidth limits apply there. This is the per-band way to say "on the FM
         * band I only care about 100-400 kHz signals" without touching the global limits.
         */
        bool useProfiles = false;
    };

    // A tracked signal together with what the configuration says about it.
    struct ClassifiedSignal {
        dsp::detector::TrackedSignal tracked;
        bool ignored = false;
        std::string ignoreReason;
        // Empty when no profile matches, or when profiles are not in use.
        std::string profileName;
        ProfileDemod demod = ProfileDemod::NFM;
        double receiverBandwidth = 0.0;
        bool hasProfile = false;
    };

    AutoReceiverManager();

    void setConfig(const Config& config);
    Config getConfig() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    /**
     * Feed one FFT frame of the currently captured spectrum, in dB with bin 0 at the lowest
     * frequency. `nowMs` must be monotonically non-decreasing.
     *
     * Called from the DSP thread.
     */
    void onFFTFrame(const float* fft, int binCount, double centerFrequency, double spanHz,
                    double usableSpectrumRatio, double frameRate, uint64_t nowMs);

    // Start a floor measurement over the configured duration. Switches the mode to MEASURED.
    void startMeasurement();
    void cancelMeasurement();
    dsp::detector::MeasurementState getMeasurementState() const;
    float getMeasurementProgress() const;

    /**
     * Set the flat manual level from the most recently seen frame, so the user can put the floor
     * where the spectrum actually is instead of guessing a number.
     * Returns false if no frame has been seen yet.
     */
    bool setManualFloorFromSpectrum();

    // Forget a measured floor and all tracked signals. A manual floor is a user setting and is
    // left alone.
    void resetFloor();

    void updateSourceState(const SourceManager::State& state);

    bool isFloorUsable() const;
    dsp::detector::NoiseFloorMode getFloorMode() const;

    /**
     * Snapshot of the floor and threshold for drawing. `floorDb` and `thresholdDb` are uniform
     * samples across [lowFrequency, highFrequency]; for a flat manual floor both have a single
     * entry. `version` changes whenever the curve changes, so the GUI can avoid rebuilding it.
     */
    struct FloorCurve {
        bool usable = false;
        bool flat = true;
        double lowFrequency = 0.0;
        double highFrequency = 0.0;
        std::vector<float> floorDb;
        std::vector<float> thresholdDb;
        uint64_t version = 0;
    };

    FloorCurve getFloorCurve(int maxSamples = 512) const;
    uint64_t getFloorVersion() const;

    void setProfiles(const ReceptionProfileSet& profiles);
    ReceptionProfileSet getProfiles() const;

    void setIgnoreRules(const IgnoreRuleSet& rules);
    IgnoreRuleSet getIgnoreRules() const;

    // Add an ignore rule covering a tracked signal, and drop it from tracking straight away.
    void ignoreSignal(uint64_t signalId, const std::string& reason, double paddingHz);

    // Snapshots for the GUI.
    std::vector<dsp::detector::TrackedSignal> getTrackedSignals() const;
    std::vector<ClassifiedSignal> getClassifiedSignals() const;
    int getActiveSignalCount() const;

    // Emitted after every frame that ran detection, with the currently ACTIVE signals.
    Event<std::vector<dsp::detector::DetectedSignal>> onDetectionUpdate;

private:
    void applyConfigLocked();
    void bumpFloorVersionLocked();

    mutable std::recursive_mutex mtx;

    // Drop detections that no enabled profile accepts, and those covered by an ignore rule.
    std::vector<dsp::detector::DetectedSignal> filterLocked(
        const std::vector<dsp::detector::DetectedSignal>& detections) const;

    Config config;
    ReceptionProfileSet profiles;
    IgnoreRuleSet ignoreRules;
    dsp::detector::NoiseFloorModel floor;
    dsp::detector::SignalTracker tracker;

    // Geometry of the last frame seen, so the GUI can be told where the floor applies.
    int lastBinCount = 0;
    double lastCenterFrequency = 0.0;
    double lastSpanHz = 0.0;
    double lastFrameRate = 0.0;
    std::vector<float> lastFrame;

    // Running sum for detection averaging.
    std::vector<double> averageAccumulator;
    int averageCount = 0;

    uint64_t floorVersion = 1;
};
