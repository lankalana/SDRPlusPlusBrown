#pragma once

#include <cmath>
#include <cstddef>
#include <vector>
#include <dsp/types.h>
#include "chirp.h"
#include "types.h"
#include "detail/fft_symbol.h"

namespace dsp::protocol::lora {

class ChirpDemodulator {
public:
    void configure(const ChirpGenerator* chirps, double sampleRate) {
        generator = chirps;
        inputSampleRate = sampleRate;
        fft.configure(chirps ? chirps->bins() : 0);
        logicalSamples.resize(chirps ? chirps->bins() : 0);
    }

    SymbolEstimate demodulateUpchirp(const complex_t* samples, std::size_t count, float frequencyCorrectionHz = 0.0f) {
        return demodulate(samples, count, true, frequencyCorrectionHz);
    }

    SymbolEstimate demodulateDownchirp(const complex_t* samples, std::size_t count, float frequencyCorrectionHz = 0.0f) {
        return demodulate(samples, count, false, frequencyCorrectionHz);
    }

private:
    SymbolEstimate demodulate(const complex_t* samples, std::size_t count, bool upChirp, float frequencyCorrectionHz) {
        SymbolEstimate empty;
        if (!generator || !samples || count < static_cast<std::size_t>(generator->samplesPerSymbol())) { return empty; }
        const std::vector<complex_t>& reference = upChirp ? generator->upchirp() : generator->downchirp();
        const int oversampling = generator->oversampling();
        const double correctionStep = -2.0 * 3.14159265358979323846 * frequencyCorrectionHz / inputSampleRate;
        for (int bin = 0; bin < generator->bins(); bin++) {
            complex_t sum = { 0.0f, 0.0f };
            for (int sub = 0; sub < oversampling; sub++) {
                const int index = bin * oversampling + sub;
                complex_t value = {
                    samples[index].re * reference[index].re + samples[index].im * reference[index].im,
                    samples[index].im * reference[index].re - samples[index].re * reference[index].im
                };
                if (frequencyCorrectionHz != 0.0f) {
                    const double phase = correctionStep * index;
                    const complex_t rotator = { static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase)) };
                    value = {
                        value.re * rotator.re - value.im * rotator.im,
                        value.im * rotator.re + value.re * rotator.im
                    };
                }
                sum += value;
            }
            logicalSamples[bin] = sum;
        }
        return fft.estimate(logicalSamples.data(), logicalSamples.size());
    }

    const ChirpGenerator* generator = nullptr;
    double inputSampleRate = 0.0;
    detail::FftSymbol fft;
    std::vector<complex_t> logicalSamples;
};

}
