#include "channel.h"

#include <cctype>

namespace protocol::meshtastic {

const std::vector<uint8_t>& defaultPsk() {
    static const std::vector<uint8_t> key = {
        0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
        0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
    };
    return key;
}

uint8_t calculateChannelHash(std::string_view name, const uint8_t* psk, std::size_t pskSize) {
    uint8_t hash = 0;
    for (char value : name) { hash ^= static_cast<uint8_t>(value); }
    for (std::size_t i = 0; i < pskSize; i++) { hash ^= psk[i]; }
    return hash;
}

std::vector<uint8_t> expandPsk(const uint8_t* psk, std::size_t size) {
    if (!psk || size == 0) { return {}; }
    if (size == 1 && psk[0] >= 1 && psk[0] <= 10) {
        std::vector<uint8_t> expanded = defaultPsk();
        expanded.back() = psk[0];
        return expanded;
    }
    if (size == 16 || size == 32) { return { psk, psk + size }; }
    return {};
}

bool decodeHexPsk(std::string_view text, std::vector<uint8_t>& psk) {
    psk.clear();
    int high = -1;
    for (char character : text) {
        if (std::isspace(static_cast<unsigned char>(character)) || character == ':' || character == '-') { continue; }
        int value = -1;
        if (character >= '0' && character <= '9') { value = character - '0'; }
        else if (character >= 'a' && character <= 'f') { value = character - 'a' + 10; }
        else if (character >= 'A' && character <= 'F') { value = character - 'A' + 10; }
        else { psk.clear(); return false; }
        if (high < 0) { high = value; }
        else { psk.push_back(static_cast<uint8_t>((high << 4u) | value)); high = -1; }
    }
    if (high >= 0) { psk.clear(); return false; }
    return psk.empty() || psk.size() == 1 || psk.size() == 16 || psk.size() == 32;
}

bool decodeBase64Psk(std::string_view text, std::vector<uint8_t>& psk) {
    psk.clear();
    uint32_t accumulator = 0;
    int bits = 0;
    bool padding = false;
    int characters = 0;
    int paddingCharacters = 0;
    for (char character : text) {
        if (std::isspace(static_cast<unsigned char>(character))) { continue; }
        characters++;
        if (character == '=') { padding = true; paddingCharacters++; continue; }
        if (padding) { psk.clear(); return false; }
        int value = -1;
        if (character >= 'A' && character <= 'Z') { value = character - 'A'; }
        else if (character >= 'a' && character <= 'z') { value = character - 'a' + 26; }
        else if (character >= '0' && character <= '9') { value = character - '0' + 52; }
        else if (character == '+') { value = 62; }
        else if (character == '/') { value = 63; }
        else { psk.clear(); return false; }
        accumulator = (accumulator << 6u) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            psk.push_back(static_cast<uint8_t>(accumulator >> bits));
            accumulator &= (1u << bits) - 1u;
        }
    }
    if ((characters && characters % 4 != 0) || paddingCharacters > 2 || accumulator != 0) {
        psk.clear();
        return false;
    }
    return psk.empty() || psk.size() == 1 || psk.size() == 16 || psk.size() == 32;
}

Channel makeChannel(std::string name, std::vector<uint8_t> psk, bool enabled) {
    Channel channel;
    channel.name = std::move(name);
    channel.psk = expandPsk(psk.data(), psk.size());
    if (psk.empty()) { channel.psk.clear(); }
    channel.hash = calculateChannelHash(channel.name, channel.psk.data(), channel.psk.size());
    channel.enabled = enabled;
    return channel;
}

}
