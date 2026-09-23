#include <signal_path/auto_receiver.h>
#include <utils/flog.h>
#include <algorithm>
#include <cmath>

using namespace dsp::detector;

AutoReceiverManager::AutoReceiverManager() {
    applyConfigLocked();
}

void AutoReceiverManager::setConfig(const Config& newConfig) {
    std::lock_guard<std::recursive_mutex> lck(mtx);

    bool wasEnabled = config.enabled;
    config = newConfig;
    applyConfigLocked();

    if (!config.enabled && wasEnabled) {
        tracker.clear();
        averageAccumulator.clear();
        averageCount = 0;
    }
    bumpFloorVersionLocked();
}

void AutoReceiverManager::applyConfigLocked() {
    floor.setMode(config.floorMode);
    floor.setManualFloorDb(config.manualFloorDb);
    floor.setMarginDb(config.marginDb);
    floor.setSpectralWindowHz(config.spectralWindowHz);
    floor.setSpectralPercentile(config.spectralPercentile);
    tracker.params.activationMs = config.activationMs;
    tracker.params.releaseMs = config.releaseMs;
}

void AutoReceiverManager::bumpFloorVersionLocked() { floorVersion++; }

uint64_t AutoReceiverManager::getFloorVersion() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return floorVersion;
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

void AutoReceiverManager::startMeasurement() {
    std::lock_guard<std::recursive_mutex> lck(mtx);

    if (lastBinCount <= 0) {
        flog::warn("Automatic reception: no spectrum yet, cannot measure the noise floor");
        return;
    }
    double rate = (lastFrameRate > 0.0) ? lastFrameRate : 20.0;
    int frames = std::max<int>(1, (int)std::lround(config.measurementSeconds * rate));

    config.floorMode = NoiseFloorMode::MEASURED;
    floor.startMeasurement(frames);
    tracker.clear();
    bumpFloorVersionLocked();

    flog::info("Automatic reception: measuring noise floor over {} frames", frames);
}

void AutoReceiverManager::cancelMeasurement() {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    floor.cancelMeasurement();
    bumpFloorVersionLocked();
}

MeasurementState AutoReceiverManager::getMeasurementState() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return floor.getMeasurementState();
}

float AutoReceiverManager::getMeasurementProgress() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return floor.getMeasurementProgress();
}

bool AutoReceiverManager::setManualFloorFromSpectrum() {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    if (lastFrame.empty()) { return false; }

    float level = NoiseFloorModel::estimateFlatFloorDb(lastFrame.data(), (int)lastFrame.size(),
                                                       config.spectralPercentile);
    config.manualFloorDb = level;
    config.floorMode = NoiseFloorMode::MANUAL;
    applyConfigLocked();
    tracker.clear();
    bumpFloorVersionLocked();

    flog::info("Automatic reception: manual noise floor set to {} dB from the current spectrum",
               level);
    return true;
}

void AutoReceiverManager::resetFloor() {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    floor.resetMeasurement();
    tracker.clear();
    averageAccumulator.clear();
    averageCount = 0;
    bumpFloorVersionLocked();
}

void AutoReceiverManager::updateSourceState(const SourceManager::State& newState) {
    std::lock_guard<std::recursive_mutex> lck(mtx);

    // A measured floor is tied to the spectrum it was measured on, so a retune or a rate change
    // makes it meaningless. A manual flat level is a user setting and is independent of tuning,
    // so it stays put and detection keeps working across a retune.
    if (floor.getMode() == NoiseFloorMode::MEASURED &&
        floor.getMeasurementState() != MeasurementState::IDLE) {
        flog::info("Automatic reception: source changed, measured noise floor discarded");
        floor.resetMeasurement();
        bumpFloorVersionLocked();
    }
    tracker.clear();
    averageAccumulator.clear();
    averageCount = 0;
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

        bool shapeChanged = (lastBinCount != binCount) || (lastSpanHz != spanHz);
        lastBinCount = binCount;
        lastCenterFrequency = centerFrequency;
        lastSpanHz = spanHz;
        lastFrameRate = frameRate;
        lastFrame.assign(fft, fft + binCount);

        floor.configure(binCount, spanHz / (double)binCount);
        if (shapeChanged) {
            averageAccumulator.clear();
            averageCount = 0;
            bumpFloorVersionLocked();
        }

        // A running measurement consumes frames and produces nothing else.
        if (floor.getMeasurementState() == MeasurementState::MEASURING) {
            if (floor.addFrame(fft, binCount)) { bumpFloorVersionLocked(); }
            return;
        }

        if (!floor.isUsable()) { return; }

        // Average before thresholding.
        int avgFrames = std::max<int>(config.detectionAveragingFrames, 1);
        if ((int)averageAccumulator.size() != binCount) {
            averageAccumulator.assign(binCount, 0.0);
            averageCount = 0;
        }
        for (int b = 0; b < binCount; b++) { averageAccumulator[b] += fft[b]; }
        averageCount++;
        if (averageCount < avgFrames) { return; }

        std::vector<float> averaged(binCount);
        for (int b = 0; b < binCount; b++) {
            averaged[b] = (float)(averageAccumulator[b] / (double)averageCount);
        }
        averageAccumulator.assign(binCount, 0.0);
        averageCount = 0;

        DetectionParams params;
        params.minBins = config.minDetectionBins;
        params.minBandwidthHz = config.minBandwidthHz;
        params.maxBandwidthHz = config.maxBandwidthHz;
        params.mergeGapHz = config.mergeGapHz;
        params.usableSpectrumRatio = usableSpectrumRatio;

        auto detections = detectSignals(averaged.data(), binCount, floor, centerFrequency, spanHz,
                                        params);
        tracker.update(filterLocked(detections), nowMs);

        activeSignals = tracker.getActiveSignals();
        emitUpdate = true;
    }

    // Emitted outside the lock: handlers may call back into this object.
    if (emitUpdate) { onDetectionUpdate.emit(activeSignals); }
}

