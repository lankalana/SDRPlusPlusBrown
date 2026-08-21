#pragma once

#include "types.h"

namespace protocol::meshcore {

class Decoder {
public:
    void setChannels(std::vector<GroupChannel> configuredChannels);
    const std::vector<GroupChannel>& channels() const;
    DecodeResult decode(const uint8_t* data, std::size_t size, const RxMetadata& metadata,
                        bool loraCrcValid) const;

private:
    std::vector<GroupChannel> configuredChannels;
};

}
