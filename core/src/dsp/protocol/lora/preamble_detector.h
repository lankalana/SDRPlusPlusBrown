#pragma once

#include "detail/math.h"
#include "types.h"

namespace dsp::protocol::lora {

class PreambleDetector {
public:
    void configure(int bins, int requiredSymbols, float minimumPeakRatio = 4.0f) {
        binCount = bins;
        required = requiredSymbols;
        minimumRatio = minimumPeakRatio;
        reset();
    }

    void reset() {
        stable = 0;
        averageBin = 0.0f;
        confidenceSum = 0.0f;
    }

    bool observe(const SymbolEstimate& estimate) {
        if (estimate.peakRatio() < minimumRatio) {
            reset();
            return false;
        }
        if (!stable) {
            stable = 1;
            averageBin = estimate.fractionalBin;
            confidenceSum = estimate.peakRatio();
            return stable >= required;
        }
        if (detail::circularDistance(estimate.fractionalBin, averageBin, binCount) > 1.5f) {
            stable = 1;
            averageBin = estimate.fractionalBin;
            confidenceSum = estimate.peakRatio();
            return false;
        }
        const float delta = detail::signedBins(estimate.fractionalBin - averageBin, binCount);
        averageBin = detail::wrapBins(averageBin + delta / static_cast<float>(stable + 1), binCount);
        confidenceSum += estimate.peakRatio();
        stable++;
        return stable >= required;
    }

    int stableSymbols() const { return stable; }
    float bin() const { return averageBin; }
    float confidence() const { return stable ? confidenceSum / stable : 0.0f; }

private:
    int binCount = 0;
    int required = 0;
    int stable = 0;
    float minimumRatio = 4.0f;
    float averageBin = 0.0f;
    float confidenceSum = 0.0f;
};

}
