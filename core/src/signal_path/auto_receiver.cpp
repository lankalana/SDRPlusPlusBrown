#include <signal_path/auto_receiver.h>
#include <utils/flog.h>
#include <algorithm>
#include <cmath>

using namespace dsp::detector;

const char* toString(CalibrationInvalidationReason reason) {
    switch (reason) {
    case CalibrationInvalidationReason::NONE: return "None";
    case CalibrationInvalidationReason::MANUAL: return "Requested by user";
    case CalibrationInvalidationReason::NOT_ENABLED: return "Automatic reception disabled";
    case CalibrationInvalidationReason::CENTER_FREQUENCY: return "Center frequency changed";
    case CalibrationInvalidationReason::SAMPLE_RATE: return "Sample rate changed";
    case CalibrationInvalidationReason::DECIMATION: return "Decimation changed";
    case CalibrationInvalidationReason::SOURCE_DEVICE: return "Source changed";
    case CalibrationInvalidationReason::SPECTRUM_SHAPE: return "FFT size changed";
    }
    return "Unknown";
}

AutoReceiverManager::AutoReceiverManager() {
    applyConfigLocked();
}

void AutoReceiverManager::setConfig(const Config& newConfig) {
    std::lock_guard<std::recursive_mutex> lck(mtx);

    bool wasEnabled = config.enabled;
    // Changing how long calibration runs, or how it is thresholded from raw statistics, means the
    // existing model was not built the way the user is now asking for.
    bool needsRecalibration = (newConfig.calibrationSeconds != config.calibrationSeconds);

    config = newConfig;
    applyConfigLocked();

    if (!config.enabled && wasEnabled) {
        invalidateCalibration(CalibrationInvalidationReason::NOT_ENABLED);
    }
    else if (config.enabled && needsRecalibration) {
        invalidateCalibration(CalibrationInvalidationReason::MANUAL);
    }
}

void AutoReceiverManager::applyConfigLocked() {
    calibration.setMinimumMarginDb(config.minSnrDb);
    calibration.setDeviationMultiplier(config.deviationMultiplier);
    tracker.params.activationMs = config.activationMs;
    tracker.params.releaseMs = config.releaseMs;
}

AutoReceiverManager::Config AutoReceiverManager::getConfig() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return config;
}

void AutoReceiverManager::setEnabled(bool enabled) {
    Config c = getConfig();
    if (c.enabled == enabled) { return; }
    c.enabled = enabled;
    setConfig(c);
}

bool AutoReceiverManager::isEnabled() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return config.enabled;
}

void AutoReceiverManager::invalidateCalibration(CalibrationInvalidationReason reason) {
    std::lock_guard<std::recursive_mutex> lck(mtx);

    lastInvalidationReason = reason;
    calibration.reset();
    tracker.clear();
    calibratedBinCount = 0;
    calibratedFrameRate = 0.0;

    flog::info("Automatic reception: calibration invalidated ({})", toString(reason));
}

void AutoReceiverManager::updateSourceState(const SourceManager::State& newState) {
    std::lock_guard<std::recursive_mutex> lck(mtx);

    if (!haveSourceState) {
        knownSourceState = newState;
        haveSourceState = true;
        return;
    }
    if (knownSourceState == newState) { return; }

    // Report the most specific reason so the UI can explain itself.
    CalibrationInvalidationReason reason = CalibrationInvalidationReason::MANUAL;
    if (knownSourceState.sourceName != newState.sourceName) {
        reason = CalibrationInvalidationReason::SOURCE_DEVICE;
    }
    else if (knownSourceState.sampleRate != newState.sampleRate) {
        reason = CalibrationInvalidationReason::SAMPLE_RATE;
    }
    else if (knownSourceState.decimation != newState.decimation) {
        reason = CalibrationInvalidationReason::DECIMATION;
    }
    else if (knownSourceState.centerFrequency != newState.centerFrequency) {
        reason = CalibrationInvalidationReason::CENTER_FREQUENCY;
    }

    knownSourceState = newState;
    invalidateCalibration(reason);
}

void AutoReceiverManager::restartCalibrationLocked(int binCount, double frameRate) {
    double rate = (frameRate > 0.0) ? frameRate : 1.0;
    int framesRequired = (int)std::lround(config.calibrationSeconds * rate);
    framesRequired = std::max<int>(framesRequired, 1);

    calibration.configure(binCount, framesRequired);
    applyConfigLocked();
    calibration.begin();

    calibratedBinCount = binCount;
    calibratedFrameRate = rate;

    flog::info("Automatic reception: calibrating {} bins over {} frames ({} s)",
               binCount, framesRequired, config.calibrationSeconds);
}

void AutoReceiverManager::onFFTFrame(const float* fft, int binCount, double centerFrequency,
                                     double spanHz, double usableSpectrumRatio, double frameRate,
                                     uint64_t nowMs) {
    std::vector<DetectedSignal> activeSignals;
    bool emitUpdate = false;

    {
        std::lock_guard<std::recursive_mutex> lck(mtx);

        if (!config.enabled) { return; }
        if (fft == nullptr || binCount <= 0 || spanHz <= 0.0) { return; }

        // A change in FFT size remaps every bin, so the model cannot be carried over.
        if (calibratedBinCount != 0 && calibratedBinCount != binCount) {
            invalidateCalibration(CalibrationInvalidationReason::SPECTRUM_SHAPE);
        }

        if (calibration.getState() == CalibrationState::UNCALIBRATED) {
            restartCalibrationLocked(binCount, frameRate);
        }

        if (calibration.getState() == CalibrationState::CALIBRATING) {
            // While calibrating we deliberately produce no detections at all.
            calibration.addFrame(fft, binCount);
            return;
        }

        if (!calibration.isReady()) { return; }

        DetectionParams params;
        params.minBins = config.minDetectionBins;
        params.maxGapBins = config.maxGapBins;
        params.usableSpectrumRatio = usableSpectrumRatio;

        auto detections = detectSignals(fft, binCount, calibration, centerFrequency, spanHz, params);
        tracker.update(detections, nowMs);

        activeSignals = tracker.getActiveSignals();
        emitUpdate = true;
    }

    // Emitted outside the lock: handlers may call back into this object.
    if (emitUpdate) { onDetectionUpdate.emit(activeSignals); }
}

CalibrationState AutoReceiverManager::getCalibrationState() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return calibration.getState();
}

float AutoReceiverManager::getCalibrationProgress() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return calibration.getProgress();
}

CalibrationInvalidationReason AutoReceiverManager::getLastInvalidationReason() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return lastInvalidationReason;
}

std::vector<TrackedSignal> AutoReceiverManager::getTrackedSignals() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return tracker.getTracked();
}

int AutoReceiverManager::getActiveSignalCount() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    int count = 0;
    for (const auto& t : tracker.getTracked()) {
        if (t.state == SignalState::ACTIVE) { count++; }
    }
    return count;
}
