#pragma once
#include "noise_floor.h"
#include <vector>

namespace dsp::detector {

    /**
     * Prefix sums of per-bin excess power and noise power, so the integrated SNR of any kernel is
     * two subtractions and a divide.
     *
     * Why this exists: thresholding each FFT bin on its own is a poor detector. At 2 MSPS with a
     * 64k FFT a bin is 30 Hz, so a 6 kHz signal spreads its power over ~200 bins and its per-bin
     * SNR is ~23 dB below its channel SNR. Integrating over a kernel matched to the signal
     * recovers that -- but only for a signal whose energy actually fills the kernel, which is why
     * callers sweep several kernel widths rather than picking one.
     *
     * Everything is in linear power. Logarithms are for reporting, never for the hot loop.
     */
    class SpectrumIntegrator {
    public:
        /**
         * A bin range whose power is replaced by the local noise power before the sums are built.
         *
         * Used for the LO spike at the capture centre. Skipping those bins at evaluation time is
         * not enough: a 40-60 dB spike dominates the integral of every kernel that overlaps it,
         * so a wide kernel would report a detection for hundreds of bins either side.
         */
        struct Excision {
            int loBin = 0; // inclusive
            int hiBin = 0; // inclusive
        };

        /**
         * `powerLin` is mean linear power per bin, already averaged over however many frames the
         * caller integrates. Normalisation is by floor.getNoisePowerDb(), not getFloorDb().
         */
        void build(const float* powerLin, int count, const NoiseFloorModel& floor,
                   const std::vector<Excision>& excisions);

        int binCount() const { return bins; }
        bool empty() const { return bins <= 0; }

        // Inclusive ranges, clamped to the frame.
        double excessPower(int lo, int hi) const;
        double noisePower(int lo, int hi) const;

        // 1 + excess/noise. Compare against a linear threshold; never take a log to decide.
        double snrRatio(int lo, int hi) const;
        // Reporting only.
        double snrDb(int lo, int hi) const;

        // Excess power in a single bin, for peak picking.
        double binExcess(int bin) const;

    private:
        // prefix[i] is the sum over bins [0, i), so a range sum needs no special case at zero.
        std::vector<double> excessPrefix;
        std::vector<double> noisePrefix;
        int bins = 0;
    };
}
