#pragma once

#include <cstdint>

namespace dsp::protocol::lora {

struct FecResult {
    uint8_t nibble = 0;
    bool corrected = false;
    bool valid = false;
};

inline uint8_t hammingEncode(uint8_t nibble, int codingRate) {
    nibble &= 0x0Fu;
    const bool d0 = (nibble & 0x01u) != 0;
    const bool d1 = (nibble & 0x02u) != 0;
    const bool d2 = (nibble & 0x04u) != 0;
    const bool d3 = (nibble & 0x08u) != 0;
    const uint8_t reversedData = static_cast<uint8_t>((static_cast<uint8_t>(d0) << 3u) |
                                                      (static_cast<uint8_t>(d1) << 2u) |
                                                      (static_cast<uint8_t>(d2) << 1u) |
                                                      static_cast<uint8_t>(d3));
    if (codingRate == 1) {
        const bool parity = d0 ^ d1 ^ d2 ^ d3;
        return static_cast<uint8_t>((reversedData << 1u) | static_cast<uint8_t>(parity));
    }

    const bool p0 = d0 ^ d1 ^ d2;
    const bool p1 = d1 ^ d2 ^ d3;
    const bool p2 = d0 ^ d1 ^ d3;
    const bool p3 = d0 ^ d2 ^ d3;
    const uint8_t parity = static_cast<uint8_t>((static_cast<uint8_t>(p0) << 3u) |
                                                (static_cast<uint8_t>(p1) << 2u) |
                                                (static_cast<uint8_t>(p2) << 1u) |
                                                static_cast<uint8_t>(p3));
    const uint8_t full = static_cast<uint8_t>((reversedData << 4u) | parity);
    return static_cast<uint8_t>(full >> (4 - codingRate));
}

inline int bitCount(uint8_t value) {
    int count = 0;
    while (value) {
        count += value & 1u;
        value >>= 1u;
    }
    return count;
}

inline FecResult hammingDecode(uint8_t codeword, int codingRate) {
    FecResult result;
    if (codingRate < 1 || codingRate > 4) { return result; }
    const uint8_t mask = static_cast<uint8_t>((1u << (4 + codingRate)) - 1u);
    codeword &= mask;
    int bestDistance = 9;
    int bestCount = 0;
    for (uint8_t nibble = 0; nibble < 16; nibble++) {
        const int distance = bitCount(static_cast<uint8_t>(codeword ^ hammingEncode(nibble, codingRate)));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestCount = 1;
            result.nibble = nibble;
        }
        else if (distance == bestDistance) {
            bestCount++;
        }
    }
    result.corrected = bestDistance == 1 && bestCount == 1 && codingRate >= 3;
    result.valid = bestDistance == 0 || result.corrected;
    return result;
}

}
