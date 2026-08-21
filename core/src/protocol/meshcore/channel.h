#pragma once

#include "types.h"

#include <string_view>

namespace protocol::meshcore {

const std::vector<uint8_t>& publicChannelSecret();
bool decodeHexSecret(std::string_view text, std::vector<uint8_t>& secret);
bool decodeBase64Secret(std::string_view text, std::vector<uint8_t>& secret);
bool makeGroupChannel(std::string name, std::vector<uint8_t> secret, GroupChannel& channel);

}
