#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp::protocol::lora {

struct Config {
    int bandwidth = 125000;
    int spreadingFactor = 7;
    int codingRate = 1;
    bool implicitHeader = false;
    bool payloadCrc = true;
    bool lowDataRateOptimize = false;
    int implicitPayloadLength = 0;
    uint16_t syncWord = 0x12;
    int minimumPreambleSymbols = 6;
    int oversampling = 4;
};

struct DerivedConfig {
    int bins = 128;
    int samplesPerSymbol = 512;
    int effectiveSpreadingFactor = 7;
    int headerSpreadingFactor = 5;
    int payloadSymbolsPerBlock = 5;
    int headerSymbolsPerBlock = 8;
    bool lowDataRateOptimize = false;
    double internalSampleRate = 500000.0;
};

inline bool defaultLowDataRateOptimize(int bandwidth, int spreadingFactor) {
    if (bandwidth <= 0 || spreadingFactor < 5 || spreadingFactor > 12) { return false; }
    return (static_cast<double>(1 << spreadingFactor) / static_cast<double>(bandwidth)) >= 0.016;
}

inline bool validateConfig(const Config& config, std::string* error = nullptr) {
    auto fail = [error](const char* message) {
        if (error) { *error = message; }
        return false;
    };

    if (config.bandwidth <= 0) { return fail("bandwidth must be positive"); }
    if (config.spreadingFactor < 5 || config.spreadingFactor > 12) { return fail("spreading factor must be in [5, 12]"); }
    if (config.codingRate < 1 || config.codingRate > 4) { return fail("coding rate must be in [1, 4]"); }
    if (config.minimumPreambleSymbols < 5) { return fail("at least five preamble symbols are required"); }
    if (config.oversampling < 1 || config.oversampling > 8) { return fail("oversampling must be in [1, 8]"); }
    if (config.implicitHeader && (config.implicitPayloadLength <= 0 || config.implicitPayloadLength > 255)) {
        return fail("implicit payload length must be in [1, 255]");
    }
    return true;
}

inline DerivedConfig derive(const Config& config) {
    DerivedConfig derived;
    derived.bins = 1 << config.spreadingFactor;
    derived.samplesPerSymbol = config.oversampling * derived.bins;
    derived.lowDataRateOptimize = config.lowDataRateOptimize;
    derived.effectiveSpreadingFactor = config.spreadingFactor - (derived.lowDataRateOptimize ? 2 : 0);
    derived.headerSpreadingFactor = config.spreadingFactor - 2;
    derived.payloadSymbolsPerBlock = derived.effectiveSpreadingFactor;
    derived.internalSampleRate = static_cast<double>(config.oversampling) * config.bandwidth;
    return derived;
}

struct Frame {
    std::vector<uint8_t> payload;
    int spreadingFactor = 0;
    int bandwidth = 0;
    int codingRate = 0;
    bool implicitHeader = false;
    bool headerValid = false;
    bool payloadCrcPresent = false;
    bool payloadCrcValid = false;
    float snrDb = 0.0f;
    float rssiDb = 0.0f;
    float frequencyErrorHz = 0.0f;
    float timingOffsetSamples = 0.0f;
    uint16_t syncWord = 0;
    uint32_t correctedCodewords = 0;
    uint32_t invalidCodewords = 0;
};

using FrameHandler = void(*)(const Frame&, void* ctx);

struct Stats {
    uint64_t preamblesDetected = 0;
    uint64_t syncFailures = 0;
    uint64_t headersDecoded = 0;
    uint64_t headerFailures = 0;
    uint64_t payloadsDecoded = 0;
    uint64_t payloadCrcFailures = 0;
};

struct SyncDiagnostics {
    float cfoHz = 0.0f;
    float timingOffset = 0.0f;
    float preambleConfidence = 0.0f;
    float fftPeakRatio = 0.0f;
    uint16_t observedSyncWord = 0;
    float preambleBin = 0.0f;
    float downchirpBin = 0.0f;
    float firstSyncBin = 0.0f;
    float secondSyncBin = 0.0f;
};

struct SymbolEstimate {
    int symbol = 0;
    float peakMagnitude = 0.0f;
    float secondPeakMagnitude = 0.0f;
    float fractionalBin = 0.0f;

    float peakRatio() const {
        return peakMagnitude / (secondPeakMagnitude > 1.0e-20f ? secondPeakMagnitude : 1.0e-20f);
    }
};

}
