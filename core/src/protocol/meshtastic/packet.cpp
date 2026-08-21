#include "packet.h"

namespace protocol::meshtastic {

uint32_t readU32LE(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8u) |
           (static_cast<uint32_t>(data[2]) << 16u) |
           (static_cast<uint32_t>(data[3]) << 24u);
}

PacketParseStatus parsePacket(const uint8_t* data, std::size_t size, const RxMetadata& metadata, Packet& packet) {
    if (!data || size < RF_HEADER_SIZE) { return PacketParseStatus::TooShort; }
    if (size > 255) { return PacketParseStatus::TooLong; }
    Packet parsed;
    parsed.header.to = readU32LE(data);
    parsed.header.from = readU32LE(data + 4);
    parsed.header.id = readU32LE(data + 8);
    parsed.header.hopLimit = data[12] & 0x07u;
    parsed.header.wantAck = (data[12] & 0x08u) != 0;
    parsed.header.viaMqtt = (data[12] & 0x10u) != 0;
    parsed.header.hopStart = (data[12] >> 5u) & 0x07u;
    parsed.header.channelHash = data[13];
    parsed.header.nextHop = data[14];
    parsed.header.relayNode = data[15];
    parsed.encryptedPayload.assign(data + RF_HEADER_SIZE, data + size);
    parsed.metadata = metadata;
    packet = std::move(parsed);
    return PacketParseStatus::Ok;
}

const char* packetParseStatusText(PacketParseStatus status) {
    switch (status) {
    case PacketParseStatus::Ok: return "ok";
    case PacketParseStatus::TooShort: return "packet is shorter than the 16-byte RF header";
    case PacketParseStatus::TooLong: return "packet exceeds the LoRa payload limit";
    }
    return "unknown packet error";
}

}
