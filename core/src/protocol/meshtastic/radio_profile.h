#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <dsp/protocol/lora/types.h>

namespace protocol::meshtastic {

enum class RadioMode { Preset, Custom };
enum class Region { EU_868 };
enum class ModemPreset { LongFast, EdgeFastLow };

struct RadioProfile {
    std::string name;
    RadioMode mode = RadioMode::Custom;
    ModemPreset preset = ModemPreset::EdgeFastLow;
    double bandwidth = 62500.0;
    int spreadingFactor = 8;
    int codingRate = 4;
    int preambleLength = 16;
    uint16_t syncWord = 0x2B;
    Region region = Region::EU_868;
    int frequencySlot = 1;
    std::optional<double> frequencyOverride;
};

RadioProfile edgeFastLowProfile();
RadioProfile longFastProfile();
double calculateFrequency(const RadioProfile& profile);
dsp::protocol::lora::Config makeLoRaConfig(const RadioProfile& profile);

}
