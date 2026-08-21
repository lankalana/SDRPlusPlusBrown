#pragma once

#include <cstdint>

namespace dsp::protocol::lora {

inline uint16_t grayEncode(uint16_t value) {
    return value ^ (value >> 1u);
}

inline uint16_t grayDecode(uint16_t value) {
    uint16_t decoded = value;
    for (uint16_t shift = 1; shift < 16; shift <<= 1u) {
        decoded ^= decoded >> shift;
    }
    return decoded;
}

}
