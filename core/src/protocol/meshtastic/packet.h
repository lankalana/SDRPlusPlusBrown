#pragma once

#include <cstddef>
#include <cstdint>
#include "types.h"

namespace protocol::meshtastic {

enum class PacketParseStatus { Ok, TooShort, TooLong };

uint32_t readU32LE(const uint8_t* data);
PacketParseStatus parsePacket(const uint8_t* data, std::size_t size, const RxMetadata& metadata, Packet& packet);
const char* packetParseStatusText(PacketParseStatus status);

}
