#include <protocol/meshtastic/meshtastic.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace meshtastic = protocol::meshtastic;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void appendFixed32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (int i = 0; i < 4; i++) { bytes.push_back(static_cast<uint8_t>(value >> (8 * i))); }
}

void appendFloat(std::vector<uint8_t>& bytes, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendFixed32(bytes, bits);
}

void appendBytes(std::vector<uint8_t>& bytes, uint8_t field, const std::string& value) {
    bytes.push_back(static_cast<uint8_t>((field << 3u) | 2u));
    bytes.push_back(static_cast<uint8_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

std::vector<uint8_t> wrapData(uint32_t port, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> data = { 0x08, static_cast<uint8_t>(port), 0x12, static_cast<uint8_t>(payload.size()) };
    data.insert(data.end(), payload.begin(), payload.end());
    data.push_back(0x18);
    data.push_back(0x01);
    return data;
}

void testHeader() {
    const std::array<uint8_t, 19> bytes = {
        0xff, 0xff, 0xff, 0xff, 0x42, 0x42, 0xfb, 0xf7,
        0x13, 0x62, 0xd0, 0xd4, 0x5a, 0x55, 0x23, 0x42,
        0xaa, 0xbb, 0xcc
    };
    meshtastic::Packet packet;
    require(meshtastic::parsePacket(bytes.data(), bytes.size(), { -6.5f, -90.0f, 412.0f }, packet) ==
            meshtastic::PacketParseStatus::Ok, "RF header parses");
    require(packet.header.to == meshtastic::BROADCAST_NODE, "broadcast destination");
    require(packet.header.from == 0xf7fb4242u, "sender little endian");
    require(packet.header.id == 0xd4d06213u, "packet id little endian");
    require(packet.header.hopLimit == 2 && packet.header.hopStart == 2, "hop flags");
    require(packet.header.wantAck && packet.header.viaMqtt, "boolean flags");
    require(packet.header.channelHash == 0x55 && packet.header.nextHop == 0x23 && packet.header.relayNode == 0x42,
            "trailing header fields");
    require(packet.encryptedPayload == std::vector<uint8_t>({0xaa, 0xbb, 0xcc}), "encrypted payload copy");
    require(meshtastic::parsePacket(bytes.data(), 15, {}, packet) == meshtastic::PacketParseStatus::TooShort,
            "short RF packet rejected");
}

void testChannelAndNonce() {
    require(meshtastic::calculateChannelHash("EdgeFastLow", meshtastic::defaultPsk().data(),
                                             meshtastic::defaultPsk().size()) == 0x55, "EFL channel hash vector");
    const auto nonce = meshtastic::makeNonce(0xd4d06213u, 0xf7fb4242u);
    const std::array<uint8_t, 16> expected = {
        0x13, 0x62, 0xd0, 0xd4, 0, 0, 0, 0, 0x42, 0x42, 0xfb, 0xf7, 0, 0, 0, 0
    };
    require(nonce == expected, "Meshtastic nonce layout");
    const uint8_t shorthand = 1;
    require(meshtastic::expandPsk(&shorthand, 1) == meshtastic::defaultPsk(), "AQ== shorthand expands");
    require(meshtastic::makeChannel("EdgeFastLow", { 1 }).hash == 0x55, "channel model computes hash");
    std::vector<uint8_t> parsedPsk;
    require(meshtastic::decodeBase64Psk("AQ==", parsedPsk) && parsedPsk == std::vector<uint8_t>({ 1 }),
            "Base64 PSK parses");
    require(meshtastic::decodeHexPsk("01", parsedPsk) && parsedPsk == std::vector<uint8_t>({ 1 }),
            "hex PSK parses");
}

void testAesCtr() {
    const std::array<uint8_t, 16> key = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    const std::array<uint8_t, 16> nonce = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };
    const std::array<uint8_t, 32> ciphertext = {
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26, 0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff, 0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff
    };
    const std::array<uint8_t, 32> expected = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51
    };
    std::vector<uint8_t> plaintext;
    require(meshtastic::cryptChannelPayload(ciphertext.data(), ciphertext.size(), key.data(), key.size(), nonce,
                                             plaintext), "AES-128-CTR provider succeeds");
    require(std::equal(plaintext.begin(), plaintext.end(), expected.begin()), "NIST AES-128-CTR vector");

    const std::array<uint8_t, 32> key256 = {
        0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, 0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
        0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7, 0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
    };
    const std::array<uint8_t, 32> ciphertext256 = {
        0x60, 0x1e, 0xc3, 0x13, 0x77, 0x57, 0x89, 0xa5, 0xb7, 0xa7, 0xf5, 0x04, 0xbb, 0xf3, 0xd2, 0x28,
        0xf4, 0x43, 0xe3, 0xca, 0x4d, 0x62, 0xb5, 0x9a, 0xca, 0x84, 0xe9, 0x90, 0xca, 0xca, 0xf5, 0xc5
    };
    require(meshtastic::cryptChannelPayload(ciphertext256.data(), ciphertext256.size(), key256.data(), key256.size(),
                                             nonce, plaintext), "AES-256-CTR provider succeeds");
    require(std::equal(plaintext.begin(), plaintext.end(), expected.begin()), "NIST AES-256-CTR vector");
}

