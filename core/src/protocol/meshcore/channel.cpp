#include "channel.h"

#include "crypto.h"

#include <cctype>

namespace protocol::meshcore {

const std::vector<uint8_t>& publicChannelSecret() {
    static const std::vector<uint8_t> secret = {
        0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
        0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72
    };
    return secret;
}

bool decodeHexSecret(std::string_view text, std::vector<uint8_t>& secret) {
    secret.clear();
    int high = -1;
    for (char character : text) {
        if (std::isspace(static_cast<unsigned char>(character)) || character == ':' || character == '-') { continue; }
        int value = -1;
        if (character >= '0' && character <= '9') { value = character - '0'; }
        else if (character >= 'a' && character <= 'f') { value = character - 'a' + 10; }
        else if (character >= 'A' && character <= 'F') { value = character - 'A' + 10; }
        else { secret.clear(); return false; }
        if (high < 0) { high = value; }
        else { secret.push_back(static_cast<uint8_t>((high << 4u) | value)); high = -1; }
    }
    if (high >= 0 || (secret.size() != 16 && secret.size() != 32)) { secret.clear(); return false; }
    return true;
}

bool decodeBase64Secret(std::string_view text, std::vector<uint8_t>& secret) {
    secret.clear();
    uint32_t accumulator = 0;
    int bits = 0;
    bool padding = false;
    int characters = 0;
    int paddingCharacters = 0;
    for (char character : text) {
        if (std::isspace(static_cast<unsigned char>(character))) { continue; }
        characters++;
        if (character == '=') { padding = true; paddingCharacters++; continue; }
        if (padding) { secret.clear(); return false; }
        int value = -1;
        if (character >= 'A' && character <= 'Z') { value = character - 'A'; }
        else if (character >= 'a' && character <= 'z') { value = character - 'a' + 26; }
        else if (character >= '0' && character <= '9') { value = character - '0' + 52; }
        else if (character == '+') { value = 62; }
        else if (character == '/') { value = 63; }
        else { secret.clear(); return false; }
        accumulator = (accumulator << 6u) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            secret.push_back(static_cast<uint8_t>(accumulator >> bits));
            accumulator &= (1u << bits) - 1u;
        }
    }
    if (!characters || characters % 4 != 0 || paddingCharacters > 2 || accumulator != 0 ||
        (secret.size() != 16 && secret.size() != 32)) {
        secret.clear();
        return false;
    }
    return true;
}

bool makeGroupChannel(std::string name, std::vector<uint8_t> secret, GroupChannel& channel) {
    if (name.empty() || (secret.size() != 16 && secret.size() != 32)) { return false; }
    uint8_t digest[32];
    if (!sha256(secret.data(), secret.size(), digest)) { return false; }
    channel.name = std::move(name);
    channel.secret = std::move(secret);
    channel.hash = digest[0];
    return true;
}

}
