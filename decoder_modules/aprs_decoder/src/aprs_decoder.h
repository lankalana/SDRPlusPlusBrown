#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace aprs {

constexpr int SAMPLE_RATE = 48000;
constexpr int BAUD_RATE = 1200;

struct IQSample {
    float re;
    float im;
};

struct Packet {
    std::vector<uint8_t> raw;
    std::string source;
    std::string destination;
    std::vector<std::string> path;
    std::string information;
    std::string type;
    double rfFrequency = 0.0;
    bool hasPosition = false;
    double latitude = 0.0;
    double longitude = 0.0;
    char symbolTable = 0;
    char symbolCode = 0;
};

uint16_t crc16(const uint8_t* data, std::size_t count);
bool decodeAX25(const std::vector<uint8_t>& frame, Packet& packet);
std::string formatTNC2(const Packet& packet);
std::string formatLogLine(const Packet& packet, int64_t timestampMillis);
double selectAPRSFrequency(double centerFrequency, double visibleBandwidth, double channelBandwidth);

struct DecoderStats {
    uint64_t frames = 0;
    uint64_t crcErrors = 0;
};

class Decoder {
public:
    using PacketHandler = std::function<void(const Packet&)>;

    explicit Decoder(PacketHandler handler = {});

    void setPacketHandler(PacketHandler handler);
    void process(const IQSample* samples, std::size_t count);
    void reset();
    const DecoderStats& stats() const;

private:
    void processAudio(float sample);
    void processBit(bool bit);
    void finishFrame();

    PacketHandler packetHandler;
    DecoderStats decoderStats;
    IQSample previousIQ{};
    bool hasPreviousIQ = false;
    std::array<float, 24> audioHistory{};
    std::size_t audioPosition = 0;
    std::size_t audioCount = 0;
    bool filteredTone = false;
    bool candidateTone = false;
    int candidateCount = 0;
    bool hasTone = false;
    float clock = 0.0f;
    bool sampledBit = false;
    bool previousSymbol = false;
    bool hasPreviousSymbol = false;
    uint8_t flagShift = 0;
    bool inFrame = false;
    std::vector<bool> frameBits;
};

struct StationState {
    std::string callsign;
    std::string destination;
    std::string path;
    std::string information;
    std::string type;
    double rfFrequency = 0.0;
    bool hasPosition = false;
    double latitude = 0.0;
    double longitude = 0.0;
    char symbolTable = 0;
    char symbolCode = 0;
    uint64_t packetCount = 0;
    uint64_t firstSeenOrder = 0;
    int64_t lastSeenMillis = 0;
};

class StationTracker {
public:
    explicit StationTracker(std::size_t capacity = 512);

    void update(const Packet& packet, int64_t timestampMillis);
    void expire(int64_t timestampMillis, int retentionSeconds);
    std::vector<StationState> snapshot() const;
    void clear();
    std::size_t size() const;

private:
    void evictOldest();

    std::size_t capacity;
    uint64_t nextOrder = 0;
    std::map<std::string, StationState> stations;
};

}
