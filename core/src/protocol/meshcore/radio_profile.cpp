#include "radio_profile.h"

#include <stdexcept>

namespace protocol::meshcore {

RadioProfile euNarrowProfile() { return {}; }

RadioProfile euLongRangeProfile() {
    RadioProfile profile;
    profile.name = "EU/UK Long Range";
    profile.frequency = 869525000.0;
    profile.bandwidth = 250000.0;
    profile.spreadingFactor = 11;
    return profile;
}

dsp::protocol::lora::Config makeLoRaConfig(const RadioProfile& profile) {
    if (profile.frequency <= 0.0 || profile.bandwidth <= 0.0 || profile.spreadingFactor < 5 ||
        profile.spreadingFactor > 12 || profile.codingRate < 1 || profile.codingRate > 4) {
        throw std::invalid_argument("invalid MeshCore radio profile");
    }
    dsp::protocol::lora::Config config;
    config.bandwidth = static_cast<int>(profile.bandwidth);
    config.spreadingFactor = profile.spreadingFactor;
    config.codingRate = profile.codingRate;
    config.implicitHeader = false;
    config.payloadCrc = true;
    config.lowDataRateOptimize = dsp::protocol::lora::defaultLowDataRateOptimize(config.bandwidth,
                                                                                 config.spreadingFactor);
    config.syncWord = profile.syncWord;
    config.minimumPreambleSymbols = profile.preambleLength;
    config.oversampling = 4;
    return config;
}

}
