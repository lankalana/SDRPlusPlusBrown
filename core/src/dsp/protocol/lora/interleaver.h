#pragma once

#include <cstddef>
#include <cstdint>

namespace dsp::protocol::lora {

inline int positiveModulo(int value, int modulus) {
    const int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

inline bool deinterleave(const uint16_t* symbols, std::size_t symbolCount, int spreadingFactor,
                         int codingRate, bool reducedRate, uint8_t* codewords, std::size_t codewordCapacity) {
    const int effectiveSf = spreadingFactor - (reducedRate ? 2 : 0);
    const int codewordLength = 4 + codingRate;
    if (!symbols || !codewords || effectiveSf <= 0 || symbolCount < static_cast<std::size_t>(codewordLength) ||
        codewordCapacity < static_cast<std::size_t>(effectiveSf)) {
        return false;
    }

    for (int row = 0; row < effectiveSf; row++) { codewords[row] = 0; }
    for (int column = 0; column < codewordLength; column++) {
        for (int bit = 0; bit < effectiveSf; bit++) {
            const bool value = (symbols[column] & (1u << (effectiveSf - 1 - bit))) != 0;
            const int row = positiveModulo(column - bit - 1, effectiveSf);
            codewords[row] |= static_cast<uint8_t>(value << (codewordLength - 1 - column));
        }
    }
    return true;
}

inline bool interleave(const uint8_t* codewords, std::size_t codewordCount, int spreadingFactor,
                       int codingRate, bool reducedRate, uint16_t* symbols, std::size_t symbolCapacity) {
    const int effectiveSf = spreadingFactor - (reducedRate ? 2 : 0);
    const int codewordLength = 4 + codingRate;
    if (!symbols || !codewords || codewordCount < static_cast<std::size_t>(effectiveSf) ||
        symbolCapacity < static_cast<std::size_t>(codewordLength)) {
        return false;
    }

    for (int column = 0; column < codewordLength; column++) {
        uint16_t symbol = 0;
        for (int bit = 0; bit < effectiveSf; bit++) {
            const int row = positiveModulo(column - bit - 1, effectiveSf);
            const bool value = (codewords[row] & (1u << (codewordLength - 1 - column))) != 0;
            symbol |= static_cast<uint16_t>(value << (effectiveSf - 1 - bit));
        }
        symbols[column] = symbol;
    }
    return true;
}

}
