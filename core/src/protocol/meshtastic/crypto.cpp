#include "crypto.h"

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace protocol::meshtastic {

std::array<uint8_t, 16> makeNonce(uint32_t packetId, uint32_t from) {
    std::array<uint8_t, 16> nonce{};
    for (int i = 0; i < 4; i++) {
        nonce[static_cast<std::size_t>(i)] = static_cast<uint8_t>(packetId >> (8 * i));
        nonce[static_cast<std::size_t>(8 + i)] = static_cast<uint8_t>(from >> (8 * i));
    }
    return nonce;
}

#ifdef _WIN32
namespace {

bool succeeded(NTSTATUS status) { return status >= 0; }

void incrementCounter(std::array<uint8_t, 16>& counter) {
    for (std::size_t i = counter.size(); i-- > 12;) {
        counter[i]++;
        if (counter[i] != 0) { break; }
    }
}

}
#endif

bool cryptChannelPayload(const uint8_t* input, std::size_t size, const uint8_t* key,
                         std::size_t keySize, const std::array<uint8_t, 16>& nonce,
                         std::vector<uint8_t>& output) {
    if ((!input && size != 0) || !key || (keySize != 16 && keySize != 32)) { return false; }
    output.assign(size, 0);
    if (size == 0) { return true; }
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE aesKey = nullptr;
    DWORD objectSize = 0;
    DWORD resultSize = 0;
    std::vector<uint8_t> keyObject;
    bool ok = succeeded(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0));
    if (ok) {
        ok = succeeded(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                                        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
                                        sizeof(BCRYPT_CHAIN_MODE_ECB), 0));
    }
    if (ok) {
        ok = succeeded(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                        reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultSize, 0));
    }
    if (ok) {
        keyObject.resize(objectSize);
        ok = succeeded(BCryptGenerateSymmetricKey(algorithm, &aesKey, keyObject.data(), objectSize,
                                                  const_cast<PUCHAR>(key), static_cast<ULONG>(keySize), 0));
    }
    std::array<uint8_t, 16> counter = nonce;
    std::array<uint8_t, 16> stream{};
    for (std::size_t offset = 0; ok && offset < size; offset += stream.size()) {
        ULONG written = 0;
        ok = succeeded(BCryptEncrypt(aesKey, counter.data(), static_cast<ULONG>(counter.size()), nullptr,
                                     nullptr, 0, stream.data(), static_cast<ULONG>(stream.size()), &written, 0)) &&
             written == stream.size();
        const std::size_t count = (std::min)(stream.size(), size - offset);
        for (std::size_t i = 0; ok && i < count; i++) { output[offset + i] = input[offset + i] ^ stream[i]; }
        incrementCounter(counter);
    }
    if (aesKey) { BCryptDestroyKey(aesKey); }
    if (algorithm) { BCryptCloseAlgorithmProvider(algorithm, 0); }
    if (!ok) { output.clear(); }
    return ok;
#else
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (!context) { output.clear(); return false; }
    const EVP_CIPHER* cipher = keySize == 16 ? EVP_aes_128_ctr() : EVP_aes_256_ctr();
    int produced = 0;
    int finalProduced = 0;
    const bool ok = EVP_DecryptInit_ex(context, cipher, nullptr, key, nonce.data()) == 1 &&
                    EVP_DecryptUpdate(context, output.data(), &produced, input, static_cast<int>(size)) == 1 &&
                    EVP_DecryptFinal_ex(context, output.data() + produced, &finalProduced) == 1;
    EVP_CIPHER_CTX_free(context);
    if (!ok || static_cast<std::size_t>(produced + finalProduced) != size) { output.clear(); return false; }
    return true;
#endif
}

}
