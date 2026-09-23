#include "aprs_decoder.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace aprs {

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr float SAMPLES_PER_BIT = (float)SAMPLE_RATE / (float)BAUD_RATE;
constexpr std::size_t MAX_FRAME_BITS = 4096 * 8;
constexpr std::array<double, 10> APRS_FREQUENCIES = {
    144390000.0,
    144575000.0,
    144660000.0,
    144800000.0,
    144930000.0,
    145175000.0,
    145570000.0,
    145825000.0,
    432500000.0,
    433800000.0
};

std::string decodeAddress(const uint8_t* address, bool path) {
    std::string callsign;
    callsign.reserve(9);
    for (int i = 0; i < 6; i++) {
        char character = (char)(address[i] >> 1);
        if (character != ' ') { callsign.push_back(character); }
    }
    int ssid = (address[6] >> 1) & 0x0F;
    if (ssid != 0) { callsign += '-' + std::to_string(ssid); }
    if (path && (address[6] & 0x80)) { callsign.push_back('*'); }
    return callsign;
}

bool parseCoordinate(const std::string& text, std::size_t offset, Packet& packet) {
    if (text.size() < offset + 19) { return false; }
    const char* value = text.c_str() + offset;
    if (value[4] != '.' || (value[7] != 'N' && value[7] != 'S') ||
        value[14] != '.' || (value[17] != 'E' && value[17] != 'W')) { return false; }
    const int digitIndices[] = { 0, 1, 2, 3, 5, 6, 9, 10, 11, 12, 13, 15, 16 };
    for (int index : digitIndices) {
        if (value[index] < '0' || value[index] > '9') { return false; }
    }

    double latitude = (value[0] - '0') * 10.0 + value[1] - '0';
    latitude += ((value[2] - '0') * 10.0 + value[3] - '0' +
                 ((value[5] - '0') * 10.0 + value[6] - '0') / 100.0) / 60.0;
    double longitude = (value[9] - '0') * 100.0 + (value[10] - '0') * 10.0 + value[11] - '0';
    longitude += ((value[12] - '0') * 10.0 + value[13] - '0' +
                  ((value[15] - '0') * 10.0 + value[16] - '0') / 100.0) / 60.0;
    if (value[7] == 'S') { latitude = -latitude; }
    if (value[17] == 'W') { longitude = -longitude; }
    if (std::fabs(latitude) > 90.0 || std::fabs(longitude) > 180.0) { return false; }

    packet.hasPosition = true;
    packet.latitude = latitude;
    packet.longitude = longitude;
    packet.symbolTable = value[8];
    packet.symbolCode = value[18];
    return true;
}

bool parseCompressedCoordinate(const std::string& text, std::size_t offset, Packet& packet) {
    if (text.size() < offset + 13) { return false; }
    const char* value = text.c_str() + offset;
    uint32_t latitudeValue = 0;
    uint32_t longitudeValue = 0;
    for (int i = 0; i < 4; i++) {
        if (value[i + 1] < '!' || value[i + 1] > '{' || value[i + 5] < '!' || value[i + 5] > '{') { return false; }
        latitudeValue = latitudeValue * 91U + (uint32_t)(value[i + 1] - '!');
        longitudeValue = longitudeValue * 91U + (uint32_t)(value[i + 5] - '!');
    }

    double latitude = 90.0 - (double)latitudeValue / 380926.0;
    double longitude = -180.0 + (double)longitudeValue / 190463.0;
    if (std::fabs(latitude) > 90.0 || std::fabs(longitude) > 180.0) { return false; }
    packet.hasPosition = true;
    packet.latitude = latitude;
    packet.longitude = longitude;
    packet.symbolTable = value[0];
    packet.symbolCode = value[9];
    return true;
}

bool parsePosition(const std::string& text, std::size_t offset, Packet& packet) {
    return parseCoordinate(text, offset, packet) || parseCompressedCoordinate(text, offset, packet);
}

bool looksLikeAPRSFrame(const std::vector<uint8_t>& frame) {
    if (frame.size() < 18) { return false; }
    std::size_t position = 0;
    int addresses = 0;
    while (position + 7 <= frame.size() - 2 && addresses < 10) {
        for (int i = 0; i < 6; i++) {
            if (frame[position + (std::size_t)i] & 1) { return false; }
            char character = (char)(frame[position + (std::size_t)i] >> 1);
            if (character != ' ' && (character < '0' || character > '9') &&
                (character < 'A' || character > 'Z')) { return false; }
        }
        if ((frame[position + 6] & 0x60) != 0x60) { return false; }
        bool last = (frame[position + 6] & 1) != 0;
        position += 7;
        addresses++;
        if (last) { break; }
    }
    if (addresses < 2 || position + 2 > frame.size() - 2 || !(frame[position - 1] & 1)) { return false; }
    return frame[position] == 0x03 && frame[position + 1] == 0xF0;
}

