#pragma once

#include <cstddef>
#include <vector>
#include <dsp/types.h>
#include <fftw3.h>
#include "../types.h"

namespace dsp::protocol::lora::detail {

class FftSymbol {
public:
    FftSymbol() = default;
    ~FftSymbol();
    FftSymbol(const FftSymbol&) = delete;
    FftSymbol& operator=(const FftSymbol&) = delete;

    void configure(int bins);
    SymbolEstimate estimate(const complex_t* samples, std::size_t count);

private:
    void release();

    fftwf_complex* input = nullptr;
    fftwf_complex* output = nullptr;
    fftwf_plan plan = nullptr;
    std::vector<float> magnitudes;
    int binCount = 0;
};

}
