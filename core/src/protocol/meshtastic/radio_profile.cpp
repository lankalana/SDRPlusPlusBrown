#include "radio_profile.h"

#include <stdexcept>

namespace protocol::meshtastic {

RadioProfile edgeFastLowProfile() {
    RadioProfile profile;
    profile.name = "EdgeFastLow (Finland)";
    return profile;
}

RadioProfile longFastProfile() {
    RadioProfile profile;
    profile.name = "LongFast";
    profile.mode = RadioMode::Preset;
    profile.preset = ModemPreset::LongFast;
    profile.bandwidth = 250000.0;
    profile.spreadingFactor = 11;
    profile.codingRate = 1;
    return profile;
}

double calculateFrequency(const RadioProfile& profile) {
    if (profile.frequencyOverride) { return *profile.frequencyOverride; }
    if (profile.region != Region::EU_868 || profile.bandwidth <= 0.0 || profile.frequencySlot < 1) {
        throw std::invalid_argument("invalid Meshtastic radio profile");
    }
    constexpr double EU868_START = 869400000.0;
    constexpr double EU868_END = 869650000.0;
    const double frequency = EU868_START + profile.bandwidth * (profile.frequencySlot - 0.5);
    if (frequency + profile.bandwidth * 0.5 > EU868_END) {
        throw std::out_of_range("Meshtastic frequency slot is outside EU_868");
    }
    return frequency;
}

dsp::protocol::lora::Config makeLoRaConfig(const RadioProfile& profile) {
    dsp::protocol::lora::Config config;
    config.bandwidth = static_cast<int>(profile.bandwidth);
    config.spreadingFactor = profile.spreadingFactor;
    config.codingRate = profile.codingRate;
    config.implicitHeader = false;
    config.payloadCrc = true;
    config.lowDataRateOptimize = dsp::protocol::lora::defaultLowDataRateOptimize(config.bandwidth,
                                                                                 config.spreadingFactor);
    config.syncWord = profile.syncWord;
    config.minimumPreambleSymbols = profile.preambleLength >= 8 ? 8 : profile.preambleLength;
    config.oversampling = 4;
    return config;
}

}
