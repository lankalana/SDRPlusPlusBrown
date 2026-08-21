#pragma once

#include <cstddef>
#include <cstdint>

namespace dsp::protocol::lora {

struct Header {
    uint8_t payloadLength = 0;
    uint8_t codingRate = 0;
    bool payloadCrcPresent = false;
    bool valid = false;
};

inline uint8_t headerChecksum(const uint8_t* nibbles) {
    const bool c4 = ((nibbles[0] >> 3u) & 1u) ^ ((nibbles[0] >> 2u) & 1u) ^ ((nibbles[0] >> 1u) & 1u) ^ (nibbles[0] & 1u);
    const bool c3 = ((nibbles[0] >> 3u) & 1u) ^ ((nibbles[1] >> 3u) & 1u) ^ ((nibbles[1] >> 2u) & 1u) ^ ((nibbles[1] >> 1u) & 1u) ^ (nibbles[2] & 1u);
    const bool c2 = ((nibbles[0] >> 2u) & 1u) ^ ((nibbles[1] >> 3u) & 1u) ^ (nibbles[1] & 1u) ^ ((nibbles[2] >> 3u) & 1u) ^ ((nibbles[2] >> 1u) & 1u);
    const bool c1 = ((nibbles[0] >> 1u) & 1u) ^ ((nibbles[1] >> 2u) & 1u) ^ (nibbles[1] & 1u) ^ ((nibbles[2] >> 2u) & 1u) ^ ((nibbles[2] >> 1u) & 1u) ^ (nibbles[2] & 1u);
    const bool c0 = (nibbles[0] & 1u) ^ ((nibbles[1] >> 1u) & 1u) ^ ((nibbles[2] >> 3u) & 1u) ^ ((nibbles[2] >> 2u) & 1u) ^ ((nibbles[2] >> 1u) & 1u) ^ (nibbles[2] & 1u);
    return static_cast<uint8_t>((static_cast<uint8_t>(c4) << 4u) | (static_cast<uint8_t>(c3) << 3u) |
                                (static_cast<uint8_t>(c2) << 2u) | (static_cast<uint8_t>(c1) << 1u) |
                                static_cast<uint8_t>(c0));
}

inline Header decodeHeader(const uint8_t* nibbles, std::size_t count) {
    Header header;
    if (!nibbles || count < 5) { return header; }
    header.payloadLength = static_cast<uint8_t>((nibbles[0] << 4u) | nibbles[1]);
    header.payloadCrcPresent = (nibbles[2] & 1u) != 0;
    header.codingRate = static_cast<uint8_t>(nibbles[2] >> 1u);
    const uint8_t receivedChecksum = static_cast<uint8_t>(((nibbles[3] & 1u) << 4u) | nibbles[4]);
    header.valid = header.payloadLength != 0 && header.codingRate >= 1 && header.codingRate <= 4 &&
                   receivedChecksum == headerChecksum(nibbles);
    return header;
}

inline void encodeHeader(uint8_t payloadLength, uint8_t codingRate, bool payloadCrcPresent, uint8_t* nibbles) {
    nibbles[0] = payloadLength >> 4u;
    nibbles[1] = payloadLength & 0x0Fu;
    nibbles[2] = static_cast<uint8_t>((codingRate << 1u) | static_cast<uint8_t>(payloadCrcPresent));
    const uint8_t checksum = headerChecksum(nibbles);
    nibbles[3] = checksum >> 4u;
    nibbles[4] = checksum & 0x0Fu;
}

}
