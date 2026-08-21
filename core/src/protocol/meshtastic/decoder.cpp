#include "decoder.h"

#include "crypto.h"
#include "packet.h"
#include "protobuf.h"

namespace protocol::meshtastic {

void Decoder::setChannels(std::vector<Channel> newChannels) {
    channels = std::move(newChannels);
}

DecodeResult Decoder::decode(const uint8_t* payload, std::size_t size, const RxMetadata& metadata,
                             bool loraCrcValid) const {
    DecodeResult result;
    if (!loraCrcValid) { result.status = DecodeStatus::LoRaCrcInvalid; return result; }
    Packet packet;
    const PacketParseStatus packetStatus = parsePacket(payload, size, metadata, packet);
    if (packetStatus == PacketParseStatus::TooShort) { result.status = DecodeStatus::TooShort; return result; }
    if (packetStatus != PacketParseStatus::Ok) { result.status = DecodeStatus::InvalidHeader; return result; }

    bool decryptAttempted = false;
    bool protobufAttempted = false;
    for (const Channel& channel : channels) {
        if (!channel.enabled || channel.hash != packet.header.channelHash) { continue; }
        result.matchingChannels++;
        std::vector<uint8_t> plaintext;
        if (channel.psk.empty()) {
            plaintext = packet.encryptedPayload;
        }
        else {
            decryptAttempted = true;
            if (!cryptChannelPayload(packet.encryptedPayload.data(), packet.encryptedPayload.size(),
                                     channel.psk.data(), channel.psk.size(),
                                     makeNonce(packet.header.id, packet.header.from), plaintext)) { continue; }
        }
        protobufAttempted = true;
        Data data;
        if (parseData(plaintext.data(), plaintext.size(), data) != DataParseStatus::Ok || data.portNum == UNKNOWN_APP) {
            continue;
        }
        result.status = DecodeStatus::Ok;
        result.decoded.packet = packet;
        result.decoded.channelName = channel.name;
        result.decoded.data = std::move(data);
        result.decoded.plaintext = std::move(plaintext);
        return result;
    }

    if (!result.matchingChannels) {
        const bool possiblePki = packet.header.channelHash == 0 && packet.header.to != BROADCAST_NODE &&
                                 packet.header.to != 0 && packet.encryptedPayload.size() > 12;
        result.status = possiblePki ? DecodeStatus::UnsupportedEncryption : DecodeStatus::UnknownChannel;
    }
    else if (protobufAttempted) { result.status = DecodeStatus::InvalidDataProtobuf; }
    else if (decryptAttempted) { result.status = DecodeStatus::DecryptionFailed; }
    else { result.status = DecodeStatus::InvalidDataProtobuf; }
    result.decoded.packet = std::move(packet);
    return result;
}

const char* decodeStatusText(DecodeStatus status) {
    switch (status) {
    case DecodeStatus::Ok: return "ok";
    case DecodeStatus::LoRaCrcInvalid: return "LoRa CRC invalid";
    case DecodeStatus::TooShort: return "RF payload too short";
    case DecodeStatus::InvalidHeader: return "invalid RF header";
    case DecodeStatus::UnknownChannel: return "unknown channel";
    case DecodeStatus::UnsupportedEncryption: return "PKI encrypted / unavailable with channel key";
    case DecodeStatus::DecryptionFailed: return "channel decryption failed";
    case DecodeStatus::InvalidDataProtobuf: return "invalid Data protobuf or wrong key";
    }
    return "unknown decode status";
}

}
