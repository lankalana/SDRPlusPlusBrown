#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <dsp/protocol/lora/lora.h>

using namespace dsp;
using namespace dsp::protocol::lora;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

std::vector<uint16_t> encodePhySymbols(const Config& config, const std::vector<uint8_t>& payload, bool corruptCrc = false) {
    std::vector<uint8_t> nibbles;
    if (!config.implicitHeader) {
        uint8_t header[5] = {};
        encodeHeader(static_cast<uint8_t>(payload.size()), static_cast<uint8_t>(config.codingRate), config.payloadCrc, header);
        nibbles.insert(nibbles.end(), header, header + 5);
    }

    std::vector<uint8_t> whitened = payload;
    dewhiten(whitened);
    for (uint8_t byte : whitened) {
        nibbles.push_back(byte & 0x0Fu);
        nibbles.push_back(byte >> 4u);
    }
    if (config.payloadCrc) {
        uint16_t crc = payloadCrc(payload);
        if (corruptCrc) { crc ^= 1u; }
        const uint8_t crcBytes[2] = { static_cast<uint8_t>(crc), static_cast<uint8_t>(crc >> 8u) };
        for (uint8_t byte : crcBytes) {
            nibbles.push_back(byte & 0x0Fu);
            nibbles.push_back(byte >> 4u);
        }
    }

    std::vector<uint16_t> symbols;
    std::size_t nibbleOffset = 0;
    auto appendBlock = [&](int codingRate, bool reducedRate) {
        const int effectiveSf = config.spreadingFactor - (reducedRate ? 2 : 0);
        std::vector<uint8_t> codewords(effectiveSf);
        for (int i = 0; i < effectiveSf; i++) {
            const uint8_t nibble = nibbleOffset < nibbles.size() ? nibbles[nibbleOffset++] : 0;
            codewords[i] = hammingEncode(nibble, codingRate);
        }
        std::vector<uint16_t> interleaved(4 + codingRate);
        require(interleave(codewords.data(), codewords.size(), config.spreadingFactor, codingRate,
                           reducedRate, interleaved.data(), interleaved.size()), "interleave failed");
        const int bins = 1 << config.spreadingFactor;
        for (uint16_t value : interleaved) {
            if (reducedRate) {
                const uint16_t parity = static_cast<uint16_t>(bitCount(static_cast<uint8_t>(value)) & 1);
                value = static_cast<uint16_t>((value << 2u) | (parity << 1u));
            }
            symbols.push_back(static_cast<uint16_t>((grayDecode(value) + 1) % bins));
        }
    };

    appendBlock(4, true);
    while (nibbleOffset < nibbles.size()) { appendBlock(config.codingRate, config.lowDataRateOptimize); }
    return symbols;
}

void testGray() {
    for (int sf = 5; sf <= 12; sf++) {
        for (uint16_t value = 0; value < (1u << sf); value++) {
            require(grayDecode(grayEncode(value)) == value, "Gray round trip failed");
        }
    }
}

void testHardwareSyncEncoding() {
    for (int sf = 7; sf <= 12; sf++) {
        const int bins = 1 << sf;
        for (uint16_t word : { 0x12u, 0x2Bu, 0xFFu, 0x1424u }) {
            const int first = syncNibbleFromBin(firstSyncBin(word, bins), bins, false);
            const int second = syncNibbleFromBin(secondSyncBin(word, bins), bins, true);
            require(matchSyncWord(first, second, word), "hardware sync encoding mismatch");
            if (word == 0x1424u) { require(first == 1 && second == 2, "SX126x private sync mapping mismatch"); }
        }
    }
}

