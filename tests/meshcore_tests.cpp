#include <protocol/meshcore/meshcore.h>
#include <protocol/meshcore/crypto.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace meshcore = protocol::meshcore;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (int i = 0; i < 4; i++) { bytes.push_back(static_cast<uint8_t>(value >> (8 * i))); }
}

void testPacketParser() {
    const std::vector<uint8_t> bytes = {
        0x18, 0x34, 0x12, 0x78, 0x56, 0x42, 0xaa, 0xbb, 0xcc, 0xdd, 0x99
    };
    meshcore::Packet packet;
    meshcore::DecodeStatus status;
    require(meshcore::parsePacket(bytes.data(), bytes.size(), packet, status), "transport packet parses");
    require(packet.routeType == meshcore::RouteType::TransportFlood &&
            packet.payloadType == meshcore::PayloadType::GroupData, "header fields parse");
    require(packet.transportCodes[0] == 0x1234 && packet.transportCodes[1] == 0x5678,
            "transport codes are little endian");
    require(packet.pathHashSize == 2 && packet.pathHashCount == 2 && packet.path.size() == 4,
            "variable-width path parses");
    require(packet.payload == std::vector<uint8_t>({ 0x99 }), "payload remainder parses");

    const std::vector<uint8_t> invalid = { 0x11, 0xc1, 0x01 };
    require(!meshcore::parsePacket(invalid.data(), invalid.size(), packet, status) &&
            status == meshcore::DecodeStatus::InvalidPath, "reserved path width rejected");
}

void testCrypto() {
    const std::array<uint8_t, 3> abc = { 'a', 'b', 'c' };
    const std::array<uint8_t, 32> expectedSha = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    std::array<uint8_t, 32> digest{};
    require(meshcore::sha256(abc.data(), abc.size(), digest.data()) && digest == expectedSha,
            "SHA-256 standard vector");

    const std::array<uint8_t, 20> hmacKey = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    const std::string hmacData = "Hi There";
    const std::array<uint8_t, 32> expectedHmac = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    require(meshcore::hmacSha256(hmacKey.data(), hmacKey.size(),
                                 reinterpret_cast<const uint8_t*>(hmacData.data()), hmacData.size(), digest.data()) &&
            digest == expectedHmac, "HMAC-SHA256 RFC 4231 vector");

    const std::array<uint8_t, 16> aesKey = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    const std::array<uint8_t, 16> ciphertext = {
        0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
        0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97
    };
    const std::array<uint8_t, 16> expectedPlaintext = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
    };
    std::vector<uint8_t> plaintext;
    require(meshcore::decryptAes128Ecb(aesKey.data(), ciphertext.data(), ciphertext.size(), plaintext) &&
            std::equal(plaintext.begin(), plaintext.end(), expectedPlaintext.begin()), "AES-128-ECB NIST vector");
}

void testAdvertisement() {
    std::vector<uint8_t> frame = { 0x11, 0x00 };
    for (uint8_t i = 0; i < 32; i++) { frame.push_back(i); }
    appendU32(frame, 0x12345678);
    frame.insert(frame.end(), 64, 0x55);
    frame.push_back(0x91);
    appendU32(frame, static_cast<uint32_t>(60192345));
    appendU32(frame, static_cast<uint32_t>(24945678));
    const std::string name = "MeshCore test";
    frame.insert(frame.end(), name.begin(), name.end());

    meshcore::Decoder decoder;
    const auto result = decoder.decode(frame.data(), frame.size(), { 7.0f, -80.0f, 120.0f }, true);
    require(result.status == meshcore::DecodeStatus::Ok && result.decoded.hasAdvertisement,
            "advertisement decodes");
    const auto& advert = result.decoded.advertisement;
    require(advert.name == name && advert.nodeType == 1 && advert.hasLocation, "advert app data decodes");
    require(std::abs(advert.latitude - 60.192345) < 0.000001 &&
            std::abs(advert.longitude - 24.945678) < 0.000001, "advert coordinates decode");
    require(!advert.signatureVerified, "unverified signature is explicit");
}

void testGroupMessage() {
    meshcore::GroupChannel channel;
    require(meshcore::makeGroupChannel("Public", meshcore::publicChannelSecret(), channel),
            "Public channel is constructed");
    std::vector<uint8_t> plaintext;
    appendU32(plaintext, 0x12345678);
    plaintext.push_back(0x00);
    const std::string text = "Alice: hello MeshCore";
    plaintext.insert(plaintext.end(), text.begin(), text.end());
    plaintext.resize((plaintext.size() + 15) & ~std::size_t(15), 0);
    std::vector<uint8_t> ciphertext;
    require(meshcore::encryptAes128Ecb(channel.secret.data(), plaintext.data(), plaintext.size(), ciphertext),
            "group fixture encrypts");
    std::array<uint8_t, 32> paddedSecret{};
    std::copy(channel.secret.begin(), channel.secret.end(), paddedSecret.begin());
    std::array<uint8_t, 32> mac{};
    require(meshcore::hmacSha256(paddedSecret.data(), paddedSecret.size(), ciphertext.data(), ciphertext.size(), mac.data()),
            "group fixture authenticates");

    std::vector<uint8_t> frame = { 0x15, 0x00, channel.hash, mac[0], mac[1] };
    frame.insert(frame.end(), ciphertext.begin(), ciphertext.end());
    meshcore::Decoder decoder;
    decoder.setChannels({ channel });
    const auto result = decoder.decode(frame.data(), frame.size(), {}, true);
    require(result.status == meshcore::DecodeStatus::Ok && result.decoded.hasGroupMessage,
            "authenticated group text decodes");
    require(result.decoded.groupMessage.text == text && result.decoded.groupMessage.timestamp == 0x12345678,
            "group text fields decode");
    frame[3] ^= 1;
    require(decoder.decode(frame.data(), frame.size(), {}, true).status == meshcore::DecodeStatus::AuthenticationFailed,
            "bad group MAC rejected");
    frame[2] ^= 1;
    require(decoder.decode(frame.data(), frame.size(), {}, true).status == meshcore::DecodeStatus::UnknownChannel,
            "unknown group channel is explicit");
    require(decoder.decode(frame.data(), frame.size(), {}, false).status == meshcore::DecodeStatus::LoRaCrcInvalid,
            "bad LoRa CRC rejected before protocol parsing");
}

void testRadioProfiles() {
    const auto narrow = meshcore::euNarrowProfile();
    const auto longRange = meshcore::euLongRangeProfile();
    const auto narrowLora = meshcore::makeLoRaConfig(narrow);
    require(narrow.frequency == 869618000.0 && narrowLora.bandwidth == 62500 &&
            narrowLora.spreadingFactor == 8 && narrowLora.codingRate == 1 && narrowLora.syncWord == 0x1424,
            "EU narrow profile maps to LoRa PHY");
    require(longRange.frequency == 869525000.0 && longRange.bandwidth == 250000.0 &&
            longRange.spreadingFactor == 11 && longRange.codingRate == 1, "EU long-range profile values");
}

}

int main() {
    testPacketParser();
    testCrypto();
    testAdvertisement();
    testGroupMessage();
    testRadioProfiles();
    std::cout << "MeshCore tests passed\n";
    return 0;
}
