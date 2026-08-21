#include "chirp.h"

#include <cmath>
#include <stdexcept>

namespace dsp::protocol::lora {

void ChirpGenerator::configure(int spreadingFactor, int oversampling) {
    if (spreadingFactor < 5 || spreadingFactor > 12 || oversampling < 1) {
        throw std::invalid_argument("invalid LoRa chirp configuration");
    }
    binCount = 1 << spreadingFactor;
    osFactor = oversampling;
    const int sampleCount = binCount * osFactor;
    up.resize(sampleCount);
    down.resize(sampleCount);
    const double twoPi = 2.0 * 3.14159265358979323846;
    const double denominator = 2.0 * osFactor * osFactor * binCount;
    for (int sample = 0; sample < sampleCount; sample++) {
        const double phase = twoPi * (-(static_cast<double>(sample) / (2.0 * osFactor)) +
                                     (static_cast<double>(sample) * sample) / denominator);
        up[sample] = { static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase)) };
        down[sample] = up[sample].conj();
    }
}

void ChirpGenerator::generateSymbol(int symbol, bool upChirp, complex_t* output, std::size_t count) const {
    if (!output || count < up.size() || up.empty()) { return; }
    symbol %= binCount;
    if (symbol < 0) { symbol += binCount; }
    const std::vector<complex_t>& reference = upChirp ? up : down;
    const double angularStep = 2.0 * 3.14159265358979323846 * symbol / static_cast<double>(up.size());
    for (std::size_t sample = 0; sample < up.size(); sample++) {
        const double phase = angularStep * sample;
        const complex_t tone = { static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase)) };
        output[sample] = {
            reference[sample].re * tone.re - reference[sample].im * tone.im,
            reference[sample].im * tone.re + reference[sample].re * tone.im
        };
    }
}

}