void testWhiteningAndCrc() {
    const uint8_t expected[] = { 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE1, 0xC2, 0x85, 0x0B };
    for (std::size_t i = 0; i < sizeof(expected); i++) {
        require(whiteningByte(i) == expected[i], "whitening sequence mismatch");
    }
    std::vector<uint8_t> data = { 0x00, 0x11, 0x22, 0x80, 0xFF };
    const std::vector<uint8_t> original = data;
    dewhiten(data);
    dewhiten(data);
    require(data == original, "whitening is not self-inverse");

    const uint8_t check[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    require(crc16Ccitt(check, sizeof(check)) == 0x31C3u, "CRC-16/CCITT vector mismatch");
    std::vector<uint8_t> withCrc = original;
    const uint16_t crc = payloadCrc(original);
    withCrc.push_back(static_cast<uint8_t>(crc));
    withCrc.push_back(static_cast<uint8_t>(crc >> 8u));
    require(checkPayloadCrc(withCrc), "LoRa payload CRC rejected");
    withCrc[1] ^= 1u;
    require(!checkPayloadCrc(withCrc), "corrupt LoRa payload CRC accepted");
}

void testFecAndInterleaver() {
    require(hammingEncode(0x1, 1) == 0x11 && hammingEncode(0x1, 3) == 0x45 &&
            hammingEncode(0x1, 4) == 0x8B && hammingEncode(0x2, 4) == 0x4E,
            "LoRa Hamming codeword vector mismatch");
    for (int codingRate = 1; codingRate <= 4; codingRate++) {
        for (uint8_t nibble = 0; nibble < 16; nibble++) {
            const uint8_t encoded = hammingEncode(nibble, codingRate);
            const FecResult decoded = hammingDecode(encoded, codingRate);
            require(decoded.valid && decoded.nibble == nibble, "FEC round trip failed");
            if (codingRate >= 3) {
                for (int bit = 0; bit < 4 + codingRate; bit++) {
                    const FecResult corrected = hammingDecode(static_cast<uint8_t>(encoded ^ (1u << bit)), codingRate);
                    require(corrected.valid && corrected.corrected && corrected.nibble == nibble, "FEC correction failed");
                }
            }
        }
    }

    for (int sf = 7; sf <= 12; sf++) {
        for (int codingRate = 1; codingRate <= 4; codingRate++) {
            for (bool reduced : { false, true }) {
                const int effectiveSf = sf - (reduced ? 2 : 0);
                std::vector<uint8_t> input(effectiveSf);
                for (int i = 0; i < effectiveSf; i++) { input[i] = hammingEncode(static_cast<uint8_t>(i), codingRate); }
                std::vector<uint16_t> symbols(4 + codingRate);
                std::vector<uint8_t> output(effectiveSf);
                require(interleave(input.data(), input.size(), sf, codingRate, reduced, symbols.data(), symbols.size()), "interleave rejected valid block");
                require(deinterleave(symbols.data(), symbols.size(), sf, codingRate, reduced, output.data(), output.size()), "deinterleave rejected valid block");
                require(input == output, "interleaver round trip failed");
            }
        }
    }
}

void testHeader() {
    for (int length : { 1, 16, 127, 255 }) {
        for (int codingRate = 1; codingRate <= 4; codingRate++) {
            for (bool crc : { false, true }) {
                uint8_t nibbles[5] = {};
                encodeHeader(static_cast<uint8_t>(length), static_cast<uint8_t>(codingRate), crc, nibbles);
                const Header header = decodeHeader(nibbles, 5);
                require(header.valid && header.payloadLength == length && header.codingRate == codingRate &&
                        header.payloadCrcPresent == crc, "header round trip failed");
                nibbles[4] ^= 1u;
                require(!decodeHeader(nibbles, 5).valid, "bad header checksum accepted");
            }
        }
    }
}

void testPhyPipeline() {
    const std::vector<uint8_t> payload = { 0x00, 0x01, 0x10, 0x7F, 0x80, 0xFE, 0xFF, 'L', 'o', 'R', 'a' };
    for (int sf = 7; sf <= 12; sf++) {
        for (int codingRate = 1; codingRate <= 4; codingRate++) {
            for (bool implicit : { false, true }) {
                for (bool crc : { false, true }) {
                    Config config;
                    config.spreadingFactor = sf;
                    config.codingRate = codingRate;
                    config.implicitHeader = implicit;
                    config.implicitPayloadLength = implicit ? static_cast<int>(payload.size()) : 0;
                    config.payloadCrc = crc;
                    config.lowDataRateOptimize = defaultLowDataRateOptimize(config.bandwidth, sf);
                    const std::vector<uint16_t> symbols = encodePhySymbols(config, payload);
                    Frame frame;
                    require(decodePhySymbols(symbols.data(), symbols.size(), config, frame) == PhyDecodeStatus::Complete,
                            "PHY pipeline did not complete");
                    require(frame.headerValid && frame.payload == payload && frame.payloadCrcValid,
                            "PHY pipeline payload mismatch");
                    if (crc) {
                        const std::vector<uint16_t> corrupt = encodePhySymbols(config, payload, true);
                        require(decodePhySymbols(corrupt.data(), corrupt.size(), config, frame) == PhyDecodeStatus::Complete &&
                                !frame.payloadCrcValid, "bad payload CRC not reported");
                    }
                }
            }
        }
    }
}

void testChirpDemodulation() {
    for (int sf : { 7, 8, 10 }) {
        ChirpGenerator chirps;
        chirps.configure(sf, 4);
        ChirpDemodulator demodulator;
        demodulator.configure(&chirps, 500000.0);
        std::vector<complex_t> samples(chirps.samplesPerSymbol());
        const int step = sf <= 8 ? 1 : 17;
        for (int symbol = 0; symbol < chirps.bins(); symbol += step) {
            chirps.generateSymbol(symbol, true, samples.data(), samples.size());
            const SymbolEstimate estimate = demodulator.demodulateUpchirp(samples.data(), samples.size());
            require(estimate.symbol == symbol && estimate.peakRatio() > 1000.0f, "ideal chirp demodulation failed");
        }
    }
}

struct Capture {
    std::vector<Frame> frames;
};

void captureFrame(const Frame& frame, void* context) {
    static_cast<Capture*>(context)->frames.push_back(frame);
}

void appendChirp(std::vector<complex_t>& iq, const ChirpGenerator& chirps, int symbol, bool up) {
    const std::size_t offset = iq.size();
    iq.resize(offset + chirps.samplesPerSymbol());
    chirps.generateSymbol(symbol, up, iq.data() + offset, chirps.samplesPerSymbol());
}

void testSyntheticIq() {
    Config config;
    config.spreadingFactor = 7;
    config.codingRate = 1;
    config.minimumPreambleSymbols = 6;
    config.syncWord = 0x1424;
    const std::vector<uint8_t> payload = { 'm', 'e', 's', 'h' };
    const std::vector<uint16_t> phySymbols = encodePhySymbols(config, payload);
    ChirpGenerator chirps;
    chirps.configure(config.spreadingFactor, config.oversampling);
    const int symbolSamples = chirps.samplesPerSymbol();
    std::vector<complex_t> iq(5 * symbolSamples + 37);
    uint32_t noiseState = 0x12345678u;
    for (complex_t& sample : iq) {
        noiseState = noiseState * 1664525u + 1013904223u;
        sample.re = (static_cast<int>((noiseState >> 16u) & 0xFFFFu) - 32768) / 1638400.0f;
        noiseState = noiseState * 1664525u + 1013904223u;
        sample.im = (static_cast<int>((noiseState >> 16u) & 0xFFFFu) - 32768) / 1638400.0f;
    }
    for (int i = 0; i < 8; i++) { appendChirp(iq, chirps, 0, true); }
    appendChirp(iq, chirps, firstSyncBin(config.syncWord, chirps.bins()), true);
    appendChirp(iq, chirps, secondSyncBin(config.syncWord, chirps.bins()), true);
    appendChirp(iq, chirps, 0, false);
    appendChirp(iq, chirps, 0, false);
    std::vector<complex_t> quarter(symbolSamples);
    chirps.generateSymbol(0, false, quarter.data(), quarter.size());
    iq.insert(iq.end(), quarter.begin(), quarter.begin() + symbolSamples / 4);
    for (uint16_t symbol : phySymbols) { appendChirp(iq, chirps, symbol, true); }
    const std::vector<complex_t> firstPacket = iq;
    iq.insert(iq.end(), firstPacket.begin() + 5 * symbolSamples + 37, firstPacket.end());
    iq.resize(iq.size() + symbolSamples, { 0.0f, 0.0f });

    const double sampleRate = config.oversampling * config.bandwidth;
    const double cfoHz = 3500.0;
    for (std::size_t i = 0; i < iq.size(); i++) {
        const double phase = 2.0 * 3.14159265358979323846 * cfoHz * i / sampleRate;
        const complex_t rotator = { static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase)) };
        const complex_t value = iq[i];
        iq[i] = { value.re * rotator.re - value.im * rotator.im,
                  value.im * rotator.re + value.re * rotator.im };
    }

    Capture capture;
    LoRaDecoder decoder;
    decoder.initOffline(sampleRate, config, captureFrame, &capture);
    for (std::size_t offset = 0; offset < iq.size();) {
        const std::size_t count = (std::min<std::size_t>)(777, iq.size() - offset);
        decoder.process(iq.data() + offset, count);
        offset += count;
    }
    const Stats stats = decoder.getStats();
    const SyncDiagnostics diagnostics = decoder.getSyncDiagnostics();
    require(capture.frames.size() == 2,
            "synthetic IQ packet was not detected (preambles=" + std::to_string(stats.preamblesDetected) +
            ", syncFailures=" + std::to_string(stats.syncFailures) +
            ", headers=" + std::to_string(stats.headersDecoded) +
            ", headerFailures=" + std::to_string(stats.headerFailures) +
            ", cfo=" + std::to_string(diagnostics.cfoHz) +
            ", timing=" + std::to_string(diagnostics.timingOffset) + ")");
    for (const Frame& frame : capture.frames) {
        require(frame.payload == payload && frame.payloadCrcValid, "synthetic IQ payload mismatch");
        require(std::fabs(frame.frequencyErrorHz - cfoHz) < config.bandwidth / (1 << config.spreadingFactor),
                "synthetic IQ CFO estimate mismatch");
    }
}

}

int main() {
    try {
        testGray();
        testHardwareSyncEncoding();
        testWhiteningAndCrc();
        testFecAndInterleaver();
        testHeader();
        testPhyPipeline();
        testChirpDemodulation();
        testSyntheticIq();
        std::cout << "LoRa PHY tests passed" << std::endl;
        std::_Exit(0);
    }
    catch (const std::exception& error) {
        std::cerr << "LoRa PHY test failure: " << error.what() << std::endl;
        std::_Exit(1);
    }
}