std::string printableInformation(const std::string& information) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (unsigned char character : information) {
        if (character >= 32 && character <= 126) { stream << (char)character; }
        else { stream << "\\x" << std::setw(2) << (int)character; }
    }
    return stream.str();
}

}

uint16_t crc16(const uint8_t* data, std::size_t count) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < count; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1) { crc = (uint16_t)((crc >> 1) ^ 0x8408); }
            else { crc >>= 1; }
        }
    }
    return (uint16_t)(crc ^ 0xFFFF);
}

bool decodeAX25(const std::vector<uint8_t>& frame, Packet& packet) {
    if (frame.size() < 18) { return false; }
    uint16_t expected = crc16(frame.data(), frame.size() - 2);
    uint16_t received = (uint16_t)(frame[frame.size() - 2] | ((uint16_t)frame.back() << 8));
    if (expected != received) { return false; }

    packet = {};
    packet.raw = frame;
    std::size_t position = 0;
    std::vector<std::string> addresses;
    while (position + 7 <= frame.size() - 2 && addresses.size() < 10) {
        bool path = addresses.size() >= 2;
        addresses.push_back(decodeAddress(frame.data() + position, path));
        bool last = (frame[position + 6] & 1) != 0;
        position += 7;
        if (last) { break; }
    }
    if (addresses.size() < 2 || position + 2 > frame.size() - 2 || !(frame[position - 1] & 1)) { return false; }

    packet.destination = addresses[0];
    packet.source = addresses[1];
    packet.path.assign(addresses.begin() + 2, addresses.end());
    uint8_t control = frame[position++];
    if ((control & 1) == 0 || control == 0x03) {
        if (position >= frame.size() - 2) { return false; }
        position++;
    }
    packet.information.assign((const char*)frame.data() + position, frame.size() - 2 - position);

    if (packet.information.empty()) { packet.type = "Empty"; }
    else {
        switch (packet.information[0]) {
        case '!':
        case '=':
            packet.type = "Position";
            parsePosition(packet.information, 1, packet);
            break;
        case '/':
        case '@':
            packet.type = "Position with timestamp";
            parsePosition(packet.information, 8, packet);
            break;
        case ':': packet.type = "Message"; break;
        case '>': packet.type = "Status"; break;
        case ';': packet.type = "Object"; break;
        case ')': packet.type = "Item"; break;
        case '_': packet.type = "Weather"; break;
        case '?': packet.type = "Query"; break;
        default:
            packet.type = packet.information.rfind("T#", 0) == 0 ? "Telemetry" : "Data";
            break;
        }
    }
    return true;
}

std::string formatTNC2(const Packet& packet) {
    std::string result = packet.source + ">" + packet.destination;
    for (const std::string& repeater : packet.path) { result += ',' + repeater; }
    return result + ':' + printableInformation(packet.information);
}

std::string formatLogLine(const Packet& packet, int64_t timestampMillis) {
    std::time_t seconds = (std::time_t)(timestampMillis / 1000);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &utc);

    std::ostringstream stream;
    stream << timestamp << '.' << std::setfill('0') << std::setw(3) << (timestampMillis % 1000) << 'Z'
           << '\t' << formatTNC2(packet);
    if (packet.rfFrequency > 0.0) { stream << "\tFREQ=" << std::fixed << std::setprecision(6) << packet.rfFrequency / 1000000.0; }
    if (packet.hasPosition) {
        stream << "\tLAT=" << std::fixed << std::setprecision(5) << packet.latitude
               << "\tLON=" << packet.longitude;
    }
    return stream.str();
}

double selectAPRSFrequency(double centerFrequency, double visibleBandwidth, double channelBandwidth) {
    double halfVisible = visibleBandwidth / 2.0;
    double halfChannel = channelBandwidth / 2.0;
    double bestFrequency = 0.0;
    double bestDistance = 1.0e30;
    for (double frequency : APRS_FREQUENCIES) {
        if (frequency - halfChannel < centerFrequency - halfVisible ||
            frequency + halfChannel > centerFrequency + halfVisible) { continue; }
        double distance = std::fabs(frequency - centerFrequency);
        if (distance < bestDistance) {
            bestFrequency = frequency;
            bestDistance = distance;
        }
    }
    return bestFrequency;
}

