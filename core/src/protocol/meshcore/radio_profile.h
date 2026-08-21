#pragma once

#include <dsp/protocol/lora/types.h>

#include <cstdint>
#include <string>

namespace protocol::meshcore {

struct RadioProfile {
    std::string name;
    double frequency = 869618000.0;
    double bandwidth = 62500.0;
    int spreadingFactor = 8;
    int codingRate = 1;
    int preambleLength = 8;
    uint16_t syncWord = 0x1424;
};

RadioProfile euNarrowProfile();
RadioProfile euLongRangeProfile();
dsp::protocol::lora::Config makeLoRaConfig(const RadioProfile& profile);

}
