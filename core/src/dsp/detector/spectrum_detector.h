#pragma once
#include "detected_signal.h"
#include "noise_floor.h"

namespace dsp::detector {

    struct DetectionParams {
        /**
         * Narrowest and widest signal worth reporting, in Hz. The minimum is the main defence
         * against a wide transmission fragmenting into a scatter of narrow detections; set it
         * near the narrowest mode being monitored (a few kHz for SSB, tens of kHz for broadcast
         * FM). A maximum of 0 means no upper limit.
         */
        double minBandwidthHz = 0.0;
        double maxBandwidthHz = 0.0;

        /**
         * Dropouts in the spectrum narrower than this are bridged rather than splitting one
         * transmission in two. Broadcast FM in particular has a deeply notched instantaneous
         * spectrum and needs tens of kHz here.
         */
        double mergeGapHz = 0.0;

        // Hard floor on run length, applied on top of minBandwidthHz.
        int minBins = 2;

        // Fraction of the FFT span that is actually usable. The edges of a capture are shaped by
        // the analog anti-alias filter and produce nothing but artefacts.
        double usableSpectrumRatio = 1.0;
    };

    /**
     * Group contiguous above-threshold FFT bins into detected signals.
     *
     * `fft` is a power spectrum in dB with bin 0 at the lowest frequency (already fftshifted), so
     * that the frame spans [centerFrequency - spanHz/2, centerFrequency + spanHz/2].
     *
     * Returns an empty vector unless the floor model is usable and matches the frame width, so an
     * unset floor can never produce detections.
     *
     * The returned signals carry geometry and SNR only; ids and timestamps are assigned by
     * SignalTracker.
     */
    std::vector<DetectedSignal> detectSignals(const float* fft, int count,
                                              const NoiseFloorModel& floor,
                                              double centerFrequency, double spanHz,
                                              const DetectionParams& params);
}
