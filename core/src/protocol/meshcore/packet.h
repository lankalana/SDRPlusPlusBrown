#pragma once

#include "types.h"

namespace protocol::meshcore {

bool parsePacket(const uint8_t* data, std::size_t size, Packet& packet, DecodeStatus& status);
const char* payloadTypeText(PayloadType type);
const char* routeTypeText(RouteType type);
const char* decodeStatusText(DecodeStatus status);

}
