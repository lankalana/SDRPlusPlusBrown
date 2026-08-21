#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace protocol::meshcore {

enum class RouteType : uint8_t {
    TransportFlood = 0,
    Flood = 1,
    Direct = 2,
    TransportDirect = 3
};

enum class PayloadType : uint8_t {
    Request = 0,
    Response = 1,
    TextMessage = 2,
    Ack = 3,
    Advert = 4,
    GroupText = 5,
    GroupData = 6,
    AnonymousRequest = 7,
    Path = 8,
    Trace = 9,
    Multipart = 10,
    Control = 11,
    RawCustom = 15
};

enum class DecodeStatus {
    Ok,
    LoRaCrcInvalid,
    TooShort,
    UnsupportedVersion,
    InvalidPath,
    InvalidPayload,
    UnknownChannel,
    AuthenticationFailed,
    CryptoFailed
};

struct RxMetadata {
    float snrDb = 0.0f;
    float rssiDb = 0.0f;
    float frequencyErrorHz = 0.0f;
};

struct Packet {
    uint8_t header = 0;
    uint8_t version = 0;
    RouteType routeType = RouteType::Flood;
    PayloadType payloadType = PayloadType::RawCustom;
    bool hasTransportCodes = false;
    std::array<uint16_t, 2> transportCodes{};
    uint8_t encodedPathLength = 0;
    uint8_t pathHashSize = 1;
    uint8_t pathHashCount = 0;
    std::vector<uint8_t> path;
    std::vector<uint8_t> payload;
    RxMetadata metadata;
};

struct Advertisement {
    std::array<uint8_t, 32> publicKey{};
    uint32_t timestamp = 0;
    std::array<uint8_t, 64> signature{};
    uint8_t flags = 0;
    uint8_t nodeType = 0;
    bool hasLocation = false;
    double latitude = 0.0;
    double longitude = 0.0;
    bool hasFeature1 = false;
    uint16_t feature1 = 0;
    bool hasFeature2 = false;
    uint16_t feature2 = 0;
    std::string name;
    bool signatureVerified = false;
};

struct GroupChannel {
    std::string name;
    std::vector<uint8_t> secret;
    uint8_t hash = 0;
};

struct GroupMessage {
    std::string channelName;
    uint8_t channelHash = 0;
    uint32_t timestamp = 0;
    uint8_t textType = 0;
    uint8_t attempt = 0;
    std::string text;
};

struct DecodedPacket {
    Packet packet;
    std::string channelName;
    std::vector<uint8_t> plaintext;
    bool hasAdvertisement = false;
    Advertisement advertisement;
    bool hasGroupMessage = false;
    GroupMessage groupMessage;
    bool hasAck = false;
    uint32_t ackChecksum = 0;
    bool hasPeerHashes = false;
    uint8_t destinationHash = 0;
    uint8_t sourceHash = 0;
};

struct DecodeResult {
    DecodeStatus status = DecodeStatus::TooShort;
    DecodedPacket decoded;
    std::size_t matchingChannels = 0;
};

}
