#include "noise_floor.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dsp::detector {

    const char* toString(NoiseFloorMode mode) {
        switch (mode) {
        case NoiseFloorMode::MANUAL: return "Manual";
        case NoiseFloorMode::MEASURED: return "Measured";
        }
        return "Unknown";
    }

    const char* toString(MeasurementState state) {
        switch (state) {
        case MeasurementState::IDLE: return "Idle";
        case MeasurementState::MEASURING: return "Measuring";
        case MeasurementState::READY: return "Ready";
        }
        return "Unknown";
    }

    // Cap on how many values a single window percentile looks at. Above this the window is
    // sampled with a stride: the percentile of an evenly spaced subset of a smooth spectrum is
    // indistinguishable from the percentile of all of it, and this keeps a 16k-bin frame cheap.
    static constexpr int MAX_WINDOW_SAMPLES = 256;

    static float percentileOf(std::vector<float>& scratch, float percentile) {
        if (scratch.empty()) { return 0.0f; }
        size_t k = (size_t)(percentile * (scratch.size() - 1));
        k = std::min(k, scratch.size() - 1);
        std::nth_element(scratch.begin(), scratch.begin() + k, scratch.end());
        return scratch[k];
    }

    float NoiseFloorModel::estimateFlatFloorDb(const float* fft, int count, float percentile) {
        if (fft == nullptr || count <= 0) { return DEFAULT_MANUAL_FLOOR_DB; }

        int stride = std::max<int>(1, count / 4096);
        std::vector<float> scratch;
        scratch.reserve((count / stride) + 1);
        for (int i = 0; i < count; i += stride) { scratch.push_back(fft[i]); }
        return percentileOf(scratch, std::clamp(percentile, 0.0f, 1.0f));
    }

    void NoiseFloorModel::spectralFloor(const float* fft, int count, int windowBins,
                                        float percentile, std::vector<float>& out) {
        out.assign(std::max<int>(count, 0), 0.0f);
        if (fft == nullptr || count <= 0) { return; }

        windowBins = std::clamp(windowBins, 3, count);
        percentile = std::clamp(percentile, 0.0f, 1.0f);
        int half = windowBins / 2;

        // Evaluate on a coarse grid and interpolate. The floor is a slowly varying quantity, so
        // evaluating it per bin would be wasted work.
        int gridStep = std::max<int>(1, windowBins / 8);
        int stride = std::max<int>(1, windowBins / MAX_WINDOW_SAMPLES);

        std::vector<int> gridIndex;
        std::vector<float> gridValue;
        std::vector<float> scratch;

        for (int g = 0; g < count; g += gridStep) {
            int lo = std::max<int>(0, g - half);
            int hi = std::min<int>(count - 1, g + half);

            scratch.clear();
            for (int i = lo; i <= hi; i += stride) { scratch.push_back(fft[i]); }

            gridIndex.push_back(g);
            gridValue.push_back(percentileOf(scratch, percentile));
        }
        // Always anchor the far edge so the interpolation covers the whole frame.
        if (gridIndex.back() != count - 1) {
            gridIndex.push_back(count - 1);
            gridValue.push_back(gridValue.back());
        }

        for (size_t seg = 0; seg + 1 < gridIndex.size(); seg++) {
            int a = gridIndex[seg];
            int b = gridIndex[seg + 1];
            float va = gridValue[seg];
            float vb = gridValue[seg + 1];
            int span = b - a;
            for (int i = a; i <= b; i++) {
                float t = (span > 0) ? ((float)(i - a) / (float)span) : 0.0f;
                out[i] = va + ((vb - va) * t);
            }
        }
    }

    void NoiseFloorModel::configure(int binCount, double binWidthHz) {
        if (this->binCount == binCount && this->binWidthHz == binWidthHz) { return; }
        this->binCount = std::max<int>(binCount, 0);
        this->binWidthHz = binWidthHz;
        // A measured floor is a per-bin quantity; a different shape means it maps to nothing.
        resetMeasurement();
    }

    void NoiseFloorModel::setMode(NoiseFloorMode mode) {
        if (this->mode == mode) { return; }
        this->mode = mode;
        if (mode == NoiseFloorMode::MANUAL) { cancelMeasurement(); }
    }

    void NoiseFloorModel::setManualFloorDb(float db) { manualFloorDb = db; }

    void NoiseFloorModel::setMarginDb(float db) { marginDb = db; }

    void NoiseFloorModel::setSpectralWindowHz(double hz) {
        spectralWindowHz = std::max<double>(hz, 0.0);
    }

    void NoiseFloorModel::setSpectralPercentile(float percentile) {
        spectralPercentile = std::clamp(percentile, 0.0f, 1.0f);
    }

    void NoiseFloorModel::startMeasurement(int frames) {
        if (binCount <= 0) { return; }
        mode = NoiseFloorMode::MEASURED;
        framesRequired = std::max<int>(frames, 1);
        framesSeen = 0;
        accumulator.assign(binCount, 0.0);
        measuredFloor.clear();
        measurementState = MeasurementState::MEASURING;
    }

    void NoiseFloorModel::cancelMeasurement() {
        if (measurementState != MeasurementState::MEASURING) { return; }
        measurementState = measuredFloor.empty() ? MeasurementState::IDLE : MeasurementState::READY;
        accumulator.clear();
        framesSeen = 0;
    }

    void NoiseFloorModel::resetMeasurement() {
        measurementState = MeasurementState::IDLE;
        measuredFloor.clear();
        accumulator.clear();
        framesSeen = 0;
    }

    bool NoiseFloorModel::addFrame(const float* fft, int count) {
        if (measurementState != MeasurementState::MEASURING) { return false; }
        if (fft == nullptr || count != binCount) {
            resetMeasurement();
            return false;
        }

        for (int b = 0; b < count; b++) { accumulator[b] += fft[b]; }
        framesSeen++;

        if (framesSeen >= framesRequired) {
            finalizeMeasurement();
            return measurementState == MeasurementState::READY;
        }
        return false;
    }

    void NoiseFloorModel::finalizeMeasurement() {
        if (framesSeen <= 0 || binCount <= 0) {
            resetMeasurement();
            return;
        }

        // Average first so a single noisy frame doesn't shape the floor, then take the spectral
        // percentile of the average.
        std::vector<float> averaged(binCount);
        for (int b = 0; b < binCount; b++) {
            averaged[b] = (float)(accumulator[b] / (double)framesSeen);
        }

        int windowBins = binCount;
        if (binWidthHz > 0.0 && spectralWindowHz > 0.0) {
            windowBins = (int)std::lround(spectralWindowHz / binWidthHz);
        }
        spectralFloor(averaged.data(), binCount, windowBins, spectralPercentile, measuredFloor);

        accumulator.clear();
        framesSeen = 0;
        measurementState = MeasurementState::READY;
    }

    float NoiseFloorModel::getMeasurementProgress() const {
        if (measurementState == MeasurementState::READY) { return 1.0f; }
        if (measurementState != MeasurementState::MEASURING || framesRequired <= 0) { return 0.0f; }
        return std::clamp((float)framesSeen / (float)framesRequired, 0.0f, 1.0f);
    }

    bool NoiseFloorModel::isUsable() const {
        if (mode == NoiseFloorMode::MANUAL) { return true; }
        return measurementState == MeasurementState::READY && !measuredFloor.empty();
    }

    float NoiseFloorModel::getFloorDb(int bin) const {
        if (mode == NoiseFloorMode::MANUAL) { return manualFloorDb; }
        if (bin < 0 || bin >= (int)measuredFloor.size()) {
            return std::numeric_limits<float>::infinity();
        }
        return measuredFloor[bin];
    }

    float NoiseFloorModel::getThresholdDb(int bin) const {
        float floor = getFloorDb(bin);
        if (!std::isfinite(floor)) { return std::numeric_limits<float>::infinity(); }
        return floor + marginDb;
    }
}
