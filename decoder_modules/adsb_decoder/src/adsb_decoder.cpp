#include "adsb_decoder.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace adsb {

namespace {

constexpr uint32_t MODES_GENERATOR = 0xFFF409;
constexpr std::size_t PREAMBLE_SAMPLES = 32;
constexpr std::size_t BIT_SAMPLES = 4;
constexpr std::size_t MESSAGE_SAMPLES = FRAME_BITS * BIT_SAMPLES;
constexpr std::size_t COMPLETE_FRAME_SAMPLES = PREAMBLE_SAMPLES + MESSAGE_SAMPLES;

int getBit(const std::array<uint8_t, FRAME_BYTES>& frame, int bit) {
    return (frame[bit >> 3] >> (7 - (bit & 7))) & 1;
}

uint32_t getBits(const std::array<uint8_t, FRAME_BYTES>& frame, int firstBit, int count) {
    uint32_t value = 0;
    for (int i = 0; i < count; i++) {
        value = (value << 1) | getBit(frame, firstBit + i);
    }
    return value;
}

void flipBit(std::array<uint8_t, FRAME_BYTES>& frame, int bit) {
    frame[bit >> 3] ^= (uint8_t)(1U << (7 - (bit & 7)));
}

int decodeID13Field(int id13) {
    int gillham = 0;
    if (id13 & 0x1000) { gillham |= 0x0010; }
    if (id13 & 0x0800) { gillham |= 0x1000; }
    if (id13 & 0x0400) { gillham |= 0x0020; }
    if (id13 & 0x0200) { gillham |= 0x2000; }
    if (id13 & 0x0100) { gillham |= 0x0040; }
    if (id13 & 0x0080) { gillham |= 0x4000; }
    if (id13 & 0x0020) { gillham |= 0x0100; }
    if (id13 & 0x0010) { gillham |= 0x0001; }
    if (id13 & 0x0008) { gillham |= 0x0200; }
    if (id13 & 0x0004) { gillham |= 0x0002; }
    if (id13 & 0x0002) { gillham |= 0x0400; }
    if (id13 & 0x0001) { gillham |= 0x0004; }
    return gillham;
}

int modeAToModeC(unsigned int modeA) {
    unsigned int fiveHundreds = 0;
    unsigned int oneHundreds = 0;

    if ((modeA & 0xFFFF8889U) || ((modeA & 0x000000F0U) == 0)) { return -9999; }
    if (modeA & 0x0010U) { oneHundreds ^= 0x007; }
    if (modeA & 0x0020U) { oneHundreds ^= 0x003; }
    if (modeA & 0x0040U) { oneHundreds ^= 0x001; }
    if ((oneHundreds & 5U) == 5U) { oneHundreds ^= 2; }
    if (oneHundreds > 5U) { return -9999; }

    if (modeA & 0x0002U) { fiveHundreds ^= 0x0FF; }
    if (modeA & 0x0004U) { fiveHundreds ^= 0x07F; }
    if (modeA & 0x1000U) { fiveHundreds ^= 0x03F; }
    if (modeA & 0x2000U) { fiveHundreds ^= 0x01F; }
    if (modeA & 0x4000U) { fiveHundreds ^= 0x00F; }
    if (modeA & 0x0100U) { fiveHundreds ^= 0x007; }
    if (modeA & 0x0200U) { fiveHundreds ^= 0x003; }
    if (modeA & 0x0400U) { fiveHundreds ^= 0x001; }
    if (fiveHundreds & 1U) { oneHundreds = 6U - oneHundreds; }
    return (int)(fiveHundreds * 5U + oneHundreds) - 13;
}

char decodeCallsignCharacter(int value) {
    if (value >= 1 && value <= 26) { return (char)('A' + value - 1); }
    if (value >= 48 && value <= 57) { return (char)('0' + value - 48); }
    if (value == 32) { return ' '; }
    return '?';
}

std::string categoryName(int typeCode, int category) {
    if (category == 0) { return "No category information"; }
    if (typeCode == 4) {
        static const char* names[] = {
            "", "Light aircraft", "Small aircraft", "Large aircraft", "Large high-vortex aircraft",
            "Heavy aircraft", "High-performance aircraft", "Rotorcraft"
        };
        return names[category];
    }
    if (typeCode == 3) {
        static const char* names[] = {
            "", "Glider or sailplane", "Lighter-than-air", "Parachutist or skydiver",
            "Ultralight, hang-glider, or paraglider", "Reserved", "Uncrewed aerial vehicle", "Space vehicle"
        };
        return names[category];
    }
    if (typeCode == 2) {
        static const char* names[] = {
            "", "Surface emergency vehicle", "Surface service vehicle", "Point obstacle",
            "Cluster obstacle", "Line obstacle", "Reserved", "Reserved"
        };
        return names[category];
    }
    return "Reserved category";
}

bool tryCorrection(std::array<uint8_t, FRAME_BYTES>& frame, int maxCorrections, int& correctedBits) {
    uint32_t syndrome = crc24(frame);
    correctedBits = 0;
    if (syndrome == 0) { return true; }
    if (maxCorrections <= 0) { return false; }

    static const std::array<uint32_t, FRAME_BITS> bitSyndromes = []() {
        std::array<uint32_t, FRAME_BITS> result{};
        for (std::size_t bit = 0; bit < FRAME_BITS; bit++) {
            std::array<uint8_t, FRAME_BYTES> error{};
            flipBit(error, (int)bit);
            result[bit] = crc24(error);
        }
        return result;
    }();

    for (std::size_t bit = 0; bit < FRAME_BITS; bit++) {
        if (bitSyndromes[bit] != syndrome) { continue; }
        auto candidate = frame;
        flipBit(candidate, (int)bit);
        if (crc24(candidate) == 0 && (candidate[0] >> 3) == 17) {
            frame = candidate;
            correctedBits = 1;
            return true;
        }
    }

    if (maxCorrections < 2) { return false; }
    for (std::size_t first = 0; first < FRAME_BITS; first++) {
        for (std::size_t second = first + 1; second < FRAME_BITS; second++) {
            if ((bitSyndromes[first] ^ bitSyndromes[second]) != syndrome) { continue; }
            auto candidate = frame;
            flipBit(candidate, (int)first);
            flipBit(candidate, (int)second);
            if (crc24(candidate) == 0 && (candidate[0] >> 3) == 17) {
                frame = candidate;
                correctedBits = 2;
                return true;
            }
        }
    }
    return false;
}

}

