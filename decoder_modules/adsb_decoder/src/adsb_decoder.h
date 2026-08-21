#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace adsb {

constexpr std::size_t FRAME_BYTES = 14;
constexpr std::size_t FRAME_BITS = FRAME_BYTES * 8;
constexpr int DEMOD_SAMPLE_RATE = 4000000;

struct DecodedFrame {
    std::array<uint8_t, FRAME_BYTES> raw{};
    uint32_t icao = 0;
    int capability = 0;
    int typeCode = 0;
    int correctedBits = 0;
    float rssiDbfs = -120.0f;
    bool hasCallsign = false;
    bool hasCategory = false;
    bool hasAltitude = false;
    std::string callsign;
    std::string category;
    int altitudeFeet = 0;
};

uint32_t crc24(const std::array<uint8_t, FRAME_BYTES>& frame);
std::string frameToHex(const std::array<uint8_t, FRAME_BYTES>& frame);
bool decodeAltitude(int ac12, int& altitudeFeet);
bool decodeFrame(std::array<uint8_t, FRAME_BYTES> frame, int maxCorrections, float rssiDbfs, DecodedFrame& decoded);

struct DemodStats {
    uint64_t valid = 0;
    uint64_t corrected = 0;
};

class StreamDecoder {
public:
    using FrameHandler = std::function<void(const DecodedFrame&)>;

    explicit StreamDecoder(FrameHandler handler = {});

    void setFrameHandler(FrameHandler handler);
    void setMaxCorrections(int maxCorrections);
    void process(const float* power, std::size_t count);
    void reset();
    const DemodStats& stats() const;

private:
    bool isPreamble(std::size_t offset, float& signalPower) const;
    bool demodulate(std::size_t offset, std::array<uint8_t, FRAME_BYTES>& frame) const;

    FrameHandler frameHandler;
    std::vector<float> samples;
    int maxCorrections = 1;
    DemodStats demodStats;
};

struct AircraftState {
    uint32_t icao = 0;
    std::string callsign;
    std::string category;
    bool hasAltitude = false;
    int altitudeFeet = 0;
    float rssiDbfs = -120.0f;
    int lastCorrectedBits = 0;
    uint64_t correctedMessages = 0;
    uint64_t messageCount = 0;
    uint64_t firstSeenOrder = 0;
    int64_t lastSeenMillis = 0;
    std::string lastRawFrame;
};

class AircraftTracker {
public:
    explicit AircraftTracker(std::size_t capacity = 512);

    void update(const DecodedFrame& frame, int64_t timestampMillis);
    void expire(int64_t timestampMillis, int retentionSeconds);
    std::vector<AircraftState> snapshot() const;
    void clear();
    std::size_t size() const;

private:
    void evictOldest();

    std::size_t capacity;
    uint64_t nextOrder = 0;
    std::map<uint32_t, AircraftState> aircraft;
};

}