Decoder::Decoder(PacketHandler handler) : packetHandler(std::move(handler)) {
    frameBits.reserve(MAX_FRAME_BITS);
}

void Decoder::setPacketHandler(PacketHandler handler) {
    packetHandler = std::move(handler);
}

void Decoder::process(const IQSample* samples, std::size_t count) {
    if (!samples) { return; }
    for (std::size_t i = 0; i < count; i++) {
        const IQSample& current = samples[i];
        if (hasPreviousIQ) {
            float cross = previousIQ.re * current.im - previousIQ.im * current.re;
            float dot = previousIQ.re * current.re + previousIQ.im * current.im;
            processAudio(std::atan2(cross, dot));
        }
        previousIQ = current;
        hasPreviousIQ = true;
    }
}

void Decoder::reset() {
    previousIQ = {};
    hasPreviousIQ = false;
    audioHistory.fill(0.0f);
    audioPosition = 0;
    audioCount = 0;
    filteredTone = false;
    candidateTone = false;
    candidateCount = 0;
    hasTone = false;
    clock = 0.0f;
    sampledBit = false;
    previousSymbol = false;
    hasPreviousSymbol = false;
    flagShift = 0;
    inFrame = false;
    frameBits.clear();
}

const DecoderStats& Decoder::stats() const {
    return decoderStats;
}

void Decoder::processAudio(float sample) {
    static const std::array<float, 24> markCos = []() {
        std::array<float, 24> values{};
        for (std::size_t i = 0; i < values.size(); i++) {
            values[i] = std::cos(2.0f * PI * 1200.0f * (float)i / (float)SAMPLE_RATE);
        }
        return values;
    }();
    static const std::array<float, 24> markSin = []() {
        std::array<float, 24> values{};
        for (std::size_t i = 0; i < values.size(); i++) {
            values[i] = std::sin(2.0f * PI * 1200.0f * (float)i / (float)SAMPLE_RATE);
        }
        return values;
    }();
    static const std::array<float, 24> spaceCos = []() {
        std::array<float, 24> values{};
        for (std::size_t i = 0; i < values.size(); i++) {
            values[i] = std::cos(2.0f * PI * 2200.0f * (float)i / (float)SAMPLE_RATE);
        }
        return values;
    }();
    static const std::array<float, 24> spaceSin = []() {
        std::array<float, 24> values{};
        for (std::size_t i = 0; i < values.size(); i++) {
            values[i] = std::sin(2.0f * PI * 2200.0f * (float)i / (float)SAMPLE_RATE);
        }
        return values;
    }();
    audioHistory[audioPosition] = sample;
    audioPosition = (audioPosition + 1) % audioHistory.size();
    audioCount = (std::min)(audioCount + 1, audioHistory.size());
    if (audioCount < audioHistory.size()) { return; }

    float markI = 0.0f;
    float markQ = 0.0f;
    float spaceI = 0.0f;
    float spaceQ = 0.0f;
    for (std::size_t age = 0; age < audioHistory.size(); age++) {
        std::size_t index = (audioPosition + audioHistory.size() - 1 - age) % audioHistory.size();
        float value = audioHistory[index];
        markI += value * markCos[age];
        markQ += value * markSin[age];
        spaceI += value * spaceCos[age];
        spaceQ += value * spaceSin[age];
    }
    bool tone = markI * markI + markQ * markQ >= spaceI * spaceI + spaceQ * spaceQ;
    if (!hasTone) {
        filteredTone = tone;
        candidateTone = tone;
        hasTone = true;
    }
    else if (tone == filteredTone) {
        candidateTone = tone;
        candidateCount = 0;
    }
    else {
        if (tone != candidateTone) {
            candidateTone = tone;
            candidateCount = 1;
        }
        else { candidateCount++; }
        if (candidateCount >= 3) {
            filteredTone = candidateTone;
            candidateCount = 0;
            clock = (float)audioHistory.size() * 0.5f;
            sampledBit = false;
        }
    }

    float previousClock = clock;
    clock += 1.0f;
    if (clock >= SAMPLES_PER_BIT) {
        clock -= SAMPLES_PER_BIT;
        sampledBit = false;
        previousClock = -1.0f;
    }
    if (!sampledBit && previousClock < SAMPLES_PER_BIT * 0.5f && clock >= SAMPLES_PER_BIT * 0.5f) {
        if (hasPreviousSymbol) { processBit(filteredTone == previousSymbol); }
        previousSymbol = filteredTone;
        hasPreviousSymbol = true;
        sampledBit = true;
    }
}