uint32_t crc24(const std::array<uint8_t, FRAME_BYTES>& frame) {
    uint32_t remainder = 0;
    for (std::size_t bit = 0; bit < FRAME_BITS; bit++) {
        bool top = (remainder & 0x800000U) != 0;
        remainder = ((remainder << 1) & 0xFFFFFFU) | (uint32_t)getBit(frame, (int)bit);
        if (top) { remainder ^= MODES_GENERATOR; }
    }
    return remainder;
}

std::string frameToHex(const std::array<uint8_t, FRAME_BYTES>& frame) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (uint8_t byte : frame) { stream << std::setw(2) << (int)byte; }
    return stream.str();
}

bool decodeAltitude(int ac12, int& altitudeFeet) {
    if (ac12 <= 0 || ac12 > 0x0FFF) { return false; }
    if (ac12 & 0x10) {
        int n = ((ac12 & 0x0FE0) >> 1) | (ac12 & 0x000F);
        altitudeFeet = n * 25 - 1000;
        return true;
    }

    int ac13 = ((ac12 & 0x0FC0) << 1) | (ac12 & 0x003F);
    int hundreds = modeAToModeC((unsigned int)decodeID13Field(ac13));
    if (hundreds < -12) { return false; }
    altitudeFeet = hundreds * 100;
    return true;
}