bool AutoReceiverManager::isFloorUsable() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return floor.isUsable();
}

NoiseFloorMode AutoReceiverManager::getFloorMode() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return floor.getMode();
}

AutoReceiverManager::FloorCurve AutoReceiverManager::getFloorCurve(int maxSamples) const {
    std::lock_guard<std::recursive_mutex> lck(mtx);

    FloorCurve curve;
    curve.version = floorVersion;
    curve.usable = floor.isUsable();
    if (!curve.usable) { return curve; }

    if (floor.getMode() == NoiseFloorMode::MANUAL) {
        curve.flat = true;
        curve.floorDb.push_back(floor.getFloorDb(0));
        curve.thresholdDb.push_back(floor.getThresholdDb(0));
        return curve;
    }

    if (lastBinCount <= 0 || lastSpanHz <= 0.0) {
        curve.usable = false;
        return curve;
    }

    curve.flat = false;
    curve.lowFrequency = lastCenterFrequency - (lastSpanHz / 2.0);
    curve.highFrequency = lastCenterFrequency + (lastSpanHz / 2.0);

    int samples = std::min<int>(std::max<int>(maxSamples, 2), lastBinCount);
    curve.floorDb.resize(samples);
    curve.thresholdDb.resize(samples);
    for (int i = 0; i < samples; i++) {
        int bin = (int)((double)i * (lastBinCount - 1) / (double)(samples - 1));
        curve.floorDb[i] = floor.getFloorDb(bin);
        curve.thresholdDb[i] = floor.getThresholdDb(bin);
    }
    return curve;
}

// Combine two detections known to belong to the same channel.
static DetectedSignal mergeSignals(const DetectedSignal& a, const DetectedSignal& b) {
    DetectedSignal out;
    out.lowerFrequency = std::min(a.lowerFrequency, b.lowerFrequency);
    out.upperFrequency = std::max(a.upperFrequency, b.upperFrequency);
    out.centerFrequency = (out.lowerFrequency + out.upperFrequency) / 2.0;
    out.bandwidth = out.upperFrequency - out.lowerFrequency;
    out.peakDb = std::max(a.peakDb, b.peakDb);
    out.noiseFloorDb = std::min(a.noiseFloorDb, b.noiseFloorDb);
    out.snrDb = out.peakDb - out.noiseFloorDb;

    // Weight each fragment's centroid by its linear power above the floor, so the combined
    // centroid follows the dominant part of the transmission.
    double wa = std::pow(10.0, a.snrDb / 10.0);
    double wb = std::pow(10.0, b.snrDb / 10.0);
    double total = wa + wb;
    out.centroidFrequency = (total > 0.0)
                                ? ((a.centroidFrequency * wa) + (b.centroidFrequency * wb)) / total
                                : out.centerFrequency;
    return out;
}

