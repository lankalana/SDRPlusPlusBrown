#include "fft_symbol.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dsp::protocol::lora::detail {

FftSymbol::~FftSymbol() {
    release();
}

void FftSymbol::release() {
    if (plan) { fftwf_destroy_plan(plan); }
    if (input) { fftwf_free(input); }
    if (output) { fftwf_free(output); }
    plan = nullptr;
    input = nullptr;
    output = nullptr;
    binCount = 0;
    magnitudes.clear();
}

void FftSymbol::configure(int bins) {
    if (bins == binCount) { return; }
    if (bins <= 0 || (bins & (bins - 1)) != 0) { throw std::invalid_argument("FFT size must be a power of two"); }
    release();
    input = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * bins));
    output = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * bins));
    if (!input || !output) {
        release();
        throw std::bad_alloc();
    }
    plan = fftwf_plan_dft_1d(bins, input, output, FFTW_FORWARD, FFTW_ESTIMATE);
    if (!plan) {
        release();
        throw std::runtime_error("failed to create LoRa FFT plan");
    }
    binCount = bins;
    magnitudes.resize(bins);
}

SymbolEstimate FftSymbol::estimate(const complex_t* samples, std::size_t count) {
    SymbolEstimate estimate;
    if (!plan || !samples || count < static_cast<std::size_t>(binCount)) { return estimate; }
    for (int i = 0; i < binCount; i++) {
        input[i][0] = samples[i].re;
        input[i][1] = samples[i].im;
    }
    fftwf_execute(plan);

    int peak = 0;
    for (int i = 0; i < binCount; i++) {
        magnitudes[i] = output[i][0] * output[i][0] + output[i][1] * output[i][1];
        if (magnitudes[i] > magnitudes[peak]) { peak = i; }
    }
    int second = (peak + 2) % binCount;
    for (int i = 0; i < binCount; i++) {
        const int distance = (i - peak + binCount) % binCount;
        if (distance != 0 && distance != 1 && distance != binCount - 1 && magnitudes[i] > magnitudes[second]) {
            second = i;
        }
    }

    const float left = magnitudes[(peak + binCount - 1) % binCount];
    const float center = magnitudes[peak];
    const float right = magnitudes[(peak + 1) % binCount];
    const float denominator = left - 2.0f * center + right;
    float fractional = 0.0f;
    if (std::fabs(denominator) > 1.0e-20f) {
        fractional = 0.5f * (left - right) / denominator;
        fractional = (std::max)(-0.5f, (std::min)(0.5f, fractional));
    }
    estimate.symbol = peak;
    estimate.peakMagnitude = center;
    estimate.secondPeakMagnitude = magnitudes[second];
    estimate.fractionalBin = peak + fractional;
    if (estimate.fractionalBin < 0.0f) { estimate.fractionalBin += binCount; }
    if (estimate.fractionalBin >= binCount) { estimate.fractionalBin -= binCount; }
    return estimate;
}

}