void Decoder::processBit(bool bit) {
    flagShift = (uint8_t)((flagShift >> 1) | (bit ? 0x80 : 0));
    if (inFrame) {
        frameBits.push_back(bit);
        if (frameBits.size() > MAX_FRAME_BITS) {
            inFrame = false;
            frameBits.clear();
        }
    }
    if (flagShift != 0x7E) { return; }

    if (inFrame && frameBits.size() >= 8) {
        frameBits.resize(frameBits.size() - 8);
        finishFrame();
    }
    frameBits.clear();
    inFrame = true;
}

void Decoder::finishFrame() {
    if (frameBits.empty()) { return; }
    std::vector<uint8_t> bytes;
    bytes.reserve(frameBits.size() / 8);
    uint8_t value = 0;
    int bitPosition = 0;
    int ones = 0;
    for (bool bit : frameBits) {
        if (bit) {
            value |= (uint8_t)(1U << bitPosition);
            ones++;
            if (ones > 6) { return; }
        }
        else {
            if (ones == 5) {
                ones = 0;
                continue;
            }
            if (ones >= 6) { return; }
            ones = 0;
        }
        bitPosition++;
        if (bitPosition == 8) {
            bytes.push_back(value);
            value = 0;
            bitPosition = 0;
        }
    }
    if (bitPosition != 0) { return; }

    if (!looksLikeAPRSFrame(bytes)) { return; }

    uint16_t expectedFCS = crc16(bytes.data(), bytes.size() - 2);
    uint16_t receivedFCS = (uint16_t)(bytes[bytes.size() - 2] | ((uint16_t)bytes.back() << 8));
    if (expectedFCS != receivedFCS) {
        decoderStats.crcErrors++;
        return;
    }

    Packet packet;
    if (!decodeAX25(bytes, packet)) { return; }
    decoderStats.frames++;
    if (packetHandler) { packetHandler(packet); }
}

StationTracker::StationTracker(std::size_t maxStations) : capacity((std::max<std::size_t>)(1, maxStations)) {}

void StationTracker::update(const Packet& packet, int64_t timestampMillis) {
    auto it = stations.find(packet.source);
    if (it == stations.end()) {
        if (stations.size() >= capacity) { evictOldest(); }
        StationState state;
        state.callsign = packet.source;
        state.firstSeenOrder = nextOrder++;
        it = stations.emplace(packet.source, std::move(state)).first;
    }

    StationState& state = it->second;
    state.destination = packet.destination;
    state.path.clear();
    for (std::size_t i = 0; i < packet.path.size(); i++) {
        if (i) { state.path.push_back(','); }
        state.path += packet.path[i];
    }
    state.information = printableInformation(packet.information);
    state.type = packet.type;
    if (packet.hasPosition) {
        state.hasPosition = true;
        state.latitude = packet.latitude;
        state.longitude = packet.longitude;
        state.symbolTable = packet.symbolTable;
        state.symbolCode = packet.symbolCode;
    }
    state.rfFrequency = packet.rfFrequency;
    state.packetCount++;
    state.lastSeenMillis = timestampMillis;
}

void StationTracker::expire(int64_t timestampMillis, int retentionSeconds) {
    int64_t maxAge = (int64_t)(std::max)(1, retentionSeconds) * 1000;
    for (auto it = stations.begin(); it != stations.end();) {
        if (timestampMillis - it->second.lastSeenMillis > maxAge) { it = stations.erase(it); }
        else { ++it; }
    }
}

std::vector<StationState> StationTracker::snapshot() const {
    std::vector<StationState> result;
    result.reserve(stations.size());
    for (const auto& entry : stations) { result.push_back(entry.second); }
    std::sort(result.begin(), result.end(), [](const StationState& first, const StationState& second) {
        return first.firstSeenOrder < second.firstSeenOrder;
    });
    return result;
}

void StationTracker::clear() {
    stations.clear();
}

std::size_t StationTracker::size() const {
    return stations.size();
}

void StationTracker::evictOldest() {
    if (stations.empty()) { return; }
    auto oldest = stations.begin();
    for (auto it = stations.begin(); it != stations.end(); ++it) {
        if (it->second.lastSeenMillis < oldest->second.lastSeenMillis) { oldest = it; }
    }
    stations.erase(oldest);
}

}
