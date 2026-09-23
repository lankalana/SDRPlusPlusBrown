#pragma once
#include "../dsp/detector/detected_signal.h"
#include "../dsp/detector/noise_floor.h"
#include "../dsp/detector/signal_tracker.h"
#include "../dsp/detector/spectrum_detector.h"
#include "../dsp/detector/spectrum_integrator.h"
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
         * Width of the notch around the capture centre, in Hz. LO leakage there is a guaranteed
         * false detection, and because the detector integrates over a kernel a 40-60 dB spike
         * would otherwise pull in hundreds of bins either side.
         */
        double dcNotchHz = 1e3;

        // Per-evaluation false alarm probability, bounding the threshold from below for wide
        // kernels where marginDb alone would be looser than the statistics justify.
        double falseAlarmRate = 1e-7;

        /**
         * Drop detections that no enabled profile covers.
         *
         * This only controls what happens *outside* every profile. A profile that matches always
         * supplies its own channel raster, bandwidth limits and demodulator -- those are
         * properties of the band, not an optional mode, and gating them behind a flag made the
         * raster silently do nothing for anyone whose config predated it.
         */
        bool restrictToProfiles = false;
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
        // Where a receiver would be tuned, after sideband reference and channel rounding.
        double tuneFrequency = 0.0;
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

    /**
     * Where a receiver for this signal should be tuned: the profile's reference point (signal
     * centroid for centre-referenced modes, the lower edge for USB/CW, the upper edge for LSB),
     * rounded to the profile's channel raster.
     */
    double getTuneFrequency(const dsp::detector::DetectedSignal& signal) const;

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

    // Drop detections covered by an ignore rule.
    std::vector<dsp::detector::DetectedSignal> filterLocked(
        const std::vector<dsp::detector::DetectedSignal>& detections) const;

    /**
     * One detection pass per enabled profile over its overlap with the frame, highest priority
     * first, plus one fallback pass over bins no profile covers.
     *
     * Each profile supplies its own integration kernels, channel raster, merge gap and bandwidth
     * limits, because those are properties of the band: broadcast FM fills a 150 kHz channel on a
     * 100 kHz raster, airband is a narrow carrier inside a 25 kHz one, and a single set of global
     * numbers cannot serve both. A candidate is kept only if findFor() agrees it belongs to the
     * profile that produced it, so overlapping profiles cannot report the same signal twice.
     */
    std::vector<dsp::detector::DetectedSignal> detectAllLocked(double centerFrequency,
                                                               double spanHz,
                                                               double usableSpectrumRatio);

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

    /**
     * Running sum for detection averaging, in *linear power*.
     *
     * Averaging dB values estimates the log-mean, which sits below the mean power by up to
     * 2.5 dB. That was harmless while the floor was also a dB average and the two cancelled, but
     * the integrator divides by noise power, so both sides have to live in the linear domain.
     */
    std::vector<double> powerAccumulator;
    int averageCount = 0;

    // Reused across frames so a 64k-bin capture does not allocate twice per frame.
    std::vector<float> framePower;
    dsp::detector::SpectrumIntegrator integrator;
    std::vector<uint8_t> coverageScratch;
    // Frames behind the current integrator, for the CFAR look count.
    int lastFramesAveraged = 1;

    uint64_t floorVersion = 1;
};
