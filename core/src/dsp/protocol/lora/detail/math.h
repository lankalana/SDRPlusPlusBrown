#pragma once

#include <cmath>

namespace dsp::protocol::lora::detail {

inline int modulo(int value, int modulus) {
    const int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

inline float wrapBins(float value, int bins) {
    value = std::fmod(value, static_cast<float>(bins));
    if (value < 0.0f) { value += bins; }
    return value;
}

inline float signedBins(float value, int bins) {
    value = wrapBins(value, bins);
    if (value >= bins * 0.5f) { value -= bins; }
    return value;
}

inline float circularDistance(float first, float second, int bins) {
    return std::fabs(signedBins(first - second, bins));
}

}
