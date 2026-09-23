#pragma once
#include <cstdint>
#include <vector>

namespace dsp::detector {

    /**
     * One contiguous above-threshold region of the captured spectrum, in absolute frequencies.
     * Produced by detectSignals() from a single FFT frame; given an id and timestamps once a
     * SignalTracker has associated it with a tracked signal.
     */
    struct DetectedSignal {
        uint64_t id = 0;

        double lowerFrequency = 0.0;
        double upperFrequency = 0.0;
        double centerFrequency = 0.0;
        double bandwidth = 0.0;

        float peakDb = 0.0f;
        float noiseFloorDb = 0.0f;
        float snrDb = 0.0f;

        uint64_t firstSeen = 0;
        uint64_t lastSeen = 0;
    };

    /**
     * Lifecycle of a tracked signal. A detection does not become ACTIVE until it has been present
     * continuously for the activation delay, and does not END until it has been absent for the
     * release delay, so that short fades stay inside one signal.
     */
    enum class SignalState {
        CANDIDATE,
        ACTIVE,
        RELEASING,
        ENDED
    };

    struct TrackedSignal {
        DetectedSignal signal;
        SignalState state = SignalState::CANDIDATE;

        // Time the signal was last actually present in a frame. Drives the release delay.
        uint64_t lastDetectedMs = 0;
        // Time the current state was entered.
        uint64_t stateSinceMs = 0;
        // True once the signal reached ACTIVE at least once. A candidate that disappears before
        // being confirmed is discarded rather than released, so transients never allocate anything.
        bool wasActive = false;
    };

    const char* toString(SignalState state);
}
