#include <dsp/protocol/lora/lora.h>
#include <protocol/meshtastic/meshtastic.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace meshtastic = protocol::meshtastic;

uint16_t readU16(std::istream& stream) {
    uint8_t bytes[2]{};
    stream.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8u);
}

uint32_t readU32(std::istream& stream) {
    uint8_t bytes[4]{};
    stream.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8u) |
           (static_cast<uint32_t>(bytes[2]) << 16u) | (static_cast<uint32_t>(bytes[3]) << 24u);
}

struct WavInfo {
    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bits = 0;
    std::streamoff dataOffset = 0;
    uint32_t dataSize = 0;
};

bool readWavInfo(std::ifstream& file, WavInfo& info) {
    char id[4]{};
    file.read(id, 4);
    if (std::string(id, 4) != "RIFF") { return false; }
    readU32(file);
    file.read(id, 4);
    if (std::string(id, 4) != "WAVE") { return false; }
    bool haveFormat = false;
    while (file && !info.dataOffset) {
        file.read(id, 4);
        const uint32_t size = readU32(file);
        if (std::string(id, 4) == "fmt ") {
            info.format = readU16(file);
            info.channels = readU16(file);
            info.sampleRate = readU32(file);
            readU32(file);
            readU16(file);
            info.bits = readU16(file);
            if (size > 16) { file.seekg(size - 16, std::ios::cur); }
            haveFormat = true;
        }
        else if (std::string(id, 4) == "data") {
            info.dataOffset = file.tellg();
            info.dataSize = size;
        }
        else { file.seekg(size, std::ios::cur); }
        if (size & 1u) { file.seekg(1, std::ios::cur); }
    }
    return haveFormat && info.dataOffset != 0;
}

struct Result {
    std::string label;
    std::size_t frames = 0;
    std::size_t meshtasticFrames = 0;
};

void frameHandler(const dsp::protocol::lora::Frame& frame, void* context) {
    auto* result = static_cast<Result*>(context);
    result->frames++;
    std::cout << result->label << " frame " << result->frames << ": bytes=" << frame.payload.size()
              << " header=" << frame.headerValid << " crc=" << frame.payloadCrcValid
              << " cr=" << frame.codingRate << " corrected=" << frame.correctedCodewords
              << " invalid=" << frame.invalidCodewords << " snr=" << frame.snrDb
              << " cfo=" << frame.frequencyErrorHz << " payload=";
    std::cout << std::hex << std::setfill('0');
    for (uint8_t byte : frame.payload) { std::cout << std::setw(2) << static_cast<unsigned>(byte); }
    std::cout << std::dec << '\n';
    meshtastic::Decoder decoder;
    decoder.setChannels({ meshtastic::makeChannel("EdgeFastLow", { 1 }) });
    const auto decoded = decoder.decode(frame.payload.data(), frame.payload.size(),
                                        { frame.snrDb, frame.rssiDb, frame.frequencyErrorHz }, frame.payloadCrcValid);
    std::cout << "  mesh=" << meshtastic::decodeStatusText(decoded.status) << " lora_crc=" << frame.payloadCrcValid;
    if (decoded.status == meshtastic::DecodeStatus::Ok) {
        result->meshtasticFrames++;
        const auto& packet = decoded.decoded.packet;
        const auto& data = decoded.decoded.data;
        std::cout << " to=" << std::hex << packet.header.to << " from=" << packet.header.from
                  << " id=" << packet.header.id << " channel=" << static_cast<unsigned>(packet.header.channelHash)
                  << std::dec << " port=" << data.portNum;
        if (!data.text.empty()) { std::cout << " text=" << data.text; }
        if (data.position.hasLatitude && data.position.hasLongitude) {
            std::cout << " position=" << data.position.latitude << ',' << data.position.longitude;
        }
    }
    std::cout << '\n';
}

