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
        // Midpoint of the above-threshold extent.
        double centerFrequency = 0.0;
        double bandwidth = 0.0;

        /**
         * Power-weighted centre of the signal.
         *
         * Prefer this over centerFrequency when tuning a receiver. The extent midpoint depends on
         * exactly where the threshold cuts the signal's skirts, which for a modulated carrier
         * moves from frame to frame; the centroid is weighted by power above the floor and stays
         * put.
         */
        double centroidFrequency = 0.0;

        /**
         * The channel this signal was assigned to, when a channel raster applies; 0 otherwise.
         *
         * On a rastered band this, not frequency overlap, is the identity of a signal. Two
         * fragments of one transmission seen in different frames may not overlap each other at
         * all, but if they snap to the same channel they are the same station.
         */
        double channelFrequency = 0.0;

        /**
         * Index of that channel on the raster, or 0 when no raster applies.
         *
         * This, not channelFrequency, is the identity comparison. Two code paths that compute the
         * same channel -- round(f/step)*step in one place, chLow + k*step in a loop in another --
         * can differ in the last bit, and comparing the doubles would then start a fresh track
         * every frame.
         */
        int64_t channelIndex = 0;

        /**
         * Width the signal actually occupies, as opposed to `bandwidth`, which on a rastered band
         * is the channel width the receiver will use. Air traffic is a good example of the two
         * being very different: a narrow carrier inside a 25 kHz channel.
         */
        double occupiedBandwidth = 0.0;

        float peakDb = 0.0f;
        float noiseFloorDb = 0.0f;
        // Integrated channel SNR: power over the signal's width against the noise in that same
        // width. Peak-minus-floor reads ~20 dB high for a carrier and does not predict audio.
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
