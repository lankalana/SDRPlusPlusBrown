#pragma once
#include "detected_signal.h"

namespace dsp::detector {

    /**
     * Turns per-frame detections into signals with hysteresis.
     *
     * A single FFT frame is never enough to act on: noise crosses the threshold occasionally and
     * real transmissions fade. The tracker associates detections across frames by frequency
     * overlap, promotes a candidate to ACTIVE only after it has been continuously present for the
     * activation delay, and keeps an ACTIVE signal alive through gaps shorter than the release
     * delay.
     *
     * Not thread safe; the owner serializes access.
     */
    class SignalTracker {
    public:
        struct Params {
            uint64_t activationMs = 300;
            uint64_t releaseMs = 2000;
            /**
             * Minimum overlap, as a fraction of the narrower of the two ranges, for a detection to
             * be considered the same signal as an existing track. Measured against the narrower
             * range rather than the union so a signal whose apparent width changes between frames
             * still associates with itself.
             */
            double minOverlapRatio = 0.25;
        };

        Params params;

        // Feed one frame's detections. `nowMs` must be monotonically non-decreasing.
        void update(const std::vector<DetectedSignal>& detections, uint64_t nowMs);

        // Forget everything. Used when calibration is invalidated.
        void clear();

        // Live tracks (CANDIDATE, ACTIVE or RELEASING). ENDED tracks are pruned by update().
        const std::vector<TrackedSignal>& getTracked() const { return tracked; }

        // Tracks that reached ENDED during the most recent update() call.
        const std::vector<TrackedSignal>& getRecentlyEnded() const { return recentlyEnded; }

        std::vector<DetectedSignal> getActiveSignals() const;

        // Overlap of two frequency ranges as a fraction of the narrower one. 0 if disjoint.
        static double overlapRatio(const DetectedSignal& a, const DetectedSignal& b);

    private:
        std::vector<TrackedSignal> tracked;
        std::vector<TrackedSignal> recentlyEnded;
        uint64_t nextId = 1;
    };
}
