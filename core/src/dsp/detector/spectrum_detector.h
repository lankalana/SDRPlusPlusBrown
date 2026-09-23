#pragma once
#include "detected_signal.h"
#include "noise_calibration.h"

namespace dsp::detector {

    struct DetectionParams {
        // Runs narrower than this are noise spikes rather than signals.
        int minBins = 2;
        // Single-bin dropouts inside a signal (notches, fades) are bridged rather than splitting
        // one transmission into two detections.
        int maxGapBins = 1;
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
     * Returns an empty vector unless the calibration is READY and its bin count matches, so an
     * uncalibrated spectrum can never produce detections.
     *
     * The returned signals carry geometry and SNR only; ids and timestamps are assigned by
     * SignalTracker.
     */
    std::vector<DetectedSignal> detectSignals(const float* fft, int count,
                                              const NoiseFloorCalibration& calibration,
                                              double centerFrequency, double spanHz,
                                              const DetectionParams& params);
}
