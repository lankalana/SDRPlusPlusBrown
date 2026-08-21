#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace protocol::meshtastic {

std::array<uint8_t, 16> makeNonce(uint32_t packetId, uint32_t from);
bool cryptChannelPayload(const uint8_t* input, std::size_t size, const uint8_t* key,
                         std::size_t keySize, const std::array<uint8_t, 16>& nonce,
                         std::vector<uint8_t>& output);

}
