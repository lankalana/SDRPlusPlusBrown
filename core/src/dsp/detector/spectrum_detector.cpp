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
                                              const NoiseFloorModel& floor,
                                              double centerFrequency, double spanHz,
                                              const DetectionParams& params) {
        std::vector<DetectedSignal> signals;

        if (fft == nullptr || count <= 0 || spanHz <= 0.0) { return signals; }
        if (!floor.isUsable()) { return signals; }
        // A measured floor is per-bin, so it only applies to a frame of the same width. A manual
        // floor is flat and applies to any frame.
        if (floor.getMode() == NoiseFloorMode::MEASURED && floor.getBinCount() != count) {
            return signals;
        }

        // Restrict to the usable, centered part of the capture.
        double ratio = std::clamp(params.usableSpectrumRatio, 0.0, 1.0);
        int usableBins = (int)std::floor(count * ratio);
        if (usableBins < 1) { return signals; }
        int loBin = (count - usableBins) / 2;
        int hiBin = loBin + usableBins; // exclusive

        double binWidth = spanHz / (double)count;
        double specLow = centerFrequency - (spanHz / 2.0);

        int minBins = std::max<int>(params.minBins, 1);
        if (params.minBandwidthHz > 0.0) {
            minBins = std::max<int>(minBins, (int)std::floor(params.minBandwidthHz / binWidth));
        }
        int maxGap = 0;
        if (params.mergeGapHz > 0.0) {
            maxGap = (int)std::lround(params.mergeGapHz / binWidth);
        }
        int maxBins = std::numeric_limits<int>::max();
        if (params.maxBandwidthHz > 0.0) {
            maxBins = std::max<int>(1, (int)std::ceil(params.maxBandwidthHz / binWidth));
        }

        int runStart = -1;  // first bin of the run being built
        int runEnd = -1;    // last above-threshold bin of the run being built
        int gap = 0;

        auto emit = [&](int start, int end) {
            if (start < 0 || end < start) { return; }
            int width = end - start + 1;
            if (width < minBins || width > maxBins) { return; }

            float peak = -std::numeric_limits<float>::infinity();
            double floorSum = 0.0;
            for (int i = start; i <= end; i++) {
                peak = std::max<float>(peak, fft[i]);
                floorSum += floor.getFloorDb(i);
            }
            float noiseFloor = (float)(floorSum / (double)width);

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
            bool above = fft[i] > floor.getThresholdDb(i);
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
