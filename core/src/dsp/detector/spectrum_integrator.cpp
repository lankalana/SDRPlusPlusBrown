#include "spectrum_integrator.h"
#include <algorithm>
#include <cmath>

namespace dsp::detector {

    void SpectrumIntegrator::build(const float* powerLin, int count, const NoiseFloorModel& floor,
                                   const std::vector<Excision>& excisions) {
        if (powerLin == nullptr || count <= 0) {
            bins = 0;
            excessPrefix.clear();
            noisePrefix.clear();
            return;
        }

        bins = count;
        excessPrefix.assign((size_t)count + 1, 0.0);
        noisePrefix.assign((size_t)count + 1, 0.0);

        // Excised bins contribute their noise power and no excess, which is exactly what a bin
        // holding nothing but noise would contribute.
        std::vector<bool> excised;
        if (!excisions.empty()) {
            excised.assign(count, false);
            for (const auto& ex : excisions) {
                int lo = std::max<int>(ex.loBin, 0);
                int hi = std::min<int>(ex.hiBin, count - 1);
                for (int b = lo; b <= hi; b++) { excised[b] = true; }
            }
        }

        for (int b = 0; b < count; b++) {
            float noiseDb = floor.getNoisePowerDb(b);
            double noiseLin = std::isfinite(noiseDb) ? std::pow(10.0, (double)noiseDb / 10.0) : 0.0;

            double excess = 0.0;
            if (excised.empty() || !excised[b]) {
                excess = std::max(0.0, (double)powerLin[b] - noiseLin);
            }

            excessPrefix[b + 1] = excessPrefix[b] + excess;
            noisePrefix[b + 1] = noisePrefix[b] + noiseLin;
        }
    }

    double SpectrumIntegrator::excessPower(int lo, int hi) const {
        if (bins <= 0) { return 0.0; }
        lo = std::max<int>(lo, 0);
        hi = std::min<int>(hi, bins - 1);
        if (hi < lo) { return 0.0; }
        return excessPrefix[hi + 1] - excessPrefix[lo];
    }

    double SpectrumIntegrator::noisePower(int lo, int hi) const {
        if (bins <= 0) { return 0.0; }
        lo = std::max<int>(lo, 0);
        hi = std::min<int>(hi, bins - 1);
        if (hi < lo) { return 0.0; }
        return noisePrefix[hi + 1] - noisePrefix[lo];
    }

    double SpectrumIntegrator::binExcess(int bin) const {
        if (bins <= 0 || bin < 0 || bin >= bins) { return 0.0; }
        return excessPrefix[bin + 1] - excessPrefix[bin];
    }

    double SpectrumIntegrator::snrRatio(int lo, int hi) const {
        double noise = noisePower(lo, hi);
        if (noise <= 0.0) { return 1.0; }
        return 1.0 + (excessPower(lo, hi) / noise);
    }

    double SpectrumIntegrator::snrDb(int lo, int hi) const {
        return 10.0 * std::log10(std::max(snrRatio(lo, hi), 1e-30));
    }
}
