#include "protobuf.h"

#include <cstring>
#include <limits>

namespace protocol::meshtastic {
namespace {

class WireReader {
public:
    WireReader(const uint8_t* bytes, std::size_t size) : bytes(bytes), size(size) {}

    bool readVarint(uint64_t& value) {
        value = 0;
        for (int shift = 0; shift < 70 && pos < size; shift += 7) {
            const uint8_t byte = bytes[pos++];
            if (shift == 63 && (byte & 0xFEu) != 0) { return false; }
            value |= static_cast<uint64_t>(byte & 0x7Fu) << shift;
            if ((byte & 0x80u) == 0) { return true; }
        }
        return false;
    }

    bool readFixed32(uint32_t& value) {
        if (size - pos < 4) { return false; }
        value = static_cast<uint32_t>(bytes[pos]) |
                (static_cast<uint32_t>(bytes[pos + 1]) << 8u) |
                (static_cast<uint32_t>(bytes[pos + 2]) << 16u) |
                (static_cast<uint32_t>(bytes[pos + 3]) << 24u);
        pos += 4;
        return true;
    }

    bool readFloat(float& value) {
        uint32_t bits = 0;
        if (!readFixed32(bits)) { return false; }
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool readBytes(const uint8_t*& value, std::size_t& length) {
        uint64_t encodedLength = 0;
        if (!readVarint(encodedLength) || encodedLength > size - pos ||
            encodedLength > (std::numeric_limits<std::size_t>::max)()) { return false; }
        length = static_cast<std::size_t>(encodedLength);
        value = bytes + pos;
        pos += length;
        return true;
    }

    bool skip(uint32_t wireType) {
        uint64_t ignored = 0;
        const uint8_t* ignoredBytes = nullptr;
        std::size_t ignoredSize = 0;
        switch (wireType) {
        case 0: return readVarint(ignored);
        case 1: if (size - pos < 8) { return false; } pos += 8; return true;
        case 2: return readBytes(ignoredBytes, ignoredSize);
        case 5: if (size - pos < 4) { return false; } pos += 4; return true;
        default: return false;
        }
    }

    bool done() const { return pos == size; }

private:
    const uint8_t* bytes;
    std::size_t size;
    std::size_t pos = 0;
};

bool readString(WireReader& reader, std::string& value) {
    const uint8_t* bytes = nullptr;
    std::size_t size = 0;
    if (!reader.readBytes(bytes, size)) { return false; }
    value.assign(reinterpret_cast<const char*>(bytes), size);
    return true;
}

bool parseDeviceMetrics(const uint8_t* bytes, std::size_t size, Telemetry& parsed) {
    WireReader reader(bytes, size);
    parsed.kind = TelemetryKind::Device;
    while (!reader.done()) {
        uint64_t tag = 0;
        if (!reader.readVarint(tag) || (tag >> 3u) == 0) { return false; }
        const uint32_t field = static_cast<uint32_t>(tag >> 3u);
        const uint32_t wire = static_cast<uint32_t>(tag & 7u);
        uint64_t integer = 0;
        float floating = 0.0f;
        if (field == 1 && wire == 0) {
            if (!reader.readVarint(integer)) { return false; }
            parsed.batteryLevel = static_cast<uint32_t>(integer); parsed.hasBatteryLevel = true;
        }
        else if (field >= 2 && field <= 4 && wire == 5) {
            if (!reader.readFloat(floating)) { return false; }
            if (field == 2) { parsed.voltage = floating; parsed.hasVoltage = true; }
            else if (field == 3) { parsed.channelUtilization = floating; parsed.hasChannelUtilization = true; }
            else { parsed.airUtilTx = floating; parsed.hasAirUtilTx = true; }
        }
        else if (field == 5 && wire == 0) {
            if (!reader.readVarint(integer)) { return false; }
            parsed.uptimeSeconds = static_cast<uint32_t>(integer); parsed.hasUptime = true;
        }
        else if (!reader.skip(wire)) { return false; }
    }
    return true;
}

bool parseEnvironmentMetrics(const uint8_t* bytes, std::size_t size, Telemetry& parsed) {
    WireReader reader(bytes, size);
    parsed.kind = TelemetryKind::Environment;
    while (!reader.done()) {
        uint64_t tag = 0;
        if (!reader.readVarint(tag) || (tag >> 3u) == 0) { return false; }
        const uint32_t field = static_cast<uint32_t>(tag >> 3u);
        const uint32_t wire = static_cast<uint32_t>(tag & 7u);
        float value = 0.0f;
        if (field >= 1 && field <= 3 && wire == 5) {
            if (!reader.readFloat(value)) { return false; }
            if (field == 1) { parsed.temperature = value; parsed.hasTemperature = true; }
            else if (field == 2) { parsed.humidity = value; parsed.hasHumidity = true; }
            else { parsed.pressure = value; parsed.hasPressure = true; }
        }
        else if (!reader.skip(wire)) { return false; }
    }
    return true;
}

bool parseRouteDiscovery(const uint8_t* bytes, std::size_t size, Routing& parsed) {
    WireReader reader(bytes, size);
    while (!reader.done()) {
        uint64_t tag = 0;
        if (!reader.readVarint(tag) || (tag >> 3u) == 0) { return false; }
        const uint32_t field = static_cast<uint32_t>(tag >> 3u);
        const uint32_t wire = static_cast<uint32_t>(tag & 7u);
        uint32_t node = 0;
        if ((field == 1 || field == 3) && wire == 5) {
            if (!reader.readFixed32(node)) { return false; }
            (field == 1 ? parsed.route : parsed.routeBack).push_back(node);
        }
        else if (!reader.skip(wire)) { return false; }
    }
    return true;
}

}

bool parsePosition(const uint8_t* bytes, std::size_t size, Position& position) {
    WireReader reader(bytes, size);
    Position parsed;
    while (!reader.done()) {
        uint64_t tag = 0;
        if (!reader.readVarint(tag) || (tag >> 3u) == 0) { return false; }
        const uint32_t field = static_cast<uint32_t>(tag >> 3u);
        const uint32_t wire = static_cast<uint32_t>(tag & 7u);
        uint32_t fixed = 0;
        uint64_t varint = 0;
        if (field == 1 && wire == 5) {
            if (!reader.readFixed32(fixed)) { return false; }
            parsed.latitude = static_cast<int32_t>(fixed) * 1.0e-7; parsed.hasLatitude = true;
        }
        else if (field == 2 && wire == 5) {
            if (!reader.readFixed32(fixed)) { return false; }
            parsed.longitude = static_cast<int32_t>(fixed) * 1.0e-7; parsed.hasLongitude = true;
        }
        else if (field == 3 && wire == 0) {
            if (!reader.readVarint(varint)) { return false; }
            parsed.altitude = static_cast<int32_t>(static_cast<uint32_t>(varint)); parsed.hasAltitude = true;
        }
        else if ((field == 4 || field == 7) && wire == 5) {
            if (!reader.readFixed32(fixed)) { return false; }
            parsed.timestamp = fixed; parsed.hasTimestamp = true;
        }
        else if (!reader.skip(wire)) { return false; }
    }
    position = parsed;
    return parsed.hasLatitude || parsed.hasLongitude || parsed.hasAltitude || parsed.hasTimestamp;
}

bool parseNodeInfo(const uint8_t* bytes, std::size_t size, NodeInfo& nodeInfo) {
    WireReader reader(bytes, size);
    NodeInfo parsed;
    while (!reader.done()) {
        uint64_t tag = 0;
        if (!reader.readVarint(tag) || (tag >> 3u) == 0) { return false; }
        const uint32_t field = static_cast<uint32_t>(tag >> 3u);
        const uint32_t wire = static_cast<uint32_t>(tag & 7u);
        uint64_t value = 0;
        if (field >= 1 && field <= 3 && wire == 2) {
            std::string text;
            if (!readString(reader, text)) { return false; }
            if (field == 1) { parsed.id = std::move(text); }
            else if (field == 2) { parsed.longName = std::move(text); }
            else { parsed.shortName = std::move(text); }
        }
        else if ((field == 5 || field == 6 || field == 7) && wire == 0) {
            if (!reader.readVarint(value)) { return false; }
            if (field == 5) { parsed.hardwareModel = static_cast<uint32_t>(value); }
            else if (field == 6) { parsed.licensed = value != 0; }
            else { parsed.role = static_cast<uint32_t>(value); }
        }
        else if (!reader.skip(wire)) { return false; }
    }
    nodeInfo = parsed;
    return !parsed.id.empty() || !parsed.longName.empty() || !parsed.shortName.empty();
}

bool parseTelemetry(const uint8_t* bytes, std::size_t size, Telemetry& telemetry) {
    WireReader reader(bytes, size);
    Telemetry parsed;
    while (!reader.done()) {
        uint64_t tag = 0;
        if (!reader.readVarint(tag) || (tag >> 3u) == 0) { return false; }
        const uint32_t field = static_cast<uint32_t>(tag >> 3u);
        const uint32_t wire = static_cast<uint32_t>(tag & 7u);
        if (field == 1 && wire == 5) {
            if (!reader.readFixed32(parsed.time)) { return false; }
        }
        else if ((field == 2 || field == 3) && wire == 2) {
            const uint8_t* nested = nullptr;
            std::size_t nestedSize = 0;
            if (!reader.readBytes(nested, nestedSize) ||
                !(field == 2 ? parseDeviceMetrics(nested, nestedSize, parsed)
                             : parseEnvironmentMetrics(nested, nestedSize, parsed))) { return false; }
        }
        else if (!reader.skip(wire)) { return false; }
    }
    telemetry = parsed;
    return parsed.kind != TelemetryKind::Unknown || parsed.time != 0;
}

bool parseRouting(const uint8_t* bytes, std::size_t size, Routing& routing) {
    WireReader reader(bytes, size);
    Routing parsed;
    while (!reader.done()) {
        uint64_t tag = 0;
        if (!reader.readVarint(tag) || (tag >> 3u) == 0) { return false; }
        const uint32_t field = static_cast<uint32_t>(tag >> 3u);
        const uint32_t wire = static_cast<uint32_t>(tag & 7u);
        parsed.variant = field;
        if ((field == 1 || field == 2) && wire == 2) {
            const uint8_t* nested = nullptr;
            std::size_t nestedSize = 0;
            if (!reader.readBytes(nested, nestedSize) || !parseRouteDiscovery(nested, nestedSize, parsed)) { return false; }
        }
        else if (field == 3 && wire == 0) {
            uint64_t error = 0;
            if (!reader.readVarint(error)) { return false; }
            parsed.errorReason = static_cast<uint32_t>(error);
        }
        else if (!reader.skip(wire)) { return false; }
    }
    routing = std::move(parsed);
    return routing.variant != 0;
}

DataParseStatus parseData(const uint8_t* bytes, std::size_t size, Data& data) {
    if (!bytes && size != 0) { return DataParseStatus::Malformed; }
    WireReader reader(bytes, size);
    Data parsed;
    bool hasPort = false;
    bool hasPayload = false;
    while (!reader.done()) {
        uint64_t tag = 0;
        if (!reader.readVarint(tag) || (tag >> 3u) == 0) { return DataParseStatus::Malformed; }
        const uint32_t field = static_cast<uint32_t>(tag >> 3u);
        const uint32_t wire = static_cast<uint32_t>(tag & 7u);
        if (field == 1 && wire == 0) {
            uint64_t value = 0;
            if (!reader.readVarint(value) || value > UINT32_MAX) { return DataParseStatus::Malformed; }
            parsed.portNum = static_cast<uint32_t>(value); hasPort = true;
        }
        else if (field == 2 && wire == 2) {
            const uint8_t* payload = nullptr;
            std::size_t payloadSize = 0;
            if (!reader.readBytes(payload, payloadSize)) { return DataParseStatus::Malformed; }
            parsed.payload.assign(payload, payload + payloadSize); hasPayload = true;
        }
        else if (field == 3 && wire == 0) {
            uint64_t value = 0;
            if (!reader.readVarint(value)) { return DataParseStatus::Malformed; }
            parsed.wantResponse = value != 0;
        }
        else if (field >= 4 && field <= 8 && wire == 5) {
            uint32_t value = 0;
            if (!reader.readFixed32(value)) { return DataParseStatus::Malformed; }
            if (field == 4) { parsed.dest = value; parsed.hasDest = true; }
            else if (field == 5) { parsed.source = value; parsed.hasSource = true; }
            else if (field == 6) { parsed.requestId = value; parsed.hasRequestId = true; }
            else if (field == 7) { parsed.replyId = value; parsed.hasReplyId = true; }
            else { parsed.emoji = value; parsed.hasEmoji = true; }
        }
        else if (!reader.skip(wire)) { return DataParseStatus::Malformed; }
    }
    if (!hasPort) { return DataParseStatus::MissingPort; }
    if (!hasPayload) { return DataParseStatus::MissingPayload; }
    bool validApp = true;
    if (parsed.portNum == TEXT_MESSAGE_APP) {
        parsed.text.assign(reinterpret_cast<const char*>(parsed.payload.data()), parsed.payload.size());
    }
    else if (parsed.portNum == POSITION_APP) { validApp = parsePosition(parsed.payload.data(), parsed.payload.size(), parsed.position); }
    else if (parsed.portNum == NODEINFO_APP) { validApp = parseNodeInfo(parsed.payload.data(), parsed.payload.size(), parsed.nodeInfo); }
    else if (parsed.portNum == TELEMETRY_APP) { validApp = parseTelemetry(parsed.payload.data(), parsed.payload.size(), parsed.telemetry); }
    else if (parsed.portNum == ROUTING_APP) { validApp = parseRouting(parsed.payload.data(), parsed.payload.size(), parsed.routing); }
    if (!validApp) { return DataParseStatus::InvalidApplicationPayload; }
    data = std::move(parsed);
    return DataParseStatus::Ok;
}

const char* dataParseStatusText(DataParseStatus status) {
    switch (status) {
    case DataParseStatus::Ok: return "ok";
    case DataParseStatus::Malformed: return "malformed protobuf wire data";
    case DataParseStatus::MissingPort: return "Data.portnum is missing";
    case DataParseStatus::MissingPayload: return "Data.payload is missing";
    case DataParseStatus::InvalidApplicationPayload: return "invalid application protobuf";
    }
    return "unknown protobuf error";
}

}
