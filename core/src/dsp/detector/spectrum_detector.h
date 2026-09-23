#pragma once
#include "detected_signal.h"
#include "noise_floor.h"
#include "spectrum_integrator.h"

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

    /**
     * One detection pass over a sub-range of a frame, using one reception profile's geometry.
     *
     * Deliberately not an overload of detectSignals(): that takes a dB frame and thresholds each
     * bin, this takes an integrator built from linear power. Sharing a name would let a caller
     * pass the wrong domain and still compile.
     */
    struct PassParams {
        // Half-open bin range this pass owns, already clipped to the usable span.
        int loBin = 0;
        int hiBin = 0;

        /**
         * Kernel widths to integrate over, in bins, ascending. The detector takes the best result
         * across all of them ("greatest-of").
         *
         * A single kernel cannot work for both of the cases that matter. An AM carrier holds
         * nearly all its power in three or four bins, so integrating over a 10 kHz channel buries
         * it under 300 bins of noise; a broadcast FM station fills its channel, so a four-bin
         * kernel throws away almost all of its energy. Sweeping widths costs one O(n) scan each
         * and is never worse than either choice alone.
         */
        std::vector<int> kernelBins;

        // User sensitivity setting: how far above the noise, integrated, counts as a signal.
        float marginDb = 10.0f;
        // Per-evaluation false alarm probability. Bounds the threshold from below for wide
        // kernels, where marginDb alone would be looser than the statistics justify.
        double falseAlarmRate = 1e-7;
        // Independent looks per bin: frames averaged, divided by the window's noise bandwidth.
        double looksPerBin = 1.0;

        // Channelized when > 0: one detection per raster channel, identity is the channel.
        double channelStepHz = 0.0;
        double channelBandwidthHz = 0.0;

        // Wideband-intruder rejection, channelized mode only. A channel is rejected when the
        // guard band either side is nearly as hot per bin as the channel itself, which is what a
        // transmission far wider than the channel looks like.
        double guardRatio = 3.0;
        float maxSpillDb = 3.0f;

        // Grouping mode only (no raster).
        double minBandwidthHz = 0.0;
        double maxBandwidthHz = 0.0;
        double mergeGapHz = 0.0;
        int minBins = 2;
    };

    std::vector<DetectedSignal> detectPass(const SpectrumIntegrator& integ, double centerFrequency,
                                           double spanHz, const PassParams& params);

    /**
     * Smallest integrated power ratio that a kernel of `looks` independent samples exceeds with
     * probability `falseAlarmRate` under noise alone. Exposed for testing.
     */
    double cfarRatio(double looks, double falseAlarmRate);

    /**
     * Kernel widths for a profile of the given bandwidth, in bins: bw/32, bw/8, bw/2, bw, clamped
     * to at least 4 bins and at most `maxBins`, deduplicated and ascending.
     *
     * Four bins is the narrow limit because a Nuttall window spreads a pure tone over three to
     * four bins; a one-bin kernel pays scalloping loss for no gain.
     */
    std::vector<int> kernelSetFor(double bandwidthHz, double binWidthHz, int maxBins);
}