void testTextData() {
    const std::string message = "EFL SDR TEST 001";
    const auto encoded = wrapData(meshtastic::TEXT_MESSAGE_APP,
                                  std::vector<uint8_t>(message.begin(), message.end()));
    meshtastic::Data data;
    require(meshtastic::parseData(encoded.data(), encoded.size(), data) == meshtastic::DataParseStatus::Ok,
            "text Data parses");
    require(data.portNum == meshtastic::TEXT_MESSAGE_APP && data.text == message && data.wantResponse,
            "text Data fields");
}

void testPositionData() {
    std::vector<uint8_t> position = { 0x0d };
    appendFixed32(position, static_cast<uint32_t>(601923456));
    position.push_back(0x15);
    appendFixed32(position, static_cast<uint32_t>(249456789));
    position.push_back(0x18);
    position.push_back(123);
    position.push_back(0x3d);
    appendFixed32(position, 1720000000u);
    position.push_back(0x98);
    position.push_back(0x01);
    position.push_back(16);
    const auto encoded = wrapData(meshtastic::POSITION_APP, position);
    meshtastic::Data data;
    require(meshtastic::parseData(encoded.data(), encoded.size(), data) == meshtastic::DataParseStatus::Ok,
            "position Data parses");
    require(std::abs(data.position.latitude - 60.1923456) < 1e-7, "position latitude");
    require(std::abs(data.position.longitude - 24.9456789) < 1e-7, "position longitude");
    require(data.position.altitude == 123 && data.position.timestamp == 1720000000u,
            "position altitude and timestamp");
}

void testNodeInfoData() {
    std::vector<uint8_t> user;
    appendBytes(user, 1, "!f7fb4242");
    appendBytes(user, 2, "EFL Test Node");
    appendBytes(user, 3, "EFL1");
    user.push_back(0x28);
    user.push_back(43);
    const auto encoded = wrapData(meshtastic::NODEINFO_APP, user);
    meshtastic::Data data;
    require(meshtastic::parseData(encoded.data(), encoded.size(), data) == meshtastic::DataParseStatus::Ok,
            "NodeInfo Data parses");
    require(data.nodeInfo.id == "!f7fb4242" && data.nodeInfo.longName == "EFL Test Node" &&
            data.nodeInfo.shortName == "EFL1", "NodeInfo names");
}

void testTelemetryAndRouting() {
    std::vector<uint8_t> device = { 0x08, 87, 0x15 };
    appendFloat(device, 4.12f);
    device.push_back(0x1d);
    appendFloat(device, 12.5f);
    std::vector<uint8_t> telemetry = { 0x0d };
    appendFixed32(telemetry, 1720000000u);
    telemetry.push_back(0x12);
    telemetry.push_back(static_cast<uint8_t>(device.size()));
    telemetry.insert(telemetry.end(), device.begin(), device.end());
    auto encoded = wrapData(meshtastic::TELEMETRY_APP, telemetry);
    meshtastic::Data data;
    require(meshtastic::parseData(encoded.data(), encoded.size(), data) == meshtastic::DataParseStatus::Ok,
            "telemetry Data parses");
    require(data.telemetry.kind == meshtastic::TelemetryKind::Device && data.telemetry.batteryLevel == 87 &&
            std::abs(data.telemetry.voltage - 4.12f) < 0.001f, "device telemetry fields");

    const std::vector<uint8_t> routing = { 0x18, 0x01 };
    encoded = wrapData(meshtastic::ROUTING_APP, routing);
    require(meshtastic::parseData(encoded.data(), encoded.size(), data) == meshtastic::DataParseStatus::Ok &&
            data.routing.variant == 3 && data.routing.errorReason == 1, "routing error parses");
}

