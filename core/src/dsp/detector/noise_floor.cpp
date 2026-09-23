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
        // Remember how far below mean noise power this percentile sits, so getNoisePowerDb() can
        // undo it. It depends on framesSeen, which is why it is captured here and not derived on
        // demand from a setting that may since have changed.
        noiseBiasDb = percentileToMeanBiasDb(spectralPercentile, framesSeen);

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

    // Standard normal quantile, Acklam's rational approximation. Accurate to ~1e-9 relative,
    // which is far more than the Cornish-Fisher expansion it feeds deserves.
    static double normalQuantile(double p) {
        static const double a[6] = { -3.969683028665376e+01, 2.209460984245205e+02,
                                     -2.759285104469687e+02, 1.383577518672690e+02,
                                     -3.066479806614716e+01, 2.506628277459239e+00 };
        static const double b[5] = { -5.447609879822406e+01, 1.615858368580409e+02,
                                     -1.556989798598866e+02, 6.680131188771972e+01,
                                     -1.328068155288572e+01 };
        static const double c[6] = { -7.784894002430293e-03, -3.223964580411365e-01,
                                     -2.400758277161838e+00, -2.549732539343734e+00,
                                     4.374664141464968e+00, 2.938163982698783e+00 };
        static const double d[4] = { 7.784695709041462e-03, 3.224671290700398e-01,
                                     2.445134137142996e+00, 3.754408661907416e+00 };
        const double pLow = 0.02425;

        if (p <= 0.0) { return -std::numeric_limits<double>::infinity(); }
        if (p >= 1.0) { return std::numeric_limits<double>::infinity(); }

        if (p < pLow) {
            double q = std::sqrt(-2.0 * std::log(p));
            return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
                   ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
        }
        if (p > 1.0 - pLow) {
            double q = std::sqrt(-2.0 * std::log(1.0 - p));
            return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
                   ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
        }
        double q = p - 0.5;
        double r = q * q;
        return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
               (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
    }

    float NoiseFloorModel::percentileToMeanBiasDb(float percentile, int framesAveraged) {
        double p = std::clamp((double)percentile, 1e-4, 1.0 - 1e-4);
        int m = std::max<int>(framesAveraged, 1);

        // 10/ln(10): converts a natural-log quantile into dB.
        const double DB_PER_NEPER = 4.342944819032518;

        if (m == 1) {
            // Single look: bin power is Exp(mean), so ln(power/mean) has CDF 1 - exp(-e^y) and
            // the quantile is available in closed form.
            double y = std::log(-std::log(1.0 - p));
            return (float)(-DB_PER_NEPER * y);
        }

        // Mean of m iid log-Exp(1) variables. Mean -gamma, variance pi^2/6 per term, and a
        // pronounced left skew that a plain normal approximation gets wrong at low percentiles.
        const double EULER_GAMMA = 0.5772156649015329;
        const double VAR_ONE = 1.6449340668482264;      // pi^2 / 6
        const double SKEW_ONE = -1.1395470994046486;    // -2*zeta(3) / (pi^2/6)^1.5

        double sigma = std::sqrt(VAR_ONE / (double)m);
        double skew = SKEW_ONE / std::sqrt((double)m);
        double z = normalQuantile(p);
        // Cornish-Fisher, first skewness term.
        double y = -EULER_GAMMA + (sigma * (z + (((z * z) - 1.0) * skew / 6.0)));
        return (float)(-DB_PER_NEPER * y);
    }

    float NoiseFloorModel::getNoisePowerDb(int bin) const {
        float floor = getFloorDb(bin);
        if (!std::isfinite(floor)) { return std::numeric_limits<float>::infinity(); }

        // A manual level is a single number the user placed where the noise looks like it sits,
        // which is the same thing estimateFlatFloorDb() computes from one frame.
        if (mode == NoiseFloorMode::MANUAL) {
            return floor + percentileToMeanBiasDb(spectralPercentile, 1);
        }
        return floor + noiseBiasDb;
    }
}
