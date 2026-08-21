#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsp::protocol::lora {

inline uint16_t crc16Ccitt(const uint8_t* data, std::size_t size) {
    uint16_t crc = 0;
    for (std::size_t i = 0; i < size; i++) {
        uint8_t value = data[i];
        for (int bit = 0; bit < 8; bit++) {
            const bool feedback = ((crc & 0x8000u) != 0) ^ ((value & 0x80u) != 0);
            crc = static_cast<uint16_t>(crc << 1u);
            if (feedback) { crc ^= 0x1021u; }
            value = static_cast<uint8_t>(value << 1u);
        }
    }
    return crc;
}

inline uint16_t payloadCrc(const uint8_t* payload, std::size_t size) {
    if (size < 2) { return crc16Ccitt(payload, size); }
    return static_cast<uint16_t>(crc16Ccitt(payload, size - 2) ^ payload[size - 1] ^ (static_cast<uint16_t>(payload[size - 2]) << 8u));
}

inline uint16_t payloadCrc(const std::vector<uint8_t>& payload) {
    return payloadCrc(payload.data(), payload.size());
}

inline bool checkPayloadCrc(const uint8_t* payloadWithCrc, std::size_t size) {
    if (size < 2) { return false; }
    const std::size_t payloadSize = size - 2;
    const uint16_t received = static_cast<uint16_t>(payloadWithCrc[payloadSize]) |
                              (static_cast<uint16_t>(payloadWithCrc[payloadSize + 1]) << 8u);
    return payloadCrc(payloadWithCrc, payloadSize) == received;
}

inline bool checkPayloadCrc(const std::vector<uint8_t>& payloadWithCrc) {
    return checkPayloadCrc(payloadWithCrc.data(), payloadWithCrc.size());
}

}