void printStats(const Result& result, const dsp::protocol::lora::LoRaDecoder& decoder) {
    const auto stats = decoder.getStats();
    const auto sync = decoder.getSyncDiagnostics();
    std::cout << result.label << ": frames=" << result.frames << " preambles=" << stats.preamblesDetected
              << " sync_failures=" << stats.syncFailures << " headers=" << stats.headersDecoded
              << " header_failures=" << stats.headerFailures << " payloads=" << stats.payloadsDecoded
              << " crc_failures=" << stats.payloadCrcFailures << " last_cfo=" << sync.cfoHz
              << " confidence=" << sync.preambleConfidence << " observed_sync=0x" << std::hex
              << sync.observedSyncWord << std::dec << '\n';
    std::cout << result.label << " bins: preamble=" << sync.preambleBin << " down=" << sync.downchirpBin
              << " sync1=" << sync.firstSyncBin << " sync2=" << sync.secondSyncBin
              << " timing=" << sync.timingOffset << '\n';
}

}

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "Meshtastic_EFL_int16.wav";
    std::ifstream file(path, std::ios::binary);
    WavInfo wav;
    if (!file || !readWavInfo(file, wav)) {
        std::cerr << "Cannot parse WAV: " << path << '\n';
        return 1;
    }
    std::cout << "WAV format=" << wav.format << " channels=" << wav.channels << " rate=" << wav.sampleRate
              << " bits=" << wav.bits << " data=" << wav.dataSize << " duration="
              << static_cast<double>(wav.dataSize) / (wav.sampleRate * wav.channels * (wav.bits / 8.0)) << " s\n";
    if (wav.format != 1 || wav.channels != 2 || wav.bits != 16) {
        std::cerr << "Probe currently requires stereo signed 16-bit PCM IQ\n";
        return 1;
    }

    dsp::protocol::lora::Config config;
    config.bandwidth = 62500;
    config.spreadingFactor = 8;
    config.codingRate = 4;
    config.implicitHeader = false;
    config.payloadCrc = true;
    config.lowDataRateOptimize = false;
    config.syncWord = argc > 2 ? static_cast<uint16_t>(std::stoul(argv[2], nullptr, 0)) : 0x2B;
    config.minimumPreambleSymbols = 8;
    config.oversampling = argc > 3 ? std::stoi(argv[3]) : 4;

    Result normal{ "normal" };
    Result conjugated{ "conjugated" };
    dsp::protocol::lora::LoRaDecoder normalDecoder;
    dsp::protocol::lora::LoRaDecoder conjugatedDecoder;
    normalDecoder.initOffline(wav.sampleRate, config, frameHandler, &normal);
    conjugatedDecoder.initOffline(wav.sampleRate, config, frameHandler, &conjugated);

    file.clear();
    file.seekg(wav.dataOffset);
    constexpr std::size_t FRAMES_PER_BLOCK = 65536;
    std::vector<int16_t> packed(FRAMES_PER_BLOCK * 2);
    std::vector<dsp::complex_t> samples(FRAMES_PER_BLOCK);
    std::vector<dsp::complex_t> conjugate(FRAMES_PER_BLOCK);
    uint64_t remaining = wav.dataSize;
    while (remaining) {
        const std::size_t bytes = static_cast<std::size_t>((std::min<uint64_t>)(remaining, packed.size() * sizeof(int16_t)));
        file.read(reinterpret_cast<char*>(packed.data()), bytes);
        const std::size_t count = static_cast<std::size_t>(file.gcount()) / (2 * sizeof(int16_t));
        if (!count) { break; }
        for (std::size_t i = 0; i < count; i++) {
            samples[i] = { packed[2 * i] / 32768.0f, packed[2 * i + 1] / 32768.0f };
            conjugate[i] = { samples[i].re, -samples[i].im };
        }
        normalDecoder.process(samples.data(), count);
        conjugatedDecoder.process(conjugate.data(), count);
        remaining -= count * 2 * sizeof(int16_t);
    }

    printStats(normal, normalDecoder);
    printStats(conjugated, conjugatedDecoder);
    const bool requireMeshtastic = argc > 4 && std::string(argv[4]) == "require";
    return requireMeshtastic && normal.meshtasticFrames == 0 ? 2 : 0;
}
