#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace protocol::meshtastic {

struct Channel {
    std::string name;
    std::vector<uint8_t> psk;
    uint8_t hash = 0;
    bool enabled = true;
};

const std::vector<uint8_t>& defaultPsk();
uint8_t calculateChannelHash(std::string_view name, const uint8_t* psk, std::size_t pskSize);
std::vector<uint8_t> expandPsk(const uint8_t* psk, std::size_t size);
bool decodeHexPsk(std::string_view text, std::vector<uint8_t>& psk);
bool decodeBase64Psk(std::string_view text, std::vector<uint8_t>& psk);
Channel makeChannel(std::string name, std::vector<uint8_t> psk, bool enabled = true);

}
