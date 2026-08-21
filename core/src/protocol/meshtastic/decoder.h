#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "channel.h"
#include "types.h"

namespace protocol::meshtastic {

enum class DecodeStatus {
    Ok,
    LoRaCrcInvalid,
    TooShort,
    InvalidHeader,
    UnknownChannel,
    UnsupportedEncryption,
    DecryptionFailed,
    InvalidDataProtobuf
};

struct DecodeResult {
    DecodeStatus status = DecodeStatus::InvalidHeader;
    DecodedPacket decoded;
    std::size_t matchingChannels = 0;
};

class Decoder {
public:
    void setChannels(std::vector<Channel> channels);
    const std::vector<Channel>& getChannels() const { return channels; }
    DecodeResult decode(const uint8_t* payload, std::size_t size, const RxMetadata& metadata,
                        bool loraCrcValid = true) const;

private:
    std::vector<Channel> channels;
};

const char* decodeStatusText(DecodeStatus status);

}
