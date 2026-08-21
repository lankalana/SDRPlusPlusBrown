#pragma once

#include <cstddef>
#include <vector>
#include <dsp/types.h>

namespace dsp::protocol::lora {

class ChirpGenerator {
public:
    void configure(int spreadingFactor, int oversampling);

    const std::vector<complex_t>& upchirp() const { return up; }
    const std::vector<complex_t>& downchirp() const { return down; }
    int bins() const { return binCount; }
    int samplesPerSymbol() const { return static_cast<int>(up.size()); }
    int oversampling() const { return osFactor; }

    void generateSymbol(int symbol, bool upChirp, complex_t* output, std::size_t count) const;

private:
    std::vector<complex_t> up;
    std::vector<complex_t> down;
    int binCount = 0;
    int osFactor = 0;
};

}
