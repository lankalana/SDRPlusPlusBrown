#include "crypto.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif

namespace protocol::meshcore {

#ifdef _WIN32
namespace {

bool succeeded(NTSTATUS status) { return status >= 0; }

bool hashWithProvider(const wchar_t* algorithmName, ULONG flags, const uint8_t* key, std::size_t keySize,
                      const uint8_t* input, std::size_t size, uint8_t output[32]) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0;
    DWORD resultSize = 0;
    std::vector<uint8_t> object;
    bool ok = succeeded(BCryptOpenAlgorithmProvider(&algorithm, algorithmName, nullptr, flags));
    if (ok) {
        ok = succeeded(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                        reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultSize, 0));
    }
    if (ok) {
        object.resize(objectSize);
        ok = succeeded(BCryptCreateHash(algorithm, &hash, object.data(), objectSize,
                                       const_cast<PUCHAR>(key), static_cast<ULONG>(keySize), 0));
    }
    if (ok && size) {
        ok = succeeded(BCryptHashData(hash, const_cast<PUCHAR>(input), static_cast<ULONG>(size), 0));
    }
    if (ok) { ok = succeeded(BCryptFinishHash(hash, output, 32, 0)); }
    if (hash) { BCryptDestroyHash(hash); }
    if (algorithm) { BCryptCloseAlgorithmProvider(algorithm, 0); }
    return ok;
}

}
#endif

bool sha256(const uint8_t* input, std::size_t size, uint8_t output[32]) {
    if ((!input && size) || !output) { return false; }
#ifdef _WIN32
    return hashWithProvider(BCRYPT_SHA256_ALGORITHM, 0, nullptr, 0, input, size, output);
#else
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) { return false; }
    unsigned int produced = 0;
    const bool ok = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1 &&
                    (!size || EVP_DigestUpdate(context, input, size) == 1) &&
                    EVP_DigestFinal_ex(context, output, &produced) == 1 && produced == 32;
    EVP_MD_CTX_free(context);
    return ok;
#endif
}

bool hmacSha256(const uint8_t* key, std::size_t keySize, const uint8_t* input, std::size_t size,
                uint8_t output[32]) {
    if (!key || (!input && size) || !output) { return false; }
#ifdef _WIN32
    return hashWithProvider(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG,
                            key, keySize, input, size, output);
#else
    unsigned int produced = 0;
    return HMAC(EVP_sha256(), key, static_cast<int>(keySize), input, size, output, &produced) != nullptr && produced == 32;
#endif
}

bool decryptAes128Ecb(const uint8_t* key, const uint8_t* input, std::size_t size,
                      std::vector<uint8_t>& output) {
    if (!key || !input || size == 0 || size % 16 != 0) { output.clear(); return false; }
    output.assign(size, 0);
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
                                                  const_cast<PUCHAR>(key), 16, 0));
    }
    ULONG written = 0;
    if (ok) {
        ok = succeeded(BCryptDecrypt(aesKey, const_cast<PUCHAR>(input), static_cast<ULONG>(size), nullptr,
                                     nullptr, 0, output.data(), static_cast<ULONG>(output.size()), &written, 0)) &&
             written == size;
    }
    if (aesKey) { BCryptDestroyKey(aesKey); }
    if (algorithm) { BCryptCloseAlgorithmProvider(algorithm, 0); }
    if (!ok) { output.clear(); }
    return ok;
#else
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (!context) { output.clear(); return false; }
    int produced = 0;
    int finalProduced = 0;
    bool ok = EVP_DecryptInit_ex(context, EVP_aes_128_ecb(), nullptr, key, nullptr) == 1;
    if (ok) { ok = EVP_CIPHER_CTX_set_padding(context, 0) == 1; }
    if (ok) { ok = EVP_DecryptUpdate(context, output.data(), &produced, input, static_cast<int>(size)) == 1; }
    if (ok) { ok = EVP_DecryptFinal_ex(context, output.data() + produced, &finalProduced) == 1; }
    EVP_CIPHER_CTX_free(context);
    if (!ok || static_cast<std::size_t>(produced + finalProduced) != size) { output.clear(); return false; }
    return true;
#endif
}

bool encryptAes128Ecb(const uint8_t* key, const uint8_t* input, std::size_t size,
                      std::vector<uint8_t>& output) {
    if (!key || !input || size == 0 || size % 16 != 0) { output.clear(); return false; }
    output.assign(size, 0);
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
                                                  const_cast<PUCHAR>(key), 16, 0));
    }
    ULONG written = 0;
    if (ok) {
        ok = succeeded(BCryptEncrypt(aesKey, const_cast<PUCHAR>(input), static_cast<ULONG>(size), nullptr,
                                     nullptr, 0, output.data(), static_cast<ULONG>(output.size()), &written, 0)) &&
             written == size;
    }
    if (aesKey) { BCryptDestroyKey(aesKey); }
    if (algorithm) { BCryptCloseAlgorithmProvider(algorithm, 0); }
    if (!ok) { output.clear(); }
    return ok;
#else
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (!context) { output.clear(); return false; }
    int produced = 0;
    int finalProduced = 0;
    bool ok = EVP_EncryptInit_ex(context, EVP_aes_128_ecb(), nullptr, key, nullptr) == 1;
    if (ok) { ok = EVP_CIPHER_CTX_set_padding(context, 0) == 1; }
    if (ok) { ok = EVP_EncryptUpdate(context, output.data(), &produced, input, static_cast<int>(size)) == 1; }
    if (ok) { ok = EVP_EncryptFinal_ex(context, output.data() + produced, &finalProduced) == 1; }
    EVP_CIPHER_CTX_free(context);
    if (!ok || static_cast<std::size_t>(produced + finalProduced) != size) { output.clear(); return false; }
    return true;
#endif
}

}
