#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace protocol::meshtastic {

constexpr std::size_t RF_HEADER_SIZE = 16;
constexpr uint32_t BROADCAST_NODE = 0xFFFFFFFFu;

enum PortNum : uint32_t {
    UNKNOWN_APP = 0,
    TEXT_MESSAGE_APP = 1,
    POSITION_APP = 3,
    NODEINFO_APP = 4,
    ROUTING_APP = 5,
    TELEMETRY_APP = 67
};

struct RxMetadata {
    float snrDb = 0.0f;
    float rssiDb = 0.0f;
    float frequencyErrorHz = 0.0f;
};

struct Header {
    uint32_t to = 0;
    uint32_t from = 0;
    uint32_t id = 0;
    uint8_t hopLimit = 0;
    bool wantAck = false;
    bool viaMqtt = false;
    uint8_t hopStart = 0;
    uint8_t channelHash = 0;
    uint8_t nextHop = 0;
    uint8_t relayNode = 0;
};

struct Packet {
    Header header;
    std::vector<uint8_t> encryptedPayload;
    RxMetadata metadata;
};

struct Position {
    bool hasLatitude = false;
    bool hasLongitude = false;
    bool hasAltitude = false;
    bool hasTimestamp = false;
    double latitude = 0.0;
    double longitude = 0.0;
    int32_t altitude = 0;
    uint32_t timestamp = 0;
};

struct NodeInfo {
    std::string id;
    std::string longName;
    std::string shortName;
    uint32_t hardwareModel = 0;
    uint32_t role = 0;
    bool licensed = false;
};

enum class TelemetryKind {
    Unknown,
    Device,
    Environment
};

struct Telemetry {
    TelemetryKind kind = TelemetryKind::Unknown;
    uint32_t time = 0;
    bool hasBatteryLevel = false;
    uint32_t batteryLevel = 0;
    bool hasVoltage = false;
    float voltage = 0.0f;
    bool hasChannelUtilization = false;
    float channelUtilization = 0.0f;
    bool hasAirUtilTx = false;
    float airUtilTx = 0.0f;
    bool hasUptime = false;
    uint32_t uptimeSeconds = 0;
    bool hasTemperature = false;
    float temperature = 0.0f;
    bool hasHumidity = false;
    float humidity = 0.0f;
    bool hasPressure = false;
    float pressure = 0.0f;
};

struct Routing {
    uint32_t variant = 0;
    uint32_t errorReason = 0;
    std::vector<uint32_t> route;
    std::vector<uint32_t> routeBack;
};

struct Data {
    uint32_t portNum = 0;
    std::vector<uint8_t> payload;
    bool wantResponse = false;
    bool hasDest = false;
    bool hasSource = false;
    bool hasRequestId = false;
    bool hasReplyId = false;
    bool hasEmoji = false;
    uint32_t dest = 0;
    uint32_t source = 0;
    uint32_t requestId = 0;
    uint32_t replyId = 0;
    uint32_t emoji = 0;
    std::string text;
    Position position;
    NodeInfo nodeInfo;
    Telemetry telemetry;
    Routing routing;
};

struct DecodedPacket {
    Packet packet;
    std::string channelName;
    Data data;
    std::vector<uint8_t> plaintext;
};

}