bool decodeFrame(std::array<uint8_t, FRAME_BYTES> frame, int maxCorrections, float rssiDbfs, DecodedFrame& decoded) {
    maxCorrections = (std::max)(0, (std::min)(2, maxCorrections));
    int correctedBits = 0;
    if (!tryCorrection(frame, maxCorrections, correctedBits)) { return false; }
    if ((frame[0] >> 3) != 17) { return false; }

    decoded = {};
    decoded.raw = frame;
    decoded.correctedBits = correctedBits;
    decoded.rssiDbfs = rssiDbfs;
    decoded.capability = frame[0] & 7;
    decoded.icao = ((uint32_t)frame[1] << 16) | ((uint32_t)frame[2] << 8) | frame[3];
    decoded.typeCode = (int)getBits(frame, 32, 5);

    if (decoded.typeCode >= 1 && decoded.typeCode <= 4) {
        int category = (int)getBits(frame, 37, 3);
        decoded.hasCategory = true;
        decoded.category = categoryName(decoded.typeCode, category);
        decoded.callsign.reserve(8);
        for (int i = 0; i < 8; i++) {
            decoded.callsign.push_back(decodeCallsignCharacter((int)getBits(frame, 40 + i * 6, 6)));
        }
        while (!decoded.callsign.empty() && decoded.callsign.back() == ' ') { decoded.callsign.pop_back(); }
        decoded.hasCallsign = !decoded.callsign.empty();
    }

    if (decoded.typeCode >= 9 && decoded.typeCode <= 18) {
        int altitude = 0;
        int ac12 = (int)getBits(frame, 40, 12);
        if (decodeAltitude(ac12, altitude)) {
            decoded.hasAltitude = true;
            decoded.altitudeFeet = altitude;
        }
    }
    return true;
}

StreamDecoder::StreamDecoder(FrameHandler handler) : frameHandler(std::move(handler)) {
    samples.reserve(COMPLETE_FRAME_SAMPLES * 3);
}

void StreamDecoder::setFrameHandler(FrameHandler handler) {
    frameHandler = std::move(handler);
}

void StreamDecoder::setMaxCorrections(int corrections) {
    maxCorrections = (std::max)(0, (std::min)(2, corrections));
}

void StreamDecoder::process(const float* power, std::size_t count) {
    if (!power || count == 0) { return; }
    samples.insert(samples.end(), power, power + count);

    std::size_t scan = 0;
    while (scan + COMPLETE_FRAME_SAMPLES <= samples.size()) {
        float signalPower = 0.0f;
        if (!isPreamble(scan, signalPower)) {
            scan++;
            continue;
        }

        std::array<uint8_t, FRAME_BYTES> frame{};
        if (!demodulate(scan + PREAMBLE_SAMPLES, frame)) {
            scan++;
            continue;
        }

        DecodedFrame decoded;
        float rssi = 10.0f * std::log10((std::max)(signalPower, 1.0e-12f));
        if (!decodeFrame(frame, maxCorrections, rssi, decoded)) {
            scan++;
            continue;
        }

        demodStats.valid++;
        if (decoded.correctedBits > 0) { demodStats.corrected++; }
        if (frameHandler) { frameHandler(decoded); }
        scan += COMPLETE_FRAME_SAMPLES;
    }

    if (scan > 0) { samples.erase(samples.begin(), samples.begin() + (std::ptrdiff_t)scan); }
    if (samples.size() > COMPLETE_FRAME_SAMPLES * 4) {
        samples.erase(samples.begin(), samples.end() - (std::ptrdiff_t)(COMPLETE_FRAME_SAMPLES - 1));
    }
}

void StreamDecoder::reset() {
    samples.clear();
}

const DemodStats& StreamDecoder::stats() const {
    return demodStats;
}

