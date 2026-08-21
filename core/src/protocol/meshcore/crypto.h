#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace protocol::meshcore {

bool sha256(const uint8_t* input, std::size_t size, uint8_t output[32]);
bool hmacSha256(const uint8_t* key, std::size_t keySize, const uint8_t* input, std::size_t size,
                uint8_t output[32]);
bool decryptAes128Ecb(const uint8_t* key, const uint8_t* input, std::size_t size,
                      std::vector<uint8_t>& output);
bool encryptAes128Ecb(const uint8_t* key, const uint8_t* input, std::size_t size,
                      std::vector<uint8_t>& output);

}