void testRadioProfiles() {
    const auto efl = meshtastic::edgeFastLowProfile();
    const auto longFast = meshtastic::longFastProfile();
    require(std::abs(meshtastic::calculateFrequency(efl) - 869431250.0) < 1.0, "EFL slot frequency");
    require(std::abs(meshtastic::calculateFrequency(longFast) - 869525000.0) < 1.0, "LongFast slot frequency");
    const auto config = meshtastic::makeLoRaConfig(efl);
    require(config.bandwidth == 62500 && config.spreadingFactor == 8 && config.codingRate == 4 &&
            config.syncWord == 0x2B, "EFL profile maps to LoRa PHY");
}

void testCompletePacket() {
    const std::string message = "encrypted EFL text";
    const auto plaintext = wrapData(meshtastic::TEXT_MESSAGE_APP,
                                    std::vector<uint8_t>(message.begin(), message.end()));
    const uint32_t packetId = 0x12345678u;
    const uint32_t from = 0xabcdef01u;
    std::vector<uint8_t> ciphertext;
    require(meshtastic::cryptChannelPayload(plaintext.data(), plaintext.size(), meshtastic::defaultPsk().data(),
                                             meshtastic::defaultPsk().size(), meshtastic::makeNonce(packetId, from),
                                             ciphertext), "test packet encryption");
    std::vector<uint8_t> rf;
    appendFixed32(rf, meshtastic::BROADCAST_NODE);
    appendFixed32(rf, from);
    appendFixed32(rf, packetId);
    rf.insert(rf.end(), { 0x43, 0x55, 0x00, 0x01 });
    rf.insert(rf.end(), ciphertext.begin(), ciphertext.end());

    meshtastic::Packet packet;
    require(meshtastic::parsePacket(rf.data(), rf.size(), { -4.0f, -80.0f, 250.0f }, packet) ==
            meshtastic::PacketParseStatus::Ok, "complete RF packet parses");
    std::vector<uint8_t> decrypted;
    require(meshtastic::cryptChannelPayload(packet.encryptedPayload.data(), packet.encryptedPayload.size(),
                                             meshtastic::defaultPsk().data(), meshtastic::defaultPsk().size(),
                                             meshtastic::makeNonce(packet.header.id, packet.header.from), decrypted),
            "complete RF packet decrypts");
    meshtastic::Data data;
    require(meshtastic::parseData(decrypted.data(), decrypted.size(), data) == meshtastic::DataParseStatus::Ok &&
            data.text == message, "complete packet reaches text");

    meshtastic::Decoder decoder;
    std::vector<uint8_t> wrongCollisionKey = meshtastic::defaultPsk();
    wrongCollisionKey[0] ^= 1u;
    wrongCollisionKey[1] ^= 1u;
    decoder.setChannels({ meshtastic::makeChannel("EdgeFastLow", wrongCollisionKey),
                          meshtastic::makeChannel("EdgeFastLow", { 1 }) });
    const auto result = decoder.decode(rf.data(), rf.size(), { -4.0f, -80.0f, 250.0f });
    require(result.status == meshtastic::DecodeStatus::Ok && result.decoded.data.text == message &&
            result.matchingChannels == 2, "hash collision tries channels until protobuf is valid");
    rf[13] = 0;
    const auto unknown = decoder.decode(rf.data(), rf.size(), {});
    require(unknown.status == meshtastic::DecodeStatus::UnknownChannel, "unknown channel is explicit");

    std::vector<uint8_t> pki;
    appendFixed32(pki, 0x12345678u);
    appendFixed32(pki, from);
    appendFixed32(pki, packetId);
    pki.insert(pki.end(), { 0x00, 0x00, 0x00, 0x00 });
    pki.resize(29, 0x42);
    require(decoder.decode(pki.data(), pki.size(), {}).status == meshtastic::DecodeStatus::UnsupportedEncryption,
            "PKI candidate is distinguished from an unknown channel");

    const auto clearChannel = meshtastic::makeChannel("clear", {});
    std::vector<uint8_t> clearRf;
    appendFixed32(clearRf, meshtastic::BROADCAST_NODE);
    appendFixed32(clearRf, from);
    appendFixed32(clearRf, packetId);
    clearRf.insert(clearRf.end(), { 0x00, clearChannel.hash, 0x00, 0x00 });
    clearRf.insert(clearRf.end(), plaintext.begin(), plaintext.end());
    decoder.setChannels({ clearChannel });
    require(decoder.decode(clearRf.data(), clearRf.size(), {}).status == meshtastic::DecodeStatus::Ok,
            "unencrypted channel decodes");
}

}

int main() {
    testHeader();
    testChannelAndNonce();
    testAesCtr();
    testTextData();
    testPositionData();
    testNodeInfoData();
    testTelemetryAndRouting();
    testRadioProfiles();
    testCompletePacket();
    std::cout << "Meshtastic EFL tests passed\n";
    return 0;
}