bool StreamDecoder::isPreamble(std::size_t offset, float& signalPower) const {
    static const int highIndices[] = { 0, 1, 4, 5, 14, 15, 18, 19 };
    static const int pulseStarts[] = { 0, 4, 14, 18 };
    static const int lowIndices[] = { 2, 3, 7, 8, 9, 10, 11, 12, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
    float high = 0.0f;
    float low = 0.0f;
    for (int index : highIndices) { high += samples[offset + index]; }
    for (int index : lowIndices) { low += samples[offset + index]; }
    high /= (float)(sizeof(highIndices) / sizeof(highIndices[0]));
    low /= (float)(sizeof(lowIndices) / sizeof(lowIndices[0]));
    signalPower = high;
    if (high < 1.0e-10f || high < low * 3.0f) { return false; }

    for (int start : pulseStarts) {
        float pulse = (samples[offset + start] + samples[offset + start + 1]) * 0.5f;
        if (pulse < low * 2.5f) { return false; }
    }
    float threshold = low + (high - low) * 0.70f;
    return samples[offset + 2] < threshold && samples[offset + 3] < threshold &&
           samples[offset + 6] < threshold && samples[offset + 12] < threshold &&
           samples[offset + 13] < threshold && samples[offset + 16] < threshold &&
           samples[offset + 17] < threshold && samples[offset + 20] < threshold;
}

bool StreamDecoder::demodulate(std::size_t offset, std::array<uint8_t, FRAME_BYTES>& frame) const {
    for (std::size_t bit = 0; bit < FRAME_BITS; bit++) {
        std::size_t pos = offset + bit * BIT_SAMPLES;
        float early = samples[pos] + samples[pos + 1];
        float late = samples[pos + 2] + samples[pos + 3];
        float total = early + late;
        if (total <= 1.0e-12f || std::fabs(early - late) / total < 0.035f) { return false; }
        if (early > late) { frame[bit >> 3] |= (uint8_t)(1U << (7 - (bit & 7))); }
    }
    return true;
}

AircraftTracker::AircraftTracker(std::size_t maxAircraft) : capacity((std::max<std::size_t>)(1, maxAircraft)) {}

void AircraftTracker::update(const DecodedFrame& frame, int64_t timestampMillis) {
    auto it = aircraft.find(frame.icao);
    if (it == aircraft.end()) {
        if (aircraft.size() >= capacity) { evictOldest(); }
        AircraftState state;
        state.icao = frame.icao;
        state.firstSeenOrder = nextOrder++;
        it = aircraft.emplace(frame.icao, std::move(state)).first;
    }

    AircraftState& state = it->second;
    if (frame.hasCallsign) { state.callsign = frame.callsign; }
    if (frame.hasCategory) { state.category = frame.category; }
    if (frame.hasAltitude) {
        state.hasAltitude = true;
        state.altitudeFeet = frame.altitudeFeet;
    }
    state.rssiDbfs = frame.rssiDbfs;
    state.lastCorrectedBits = frame.correctedBits;
    if (frame.correctedBits > 0) { state.correctedMessages++; }
    state.messageCount++;
    state.lastSeenMillis = timestampMillis;
    state.lastRawFrame = frameToHex(frame.raw);
}

void AircraftTracker::expire(int64_t timestampMillis, int retentionSeconds) {
    int64_t maxAge = (int64_t)(std::max)(1, retentionSeconds) * 1000;
    for (auto it = aircraft.begin(); it != aircraft.end();) {
        if (timestampMillis - it->second.lastSeenMillis > maxAge) { it = aircraft.erase(it); }
        else { ++it; }
    }
}

std::vector<AircraftState> AircraftTracker::snapshot() const {
    std::vector<AircraftState> result;
    result.reserve(aircraft.size());
    for (const auto& entry : aircraft) { result.push_back(entry.second); }
    std::sort(result.begin(), result.end(), [](const AircraftState& a, const AircraftState& b) {
        return a.firstSeenOrder < b.firstSeenOrder;
    });
    return result;
}

void AircraftTracker::clear() {
    aircraft.clear();
}

std::size_t AircraftTracker::size() const {
    return aircraft.size();
}

void AircraftTracker::evictOldest() {
    if (aircraft.empty()) { return; }
    auto oldest = aircraft.begin();
    for (auto it = aircraft.begin(); it != aircraft.end(); ++it) {
        if (it->second.lastSeenMillis < oldest->second.lastSeenMillis) { oldest = it; }
    }
    aircraft.erase(oldest);
}

}
