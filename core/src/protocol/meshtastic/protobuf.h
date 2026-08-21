#pragma once

#include <cstddef>
#include <cstdint>
#include "types.h"

namespace protocol::meshtastic {

enum class DataParseStatus { Ok, Malformed, MissingPort, MissingPayload, InvalidApplicationPayload };

DataParseStatus parseData(const uint8_t* bytes, std::size_t size, Data& data);
bool parsePosition(const uint8_t* bytes, std::size_t size, Position& position);
bool parseNodeInfo(const uint8_t* bytes, std::size_t size, NodeInfo& nodeInfo);
bool parseTelemetry(const uint8_t* bytes, std::size_t size, Telemetry& telemetry);
bool parseRouting(const uint8_t* bytes, std::size_t size, Routing& routing);
const char* dataParseStatusText(DataParseStatus status);

}
