#include "noise_calibration.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dsp::detector {

    const char* toString(CalibrationState state) {
        switch (state) {
        case CalibrationState::UNCALIBRATED: return "Uncalibrated";
        case CalibrationState::CALIBRATING: return "Calibrating";
        case CalibrationState::READY: return "Ready";
        }
        return "Unknown";
    }

    // Scale factor that turns a median absolute deviation into a standard-deviation estimate for
    // normally distributed data, so that deviationMultiplier reads as "sigmas".
    static constexpr float MAD_TO_SIGMA = 1.4826f;

    static float medianOf(std::vector<float>& scratch) {
        if (scratch.empty()) { return 0.0f; }
        size_t mid = scratch.size() / 2;
        std::nth_element(scratch.begin(), scratch.begin() + mid, scratch.end());
        return scratch[mid];
    }

    void NoiseFloorCalibration::configure(int binCount, int framesRequired, int maxStoredFrames) {
        reset();
        this->binCount = std::max<int>(binCount, 0);
        this->framesRequired = std::max<int>(framesRequired, 1);
        this->maxStoredFrames = std::max<int>(maxStoredFrames, 1);
        // Keep every stride-th frame so the retained sample spans the whole calibration window
        // instead of only its beginning.
        stride = (this->framesRequired + this->maxStoredFrames - 1) / this->maxStoredFrames;
        if (stride < 1) { stride = 1; }
    }

    void NoiseFloorCalibration::reset() {
        state = CalibrationState::UNCALIBRATED;
        framesSeen = 0;
        collected.clear();
        collected.shrink_to_fit();
        bins.clear();
        thresholds.clear();
    }

    void NoiseFloorCalibration::begin() {
        if (binCount <= 0 || framesRequired <= 0) { return; }
        framesSeen = 0;
        collected.clear();
        bins.clear();
        thresholds.clear();
        state = CalibrationState::CALIBRATING;
    }

    bool NoiseFloorCalibration::addFrame(const float* fft, int count) {
        if (state != CalibrationState::CALIBRATING) { return false; }
        if (fft == nullptr || count != binCount) {
            // The spectrum changed shape underneath us; the partial model is meaningless.
            reset();
            return false;
        }

        if ((framesSeen % stride) == 0 && (int)collected.size() < maxStoredFrames) {
            collected.emplace_back(fft, fft + count);
        }
        framesSeen++;

        if (framesSeen >= framesRequired) {
            finalizeModel();
            return state == CalibrationState::READY;
        }
        return false;
    }

    void NoiseFloorCalibration::finalizeModel() {
        if (collected.empty() || binCount <= 0) {
            reset();
            return;
        }

        bins.assign(binCount, NoiseBin{});
        std::vector<float> scratch(collected.size());

        for (int b = 0; b < binCount; b++) {
            for (size_t f = 0; f < collected.size(); f++) {
                scratch[f] = collected[f][b];
            }
            float baseline = medianOf(scratch);

            // MAD, reusing the scratch buffer. nth_element above left it permuted, which is fine:
            // we only need the multiset of values, not their order.
            for (size_t f = 0; f < scratch.size(); f++) {
                scratch[f] = fabsf(scratch[f] - baseline);
            }
            float mad = medianOf(scratch);

            bins[b].baselineDb = baseline;
            bins[b].deviationDb = mad * MAD_TO_SIGMA;
        }

        // The frames are no longer needed once the model exists.
        collected.clear();
        collected.shrink_to_fit();

        state = CalibrationState::READY;
        recomputeThresholds();
    }

    void NoiseFloorCalibration::recomputeThresholds() {
        if (state != CalibrationState::READY) {
            thresholds.clear();
            return;
        }
        thresholds.resize(bins.size());
        for (size_t b = 0; b < bins.size(); b++) {
            thresholds[b] = bins[b].baselineDb +
                            std::max<float>(minimumMarginDb, bins[b].deviationDb * deviationMultiplier);
        }
    }

    float NoiseFloorCalibration::getProgress() const {
        if (state == CalibrationState::READY) { return 1.0f; }
        if (state != CalibrationState::CALIBRATING || framesRequired <= 0) { return 0.0f; }
        return std::clamp((float)framesSeen / (float)framesRequired, 0.0f, 1.0f);
    }

    float NoiseFloorCalibration::getThresholdDb(int bin) const {
        if (bin < 0 || bin >= (int)thresholds.size()) {
            return std::numeric_limits<float>::infinity();
        }
        return thresholds[bin];
    }

    float NoiseFloorCalibration::getBaselineDb(int bin) const {
        if (bin < 0 || bin >= (int)bins.size()) { return 0.0f; }
        return bins[bin].baselineDb;
    }

    void NoiseFloorCalibration::setMinimumMarginDb(float margin) {
        if (minimumMarginDb == margin) { return; }
        minimumMarginDb = margin;
        recomputeThresholds();
    }

    void NoiseFloorCalibration::setDeviationMultiplier(float multiplier) {
        if (deviationMultiplier == multiplier) { return; }
        deviationMultiplier = multiplier;
        recomputeThresholds();
    }
}
