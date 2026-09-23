#pragma once
#include "../dsp/detector/detected_signal.h"
#include "../dsp/detector/noise_calibration.h"
#include "../dsp/detector/signal_tracker.h"
#include "../dsp/detector/spectrum_detector.h"
#include "source.h"
#include <utils/event.h>
#include <mutex>
#include <string>
#include <vector>

/**
 * Why a calibration had to be thrown away. Anything that changes what the noise floor of a given
 * FFT bin means invalidates the model.
 */
enum class CalibrationInvalidationReason {
    NONE,
    MANUAL,
    NOT_ENABLED,
    CENTER_FREQUENCY,
    SAMPLE_RATE,
    DECIMATION,
    SOURCE_DEVICE,
    SPECTRUM_SHAPE
};

const char* toString(CalibrationInvalidationReason reason);

/**
 * Owns automatic reception.
 *
 * At this stage it covers noise-floor calibration, structured detection and signal tracking.
 * Receiver allocation, recording and frequency history hang off the same object later.
 *
 * The one invariant that shapes everything here: the user owns the SDR centre frequency. This
 * class only ever observes the spectrum that is already being captured, and it never calls
 * sigpath::sourceManager.tune().
 *
 * onFFTFrame() runs on the DSP thread; every getter is safe to call from the GUI thread.
 */
class AutoReceiverManager {
public:
    struct Config {
        // Off by default: an existing installation should see no change until the user opts in.
        bool enabled = false;

        double calibrationSeconds = 15.0;
        float minSnrDb = 10.0f;
        float deviationMultiplier = 3.0f;

        uint64_t activationMs = 300;
        uint64_t releaseMs = 2000;

        int minDetectionBins = 2;
        int maxGapBins = 1;
    };

    AutoReceiverManager();

    void setConfig(const Config& config);
    Config getConfig() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    /**
     * Feed one FFT frame of the currently captured spectrum, in dB with bin 0 at the lowest
     * frequency. `frameRate` is how often frames arrive, used to translate the calibration
     * duration into a frame count. `nowMs` must be monotonically non-decreasing.
     *
     * Called from the DSP thread.
     */
    void onFFTFrame(const float* fft, int binCount, double centerFrequency, double spanHz,
                    double usableSpectrumRatio, double frameRate, uint64_t nowMs);

    /**
     * Throw away the noise model and all tracked signals, and start over if enabled. Detection
     * produces nothing until the new calibration completes.
     */
    void invalidateCalibration(CalibrationInvalidationReason reason);

    // Compare against the state the current model was built under and invalidate if it differs.
    void updateSourceState(const SourceManager::State& state);

    dsp::detector::CalibrationState getCalibrationState() const;
    float getCalibrationProgress() const;
    CalibrationInvalidationReason getLastInvalidationReason() const;

    // Snapshots for the GUI.
    std::vector<dsp::detector::TrackedSignal> getTrackedSignals() const;
    int getActiveSignalCount() const;

    // Emitted after every frame that ran detection, with the currently ACTIVE signals.
    Event<std::vector<dsp::detector::DetectedSignal>> onDetectionUpdate;

private:
    void restartCalibrationLocked(int binCount, double frameRate);
    void applyConfigLocked();

    mutable std::recursive_mutex mtx;

    Config config;
    dsp::detector::NoiseFloorCalibration calibration;
    dsp::detector::SignalTracker tracker;

    CalibrationInvalidationReason lastInvalidationReason = CalibrationInvalidationReason::NONE;

    // Shape the current model was built for; a change means the model no longer maps to bins.
    int calibratedBinCount = 0;
    double calibratedFrameRate = 0.0;

    SourceManager::State knownSourceState;
    bool haveSourceState = false;
};
