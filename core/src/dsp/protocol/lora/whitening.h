#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsp::protocol::lora {

inline uint8_t whiteningByte(std::size_t offset) {
    uint8_t state = 0xFF;
    for (std::size_t i = 0; i < offset; i++) {
        const uint8_t feedback = static_cast<uint8_t>(((state >> 7u) ^ (state >> 5u) ^
                                                       (state >> 4u) ^ (state >> 3u)) & 1u);
        state = static_cast<uint8_t>((state << 1u) | feedback);
    }
    return state;
}

inline void dewhiten(uint8_t* data, std::size_t size, std::size_t offset = 0) {
    uint8_t state = whiteningByte(offset);
    for (std::size_t i = 0; i < size; i++) {
        data[i] ^= state;
        const uint8_t feedback = static_cast<uint8_t>(((state >> 7u) ^ (state >> 5u) ^
                                                       (state >> 4u) ^ (state >> 3u)) & 1u);
        state = static_cast<uint8_t>((state << 1u) | feedback);
    }
}

inline void dewhiten(std::vector<uint8_t>& data) {
    dewhiten(data.data(), data.size());
}

}
