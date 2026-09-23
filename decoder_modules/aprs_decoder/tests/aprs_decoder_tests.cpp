#include "aprs_decoder.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr double PI = 3.14159265358979323846;

void appendAddress(std::vector<uint8_t>& frame, const std::string& callsign, int ssid, bool last, bool repeated = false) {
    std::string padded = callsign;
    padded.resize(6, ' ');
    for (int i = 0; i < 6; i++) { frame.push_back((uint8_t)(padded[(std::size_t)i] << 1)); }
    frame.push_back((uint8_t)(0x60 | (ssid << 1) | (last ? 1 : 0) | (repeated ? 0x80 : 0)));
}

std::vector<uint8_t> makeFrame(const std::string& information) {
    std::vector<uint8_t> frame;
    appendAddress(frame, "APRS", 0, false);
    appendAddress(frame, "OH2ABC", 9, false);
    appendAddress(frame, "WIDE1", 1, false, true);
    appendAddress(frame, "WIDE2", 2, true);
    frame.push_back(0x03);
    frame.push_back(0xF0);
    frame.insert(frame.end(), information.begin(), information.end());
    uint16_t fcs = aprs::crc16(frame.data(), frame.size());
    frame.push_back((uint8_t)fcs);
    frame.push_back((uint8_t)(fcs >> 8));
    return frame;
}

std::string base91(uint32_t value) {
    std::string encoded(4, '!');
    for (int i = 3; i >= 0; i--) {
        encoded[(std::size_t)i] = (char)('!' + value % 91U);
        value /= 91U;
    }
    return encoded;
}

void appendByteBits(std::vector<bool>& bits, uint8_t value) {
    for (int bit = 0; bit < 8; bit++) { bits.push_back((value & (1U << bit)) != 0); }
}

std::vector<bool> makeHDLCBits(const std::vector<uint8_t>& frame) {
    std::vector<bool> bits;
    for (int i = 0; i < 16; i++) { appendByteBits(bits, 0x7E); }
    int ones = 0;
    for (uint8_t byte : frame) {
        for (int bit = 0; bit < 8; bit++) {
            bool value = (byte & (1U << bit)) != 0;
            bits.push_back(value);
            if (value) {
                ones++;
                if (ones == 5) {
                    bits.push_back(false);
                    ones = 0;
                }
            }
            else { ones = 0; }
        }
    }
    for (int i = 0; i < 3; i++) { appendByteBits(bits, 0x7E); }
    return bits;
}

std::vector<aprs::IQSample> modulate(const std::vector<uint8_t>& frame) {
    std::vector<bool> bits = makeHDLCBits(frame);
    std::vector<aprs::IQSample> samples;
    samples.reserve(bits.size() * 40 + 200);
    double audioPhase = 0.0;
    double fmPhase = 0.0;
    bool mark = true;
    for (int i = 0; i < 100; i++) { samples.push_back({ 1.0f, 0.0f }); }
    for (bool bit : bits) {
        if (!bit) { mark = !mark; }
        double frequency = mark ? 1200.0 : 2200.0;
        for (int sample = 0; sample < 40; sample++) {
            fmPhase += 0.7 * std::sin(audioPhase);
            audioPhase += 2.0 * PI * frequency / aprs::SAMPLE_RATE;
            samples.push_back({ (float)std::cos(fmPhase), (float)std::sin(fmPhase) });
        }
    }
    return samples;
}

