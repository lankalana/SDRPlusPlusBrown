#include "adsb_decoder.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::array<uint8_t, adsb::FRAME_BYTES> fromHex(const std::string& text) {
    assert(text.size() == adsb::FRAME_BYTES * 2);
    std::array<uint8_t, adsb::FRAME_BYTES> frame{};
    for (std::size_t i = 0; i < frame.size(); i++) {
        frame[i] = (uint8_t)std::stoul(text.substr(i * 2, 2), nullptr, 16);
    }
    return frame;
}

void flipBit(std::array<uint8_t, adsb::FRAME_BYTES>& frame, int bit) {
    frame[bit >> 3] ^= (uint8_t)(1U << (7 - (bit & 7)));
}

void updateParity(std::array<uint8_t, adsb::FRAME_BYTES>& frame) {
    frame[11] = 0;
    frame[12] = 0;
    frame[13] = 0;
    uint32_t parity = adsb::crc24(frame);
    frame[11] = (uint8_t)(parity >> 16);
    frame[12] = (uint8_t)(parity >> 8);
    frame[13] = (uint8_t)parity;
    assert(adsb::crc24(frame) == 0);
}

std::vector<float> modulate(const std::array<uint8_t, adsb::FRAME_BYTES>& frame, std::size_t prefix = 0) {
    constexpr std::size_t preambleSamples = 32;
    constexpr std::size_t bitSamples = 4;
    std::vector<float> samples(prefix + preambleSamples + adsb::FRAME_BITS * bitSamples + 25, 0.01f);
    std::size_t start = prefix;
    const int pulses[] = { 0, 1, 4, 5, 14, 15, 18, 19 };
    for (int pulse : pulses) { samples[start + (std::size_t)pulse] = 1.0f; }
    start += preambleSamples;
    for (std::size_t bit = 0; bit < adsb::FRAME_BITS; bit++) {
        bool value = (frame[bit >> 3] & (1U << (7 - (bit & 7)))) != 0;
        std::size_t pos = start + bit * bitSamples;
        samples[pos + (value ? 0 : 2)] = 1.0f;
        samples[pos + (value ? 1 : 3)] = 1.0f;
    }
    return samples;
}

void testProtocolVectors() {
    auto callsignFrame = fromHex("8D4840D6202CC371C32CE0576098");
    assert(adsb::crc24(callsignFrame) == 0);
    adsb::DecodedFrame callsign;
    assert(adsb::decodeFrame(callsignFrame, 0, -18.0f, callsign));
    assert(callsign.icao == 0x4840D6);
    assert(callsign.callsign == "KLM1023");
    assert(callsign.typeCode == 4);
    assert(callsign.hasCategory);
    assert(adsb::frameToHex(callsignFrame) == "8D4840D6202CC371C32CE0576098");

    auto categoryFrame = callsignFrame;
    categoryFrame[4] = (uint8_t)((categoryFrame[4] & 0xF8) | 5);
    updateParity(categoryFrame);
    adsb::DecodedFrame category;
    assert(adsb::decodeFrame(categoryFrame, 0, -18.0f, category));
    assert(category.category == "Heavy aircraft");

    auto velocityFrame = callsignFrame;
    velocityFrame[4] = (uint8_t)(19 << 3);
    updateParity(velocityFrame);
    adsb::DecodedFrame unavailable;
    assert(adsb::decodeFrame(velocityFrame, 0, -18.0f, unavailable));
    assert(!unavailable.hasCallsign);
    assert(!unavailable.hasCategory);
    assert(!unavailable.hasAltitude);

    auto altitudeFrame = fromHex("8D40621D58C382D690C8AC2863A7");
    adsb::DecodedFrame altitude;
    assert(adsb::decodeFrame(altitudeFrame, 0, -22.0f, altitude));
    assert(altitude.hasAltitude);
    assert(altitude.altitudeFeet == 38000);

    int altitudeFeet = 0;
    assert(adsb::decodeAltitude(0x661, altitudeFeet));
    assert(altitudeFeet == 35000);
    assert(!adsb::decodeAltitude(0, altitudeFeet));
}

void testCorrectionLevels() {
    const auto original = fromHex("8D4840D6202CC371C32CE0576098");
    adsb::DecodedFrame decoded;

    auto oneBit = original;
    flipBit(oneBit, 47);
    assert(!adsb::decodeFrame(oneBit, 0, -20.0f, decoded));
    assert(adsb::decodeFrame(oneBit, 1, -20.0f, decoded));
    assert(decoded.correctedBits == 1);
    assert(decoded.raw == original);

    auto twoBits = original;
    flipBit(twoBits, 17);
    flipBit(twoBits, 83);
    assert(!adsb::decodeFrame(twoBits, 1, -20.0f, decoded));
    assert(adsb::decodeFrame(twoBits, 2, -20.0f, decoded));
    assert(decoded.correctedBits == 2);
    assert(decoded.raw == original);
}

void testStreamingDemodulator() {
    const auto frame = fromHex("8D4840D6202CC371C32CE0576098");
    auto samples = modulate(frame, 17);
    int decodedCount = 0;
    adsb::DecodedFrame result;
    adsb::StreamDecoder decoder([&](const adsb::DecodedFrame& decoded) {
        decodedCount++;
        result = decoded;
    });
    decoder.process(samples.data(), 211);
    assert(decodedCount == 0);
    decoder.process(samples.data() + 211, samples.size() - 211);
    assert(decodedCount == 1);
    assert(result.callsign == "KLM1023");
    assert(std::fabs(result.rssiDbfs) < 0.1f);
    assert(decoder.stats().valid == 1);

    decoder.reset();
    auto shifted = samples;
    for (std::size_t i = shifted.size() - 1; i > 0; i--) {
        shifted[i] = samples[i] * 0.5f + samples[i - 1] * 0.5f + (float)(i % 5) * 0.0002f;
    }
    decoder.process(shifted.data(), shifted.size());
    assert(decodedCount == 2);

    decoder.reset();
    auto noisy = samples;
    noisy[17 + 2] = 1.0f;
    decoder.process(noisy.data(), noisy.size());
    assert(decodedCount == 2);
}

void testTracker() {
    adsb::AircraftTracker tracker(2);
    adsb::DecodedFrame first;
    first.icao = 0xABCDEF;
    first.hasCallsign = true;
    first.callsign = "TEST123";
    first.raw = fromHex("8D4840D6202CC371C32CE0576098");
    tracker.update(first, 1000);

    adsb::DecodedFrame altitude = first;
    altitude.hasCallsign = false;
    altitude.callsign.clear();
    altitude.hasAltitude = true;
    altitude.altitudeFeet = 12000;
    tracker.update(altitude, 2000);
    auto states = tracker.snapshot();
    assert(states.size() == 1);
    assert(states[0].callsign == "TEST123");
    assert(states[0].altitudeFeet == 12000);
    assert(states[0].messageCount == 2);

    adsb::DecodedFrame second = first;
    second.icao = 0x111111;
    tracker.update(second, 3000);
    adsb::DecodedFrame third = first;
    third.icao = 0x222222;
    tracker.update(third, 4000);
    assert(tracker.size() == 2);
    states = tracker.snapshot();
    assert(states[0].icao == 0x111111);
    assert(states[1].icao == 0x222222);

    tracker.expire(10001, 6);
    assert(tracker.size() == 0);
}

}

int main() {
    testProtocolVectors();
    testCorrectionLevels();
    testStreamingDemodulator();
    testTracker();
    std::cout << "ADS-B decoder tests passed\n";
    return 0;
}
