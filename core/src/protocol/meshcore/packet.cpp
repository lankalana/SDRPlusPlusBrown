#include "packet.h"

namespace protocol::meshcore {

bool parsePacket(const uint8_t* data, std::size_t size, Packet& packet, DecodeStatus& status) {
    if (!data || size < 2) { status = DecodeStatus::TooShort; return false; }
    packet = {};
    packet.header = data[0];
    packet.routeType = static_cast<RouteType>(packet.header & 0x03u);
    packet.payloadType = static_cast<PayloadType>((packet.header >> 2u) & 0x0Fu);
    packet.version = packet.header >> 6u;
    if (packet.version != 0) { status = DecodeStatus::UnsupportedVersion; return false; }
    packet.hasTransportCodes = packet.routeType == RouteType::TransportFlood ||
                               packet.routeType == RouteType::TransportDirect;
    std::size_t offset = 1;
    if (packet.hasTransportCodes) {
        if (size < offset + 5) { status = DecodeStatus::TooShort; return false; }
        packet.transportCodes[0] = static_cast<uint16_t>(data[offset]) |
                                   (static_cast<uint16_t>(data[offset + 1]) << 8u);
        packet.transportCodes[1] = static_cast<uint16_t>(data[offset + 2]) |
                                   (static_cast<uint16_t>(data[offset + 3]) << 8u);
        offset += 4;
    }
    packet.encodedPathLength = data[offset++];
    packet.pathHashCount = packet.encodedPathLength & 0x3Fu;
    packet.pathHashSize = static_cast<uint8_t>((packet.encodedPathLength >> 6u) + 1u);
    if (packet.pathHashSize == 4) { status = DecodeStatus::InvalidPath; return false; }
    const std::size_t pathBytes = static_cast<std::size_t>(packet.pathHashCount) * packet.pathHashSize;
    if (pathBytes > 64 || offset + pathBytes >= size) { status = DecodeStatus::InvalidPath; return false; }
    packet.path.assign(data + offset, data + offset + pathBytes);
    offset += pathBytes;
    packet.payload.assign(data + offset, data + size);
    status = DecodeStatus::Ok;
    return true;
}

const char* payloadTypeText(PayloadType type) {
    switch (type) {
        case PayloadType::Request: return "Request";
        case PayloadType::Response: return "Response";
        case PayloadType::TextMessage: return "Text";
        case PayloadType::Ack: return "ACK";
        case PayloadType::Advert: return "Advert";
        case PayloadType::GroupText: return "Group text";
        case PayloadType::GroupData: return "Group data";
        case PayloadType::AnonymousRequest: return "Anonymous request";
        case PayloadType::Path: return "Returned path";
        case PayloadType::Trace: return "Trace";
        case PayloadType::Multipart: return "Multipart";
        case PayloadType::Control: return "Control";
        case PayloadType::RawCustom: return "Raw custom";
        default: return "Reserved";
    }
}

const char* routeTypeText(RouteType type) {
    switch (type) {
        case RouteType::TransportFlood: return "Transport flood";
        case RouteType::Flood: return "Flood";
        case RouteType::Direct: return "Direct";
        case RouteType::TransportDirect: return "Transport direct";
        default: return "Unknown";
    }
}

const char* decodeStatusText(DecodeStatus status) {
    switch (status) {
        case DecodeStatus::Ok: return "ok";
        case DecodeStatus::LoRaCrcInvalid: return "LoRa CRC invalid";
        case DecodeStatus::TooShort: return "packet too short";
        case DecodeStatus::UnsupportedVersion: return "unsupported packet version";
        case DecodeStatus::InvalidPath: return "invalid path";
        case DecodeStatus::InvalidPayload: return "invalid payload";
        case DecodeStatus::UnknownChannel: return "unknown group channel";
        case DecodeStatus::AuthenticationFailed: return "group authentication failed";
        case DecodeStatus::CryptoFailed: return "crypto provider failed";
        default: return "unknown error";
    }
}

}