void testProtocolParsing() {
    assert(aprs::selectAPRSFrequency(144800000.0, 2000000.0, 16000.0) == 144800000.0);
    assert(aprs::selectAPRSFrequency(145200000.0, 100000.0, 16000.0) == 145175000.0);
    assert(aprs::selectAPRSFrequency(146000000.0, 100000.0, 16000.0) == 0.0);

    const uint8_t crcCheck[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    assert(aprs::crc16(crcCheck, sizeof(crcCheck)) == 0x906E);

    std::vector<uint8_t> frame = makeFrame("!6010.50N/02456.25E-Test beacon");
    aprs::Packet packet;
    assert(aprs::decodeAX25(frame, packet));
    assert(packet.source == "OH2ABC-9");
    assert(packet.destination == "APRS");
    assert(packet.path.size() == 2);
    assert(packet.path[0] == "WIDE1-1*");
    assert(packet.path[1] == "WIDE2-2");
    assert(packet.type == "Position");
    assert(packet.hasPosition);
    assert(std::fabs(packet.latitude - 60.175) < 0.00001);
    assert(std::fabs(packet.longitude - 24.9375) < 0.00001);
    assert(packet.symbolTable == '/');
    assert(packet.symbolCode == '-');
    assert(aprs::formatTNC2(packet) == "OH2ABC-9>APRS,WIDE1-1*,WIDE2-2:!6010.50N/02456.25E-Test beacon");
    assert(aprs::formatLogLine(packet, 1704067200123) ==
           "2024-01-01T00:00:00.123Z\tOH2ABC-9>APRS,WIDE1-1*,WIDE2-2:!6010.50N/02456.25E-Test beacon"
           "\tLAT=60.17500\tLON=24.93750");
    packet.rfFrequency = 144800000.0;
    assert(aprs::formatLogLine(packet, 1704067200123) ==
           "2024-01-01T00:00:00.123Z\tOH2ABC-9>APRS,WIDE1-1*,WIDE2-2:!6010.50N/02456.25E-Test beacon"
           "\tFREQ=144.800000\tLAT=60.17500\tLON=24.93750");

    frame[20] ^= 1;
    assert(!aprs::decodeAX25(frame, packet));

    double compressedLatitude = 61.5;
    double compressedLongitude = 23.75;
    std::string compressed = "!/" + base91((uint32_t)std::llround(380926.0 * (90.0 - compressedLatitude))) +
                             base91((uint32_t)std::llround(190463.0 * (180.0 + compressedLongitude))) + "-!!!";
    assert(aprs::decodeAX25(makeFrame(compressed), packet));
    assert(packet.hasPosition);
    assert(std::fabs(packet.latitude - compressedLatitude) < 0.00001);
    assert(std::fabs(packet.longitude - compressedLongitude) < 0.00001);
    assert(packet.symbolTable == '/');
    assert(packet.symbolCode == '-');
}

void testStreamingDecoder() {
    std::vector<uint8_t> frame = makeFrame(">SDR++ APRS decoder test");
    std::vector<aprs::IQSample> samples = modulate(frame);
    int packetCount = 0;
    aprs::Packet received;
    aprs::Decoder decoder([&](const aprs::Packet& packet) {
        packetCount++;
        received = packet;
    });
    decoder.process(samples.data(), 337);
    assert(packetCount == 0);
    decoder.process(samples.data() + 337, samples.size() - 337);
    assert(packetCount == 1);
    assert(received.source == "OH2ABC-9");
    assert(received.type == "Status");
    assert(decoder.stats().frames == 1);

    std::vector<uint8_t> badFCS = frame;
    badFCS.back() ^= 1;
    samples = modulate(badFCS);
    decoder.reset();
    decoder.process(samples.data(), samples.size());
    assert(packetCount == 1);
    assert(decoder.stats().crcErrors == 1);

    std::vector<uint8_t> nonAPRS(40, 0x42);
    samples = modulate(nonAPRS);
    decoder.reset();
    decoder.process(samples.data(), samples.size());
    assert(packetCount == 1);
    assert(decoder.stats().crcErrors == 1);
}

void testTracker() {
    aprs::Packet first;
    assert(aprs::decodeAX25(makeFrame(">first"), first));
    first.rfFrequency = 144800000.0;
    aprs::StationTracker tracker(2);
    tracker.update(first, 1000);
    aprs::Packet update;
    assert(aprs::decodeAX25(makeFrame("!6010.50N/02456.25E-Home"), update));
    update.rfFrequency = 144800000.0;
    tracker.update(update, 2000);
    std::vector<aprs::StationState> states = tracker.snapshot();
    assert(states.size() == 1);
    assert(states[0].packetCount == 2);
    assert(states[0].hasPosition);
    assert(states[0].information == "!6010.50N/02456.25E-Home");
    assert(states[0].rfFrequency == 144800000.0);
    tracker.expire(9001, 7);
    assert(tracker.size() == 0);
}

}

int main() {
    testProtocolParsing();
    testStreamingDecoder();
    testTracker();
    std::cout << "APRS decoder tests passed\n";
    return 0;
}
