#include "decoder.h"

#include "crypto.h"
#include "packet.h"

#include <algorithm>
#include <cstring>

namespace protocol::meshcore {
namespace {

uint32_t readU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8u) |
           (static_cast<uint32_t>(data[2]) << 16u) |
           (static_cast<uint32_t>(data[3]) << 24u);
}

int32_t readI32(const uint8_t* data) { return static_cast<int32_t>(readU32(data)); }

uint16_t readU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8u);
}

bool parseAdvertisement(const std::vector<uint8_t>& payload, Advertisement& advertisement) {
    constexpr std::size_t FIXED_SIZE = 32 + 4 + 64;
    if (payload.size() < FIXED_SIZE) { return false; }
    std::copy_n(payload.data(), advertisement.publicKey.size(), advertisement.publicKey.data());
    advertisement.timestamp = readU32(payload.data() + 32);
    std::copy_n(payload.data() + 36, advertisement.signature.size(), advertisement.signature.data());
    std::size_t offset = FIXED_SIZE;
    if (offset == payload.size()) { return true; }
    advertisement.flags = payload[offset++];
    advertisement.nodeType = advertisement.flags & 0x0Fu;
    if (advertisement.flags & 0x10u) {
        if (offset + 8 > payload.size()) { return false; }
        advertisement.hasLocation = true;
        advertisement.latitude = readI32(payload.data() + offset) / 1000000.0;
        advertisement.longitude = readI32(payload.data() + offset + 4) / 1000000.0;
        offset += 8;
    }
    if (advertisement.flags & 0x20u) {
        if (offset + 2 > payload.size()) { return false; }
        advertisement.hasFeature1 = true;
        advertisement.feature1 = readU16(payload.data() + offset);
        offset += 2;
    }
    if (advertisement.flags & 0x40u) {
        if (offset + 2 > payload.size()) { return false; }
        advertisement.hasFeature2 = true;
        advertisement.feature2 = readU16(payload.data() + offset);
        offset += 2;
    }
    if (advertisement.flags & 0x80u) {
        const auto end = std::find(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.end(), 0);
        advertisement.name.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset), end);
    }
    return true;
}

DecodeStatus decryptGroup(const Packet& packet, const std::vector<GroupChannel>& channels,
                          DecodedPacket& decoded, std::size_t& matchingChannels) {
    if (packet.payload.size() < 19 || (packet.payload.size() - 3) % 16 != 0) {
        return DecodeStatus::InvalidPayload;
    }
    const uint8_t channelHash = packet.payload[0];
    for (const GroupChannel& channel : channels) {
        if (channel.hash != channelHash) { continue; }
        matchingChannels++;
        std::array<uint8_t, 32> paddedSecret{};
        std::copy(channel.secret.begin(), channel.secret.end(), paddedSecret.begin());
        uint8_t mac[32];
        const uint8_t* ciphertext = packet.payload.data() + 3;
        const std::size_t ciphertextSize = packet.payload.size() - 3;
        if (!hmacSha256(paddedSecret.data(), paddedSecret.size(), ciphertext, ciphertextSize, mac)) {
            return DecodeStatus::CryptoFailed;
        }
        if (mac[0] != packet.payload[1] || mac[1] != packet.payload[2]) {
            continue;
        }
        if (!decryptAes128Ecb(paddedSecret.data(), ciphertext, ciphertextSize, decoded.plaintext)) {
            return DecodeStatus::CryptoFailed;
        }
        decoded.channelName = channel.name;
        if (packet.payloadType == PayloadType::GroupText) {
            if (decoded.plaintext.size() < 5 || (decoded.plaintext[4] >> 2u) != 0) {
                return DecodeStatus::InvalidPayload;
            }
            decoded.hasGroupMessage = true;
            decoded.groupMessage.channelName = channel.name;
            decoded.groupMessage.channelHash = channelHash;
            decoded.groupMessage.timestamp = readU32(decoded.plaintext.data());
            decoded.groupMessage.textType = decoded.plaintext[4] >> 2u;
            decoded.groupMessage.attempt = decoded.plaintext[4] & 0x03u;
            const auto end = std::find(decoded.plaintext.begin() + 5, decoded.plaintext.end(), 0);
            decoded.groupMessage.text.assign(decoded.plaintext.begin() + 5, end);
        }
        return DecodeStatus::Ok;
    }
    return matchingChannels ? DecodeStatus::AuthenticationFailed : DecodeStatus::UnknownChannel;
}

}

void Decoder::setChannels(std::vector<GroupChannel> channels) { configuredChannels = std::move(channels); }

const std::vector<GroupChannel>& Decoder::channels() const { return configuredChannels; }

DecodeResult Decoder::decode(const uint8_t* data, std::size_t size, const RxMetadata& metadata,
                             bool loraCrcValid) const {
    DecodeResult result;
    if (!loraCrcValid) { result.status = DecodeStatus::LoRaCrcInvalid; return result; }
    if (!parsePacket(data, size, result.decoded.packet, result.status)) { return result; }
    result.decoded.packet.metadata = metadata;
    const Packet& packet = result.decoded.packet;
    switch (packet.payloadType) {
        case PayloadType::Advert:
            if (!parseAdvertisement(packet.payload, result.decoded.advertisement)) {
                result.status = DecodeStatus::InvalidPayload;
                return result;
            }
            result.decoded.hasAdvertisement = true;
            break;
        case PayloadType::GroupText:
        case PayloadType::GroupData:
            result.status = decryptGroup(packet, configuredChannels, result.decoded, result.matchingChannels);
            return result;
        case PayloadType::Ack:
            if (packet.payload.size() < 4) { result.status = DecodeStatus::InvalidPayload; return result; }
            result.decoded.hasAck = true;
            result.decoded.ackChecksum = readU32(packet.payload.data());
            break;
        case PayloadType::Request:
        case PayloadType::Response:
        case PayloadType::TextMessage:
        case PayloadType::Path:
            if (packet.payload.size() < 20 || (packet.payload.size() - 4) % 16 != 0) {
                result.status = DecodeStatus::InvalidPayload;
                return result;
            }
            result.decoded.hasPeerHashes = true;
            result.decoded.destinationHash = packet.payload[0];
            result.decoded.sourceHash = packet.payload[1];
            break;
        default:
            break;
    }
    result.status = DecodeStatus::Ok;
    return result;
}

}