std::vector<DetectedSignal> AutoReceiverManager::filterLocked(
    const std::vector<DetectedSignal>& detections) const {
    std::vector<DetectedSignal> kept;
    kept.reserve(detections.size());

    for (const auto& sig : detections) {
        // Ignored ranges never reach the tracker, so they never allocate a receiver or start a
        // recording. They stay visible in the spectrum itself and in the history later.
        if (ignoreRules.isIgnored(sig.lowerFrequency, sig.upperFrequency)) { continue; }
        // A detection nowhere near an enabled profile is only dropped if the user asked for that.
        if (config.restrictToProfiles && !profiles.covers(sig.centroidFrequency)) { continue; }
        kept.push_back(sig);
    }

    // Collapse fragments onto their channel. A modulated carrier breaks into several
    // above-threshold runs within one channel; on a rastered band those all snap to the same
    // frequency and are one signal, not several.
    //
    // This runs whenever a matching profile defines a raster, regardless of restrictToProfiles.
    {
        std::vector<std::pair<double, size_t>> channelOf; // snapped frequency -> index in merged
        std::vector<DetectedSignal> merged;

        for (const auto& sig : kept) {
            const ReceptionProfile* profile = profiles.findFor(sig.centroidFrequency);
            double step = (profile != nullptr) ? profile->frequencyStep : 0.0;
            if (step <= 0.0) {
                merged.push_back(sig);
                continue;
            }

            double channel = snapToStep(sig.centroidFrequency, step);
            bool combined = false;
            for (auto& [existing, idx] : channelOf) {
                if (existing == channel) {
                    double keep = merged[idx].channelFrequency;
                    merged[idx] = mergeSignals(merged[idx], sig);
                    merged[idx].channelFrequency = keep;
                    combined = true;
                    break;
                }
            }
            if (!combined) {
                DetectedSignal tagged = sig;
                tagged.channelFrequency = channel;
                channelOf.emplace_back(channel, merged.size());
                merged.push_back(tagged);
            }
        }
        kept.swap(merged);
    }

    // Bandwidth limits are judged after merging, so that a channel assembled from fragments is
    // measured at its true width rather than each fragment being rejected on its own.
    std::vector<DetectedSignal> result;
    result.reserve(kept.size());
    for (const auto& sig : kept) {
        double minBw = config.minBandwidthHz;
        double maxBw = config.maxBandwidthHz;

        // A matching profile's limits are more specific than the global ones, so they win.
        double pMin = 0.0;
        double pMax = 0.0;
        if (profiles.detectionLimitsAt(sig.centroidFrequency, pMin, pMax)) {
            minBw = pMin;
            maxBw = pMax;
        }
        else if (config.restrictToProfiles) {
            continue;
        }

        if (minBw > 0.0 && sig.bandwidth < minBw) { continue; }
        if (maxBw > 0.0 && sig.bandwidth > maxBw) { continue; }
        result.push_back(sig);
    }
    return result;
}

double AutoReceiverManager::getTuneFrequency(const DetectedSignal& signal) const {
    std::lock_guard<std::recursive_mutex> lck(mtx);

    const ReceptionProfile* profile = profiles.findFor(signal.centroidFrequency);
    if (profile == nullptr) { return signal.centroidFrequency; }

    // SSB is referenced to the edge of the signal, everything else to its centre.
    switch (profile->demod) {
    case ProfileDemod::USB:
    case ProfileDemod::CW:
        return profile->snapFrequency(signal.lowerFrequency);
    case ProfileDemod::LSB:
        return profile->snapFrequency(signal.upperFrequency);
    default:
        break;
    }

    // For a centre-referenced mode on a rastered band, the channel the signal was assigned to is
    // a better answer than re-snapping a centroid that has since drifted across a channel edge.
    if (signal.channelFrequency != 0.0) { return signal.channelFrequency; }
    return profile->snapFrequency(signal.centroidFrequency);
}

void AutoReceiverManager::setProfiles(const ReceptionProfileSet& newProfiles) {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    profiles = newProfiles;
    tracker.clear();
}

ReceptionProfileSet AutoReceiverManager::getProfiles() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return profiles;
}

void AutoReceiverManager::setIgnoreRules(const IgnoreRuleSet& rules) {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    ignoreRules = rules;
    tracker.clear();
}

IgnoreRuleSet AutoReceiverManager::getIgnoreRules() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    return ignoreRules;
}

void AutoReceiverManager::ignoreSignal(uint64_t signalId, const std::string& reason,
                                       double paddingHz) {
    std::lock_guard<std::recursive_mutex> lck(mtx);

    for (const auto& track : tracker.getTracked()) {
        if (track.signal.id != signalId) { continue; }
        ignoreRules.addForSignal(track.signal.lowerFrequency, track.signal.upperFrequency, reason,
                                 paddingHz);
        tracker.clear();
        flog::info("Automatic reception: ignoring {} .. {} Hz ({})",
                   track.signal.lowerFrequency - paddingHz,
                   track.signal.upperFrequency + paddingHz, reason);
        return;
    }
}

std::vector<AutoReceiverManager::ClassifiedSignal> AutoReceiverManager::getClassifiedSignals() const {
    std::lock_guard<std::recursive_mutex> lck(mtx);

    std::vector<ClassifiedSignal> out;
    for (const auto& track : tracker.getTracked()) {
        ClassifiedSignal cs;
        cs.tracked = track;

        const IgnoreRule* rule = ignoreRules.find(track.signal.lowerFrequency,
                                                  track.signal.upperFrequency);
        if (rule != nullptr) {
            cs.ignored = true;
            cs.ignoreReason = rule->reason;
        }

        const ReceptionProfile* profile = profiles.findFor(track.signal.centroidFrequency);
        if (profile != nullptr) {
            cs.hasProfile = true;
            cs.profileName = profile->name;
            cs.demod = profile->demod;
            cs.receiverBandwidth = profile->bandwidth;
            cs.tuneFrequency = getTuneFrequency(track.signal);
        }
        else {
            cs.tuneFrequency = track.signal.centroidFrequency;
        }
        out.push_back(cs);
    }
    return out;
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
