#include "spectrum_detector.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dsp::detector {

    const char* toString(SignalState state) {
        switch (state) {
        case SignalState::CANDIDATE: return "Candidate";
        case SignalState::ACTIVE: return "Active";
        case SignalState::RELEASING: return "Releasing";
        case SignalState::ENDED: return "Ended";
        }
        return "Unknown";
    }

    std::vector<DetectedSignal> detectSignals(const float* fft, int count,
                                              const NoiseFloorCalibration& calibration,
                                              double centerFrequency, double spanHz,
                                              const DetectionParams& params) {
        std::vector<DetectedSignal> signals;

        if (fft == nullptr || count <= 0 || spanHz <= 0.0) { return signals; }
        if (!calibration.isReady() || calibration.getBinCount() != count) { return signals; }

        // Restrict to the usable, centered part of the capture.
        double ratio = std::clamp(params.usableSpectrumRatio, 0.0, 1.0);
        int usableBins = (int)std::floor(count * ratio);
        if (usableBins < 1) { return signals; }
        int loBin = (count - usableBins) / 2;
        int hiBin = loBin + usableBins; // exclusive

        double binWidth = spanHz / (double)count;
        double specLow = centerFrequency - (spanHz / 2.0);

        int minBins = std::max<int>(params.minBins, 1);
        int maxGap = std::max<int>(params.maxGapBins, 0);

        int runStart = -1;  // first bin of the run being built
        int runEnd = -1;    // last above-threshold bin of the run being built
        int gap = 0;

        auto emit = [&](int start, int end) {
            if (start < 0 || end < start) { return; }
            if ((end - start + 1) < minBins) { return; }

            float peak = -std::numeric_limits<float>::infinity();
            double floorSum = 0.0;
            for (int i = start; i <= end; i++) {
                peak = std::max<float>(peak, fft[i]);
                floorSum += calibration.getBaselineDb(i);
            }
            float noiseFloor = (float)(floorSum / (double)(end - start + 1));

            DetectedSignal sig;
            sig.lowerFrequency = specLow + (start * binWidth);
            sig.upperFrequency = specLow + ((end + 1) * binWidth);
            sig.centerFrequency = (sig.lowerFrequency + sig.upperFrequency) / 2.0;
            sig.bandwidth = sig.upperFrequency - sig.lowerFrequency;
            sig.peakDb = peak;
            sig.noiseFloorDb = noiseFloor;
            sig.snrDb = peak - noiseFloor;
            signals.push_back(sig);
        };

        for (int i = loBin; i < hiBin; i++) {
            bool above = fft[i] > calibration.getThresholdDb(i);
            if (above) {
                if (runStart < 0) { runStart = i; }
                runEnd = i;
                gap = 0;
            }
            else if (runStart >= 0) {
                gap++;
                if (gap > maxGap) {
                    emit(runStart, runEnd);
                    runStart = -1;
                    runEnd = -1;
                    gap = 0;
                }
            }
        }
        emit(runStart, runEnd);

        return signals;
    }
}
