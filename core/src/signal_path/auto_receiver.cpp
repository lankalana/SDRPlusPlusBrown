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
        powerAccumulator.clear();
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
    powerAccumulator.clear();
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
    powerAccumulator.clear();
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
            powerAccumulator.clear();
            averageCount = 0;
            bumpFloorVersionLocked();
        }

        // A running measurement consumes frames and produces nothing else.
        if (floor.getMeasurementState() == MeasurementState::MEASURING) {
            if (floor.addFrame(fft, binCount)) { bumpFloorVersionLocked(); }
            return;
        }

        if (!floor.isUsable()) { return; }

        // Accumulate in linear power, not dB: the detector integrates energy, and a dB average
        // estimates the log-mean instead of the mean.
        int avgFrames = std::max<int>(config.detectionAveragingFrames, 1);
        if ((int)powerAccumulator.size() != binCount) {
            powerAccumulator.assign(binCount, 0.0);
            averageCount = 0;
        }
        for (int b = 0; b < binCount; b++) {
            powerAccumulator[b] += std::pow(10.0, (double)fft[b] / 10.0);
        }
        averageCount++;
        if (averageCount < avgFrames) { return; }

        framePower.resize(binCount);
        for (int b = 0; b < binCount; b++) {
            framePower[b] = (float)(powerAccumulator[b] / (double)averageCount);
        }
        int framesAveraged = averageCount;
        std::fill(powerAccumulator.begin(), powerAccumulator.end(), 0.0);
        averageCount = 0;

        // Replace the LO spike at the capture centre with the local noise level before the prefix
        // sums are built, so it cannot dominate the integral of every kernel that overlaps it.
        std::vector<SpectrumIntegrator::Excision> excisions;
        if (config.dcNotchHz > 0.0) {
            double binWidth = spanHz / (double)binCount;
            int half = std::max<int>(1, (int)std::lround((config.dcNotchHz / 2.0) / binWidth));
            int center = binCount / 2;
            excisions.push_back({ center - half, center + half });
        }
        integrator.build(framePower.data(), binCount, floor, excisions);

        lastFramesAveraged = framesAveraged;
        auto detections = detectAllLocked(centerFrequency, spanHz, usableSpectrumRatio);
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

std::vector<DetectedSignal> AutoReceiverManager::detectAllLocked(double centerFrequency,
                                                                 double spanHz,
                                                                 double usableSpectrumRatio) {
    std::vector<DetectedSignal> out;
    int binCount = integrator.binCount();
    if (binCount <= 0 || spanHz <= 0.0) { return out; }

    const double binWidth = spanHz / (double)binCount;
    const double specLow = centerFrequency - (spanHz / 2.0);

    // The edges of a capture are shaped by the analog anti-alias filter and produce nothing but
    // artefacts.
    double ratio = std::clamp(usableSpectrumRatio, 0.0, 1.0);
    int usableBins = (int)std::floor(binCount * ratio);
    if (usableBins < 1) { return out; }
    int usableLo = (binCount - usableBins) / 2;
    int usableHi = usableLo + usableBins; // exclusive

    // Nuttall's equivalent noise bandwidth: a kernel of K bins holds K/2.02 independent samples.
    const double NUTTALL_ENBW = 2.02;
    const double looksPerBin = (double)std::max(lastFramesAveraged, 1) / NUTTALL_ENBW;

    coverageScratch.assign(binCount, 0);

    // Profiles in descending priority, so that findFor() and the pass that produced a candidate
    // always agree about who owns a frequency.
    std::vector<const ReceptionProfile*> ordered;
    for (const auto& p : profiles.profiles) {
        if (p.enabled) { ordered.push_back(&p); }
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const ReceptionProfile* a, const ReceptionProfile* b) {
                         return a->priority > b->priority;
                     });

    auto runPass = [&](PassParams& params, const ReceptionProfile* owner) {
        if (params.hiBin <= params.loBin) { return; }
        params.marginDb = config.marginDb;
        params.falseAlarmRate = config.falseAlarmRate;
        params.looksPerBin = looksPerBin;

        auto found = detectPass(integrator, centerFrequency, spanHz, params);
        for (auto& sig : found) {
            // Overlapping profiles would otherwise each report the same signal.
            if (profiles.findFor(sig.centroidFrequency) != owner) { continue; }
            out.push_back(sig);
        }
    };

    for (const ReceptionProfile* p : ordered) {
        // Where this profile's range meets the captured span.
        double lo = (p->minFrequency == 0.0 && p->maxFrequency == 0.0) ? specLow : p->minFrequency;
        double hi = (p->minFrequency == 0.0 && p->maxFrequency == 0.0) ? (specLow + spanHz)
                                                                       : p->maxFrequency;
        int loBin = std::max<int>(usableLo, (int)std::floor((lo - specLow) / binWidth));
        int hiBin = std::min<int>(usableHi, (int)std::ceil((hi - specLow) / binWidth));
        if (hiBin <= loBin) { continue; }

        for (int b = loBin; b < hiBin; b++) { coverageScratch[b] = 1; }

        PassParams params;
        params.loBin = loBin;
        params.hiBin = hiBin;
        params.kernelBins = kernelSetFor(p->bandwidth, binWidth, hiBin - loBin);
        params.channelStepHz = p->frequencyStep;
        params.channelBandwidthHz = p->bandwidth;
        params.minBandwidthHz = p->minDetectionBandwidth;
        params.maxBandwidthHz = p->maxDetectionBandwidth;
        params.mergeGapHz = (p->mergeGapHz > 0.0) ? p->mergeGapHz : config.mergeGapHz;
        params.minBins = config.minDetectionBins;
        runPass(params, p);
    }

    // Anything no profile claims, using the global settings, unless the user asked for profiles
    // only. Run one pass per contiguous uncovered stretch rather than re-querying per bin.
    if (!config.restrictToProfiles) {
        int b = usableLo;
        while (b < usableHi) {
            if (coverageScratch[b]) {
                b++;
                continue;
            }
            int start = b;
            while (b < usableHi && !coverageScratch[b]) { b++; }

            PassParams params;
            params.loBin = start;
            params.hiBin = b;
            // No profile means no expected channel width, so sweep a wide span of kernels.
            params.kernelBins = { 4, 16, 64, 256 };
            params.kernelBins.erase(
                std::remove_if(params.kernelBins.begin(), params.kernelBins.end(),
                               [&](int k) { return k > (b - start); }),
                params.kernelBins.end());
            params.minBandwidthHz = config.minBandwidthHz;
            params.maxBandwidthHz = config.maxBandwidthHz;
            params.mergeGapHz = config.mergeGapHz;
            params.minBins = config.minDetectionBins;
            runPass(params, nullptr);
        }
    }

    return out;
}

std::vector<DetectedSignal> AutoReceiverManager::filterLocked(
    const std::vector<DetectedSignal>& detections) const {
    std::vector<DetectedSignal> kept;
    kept.reserve(detections.size());

    // Profile gating, channel assignment and bandwidth limits all happen inside the per-profile
    // passes now, so the only thing left to apply here is the user's ignore list. Ignored ranges
    // never reach the tracker, so they never allocate a receiver or start a recording; they stay
    // visible in the spectrum itself.
    for (const auto& sig : detections) {
        if (ignoreRules.isIgnored(sig.lowerFrequency, sig.upperFrequency)) { continue; }
        kept.push_back(sig);
    }
    return kept;
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
