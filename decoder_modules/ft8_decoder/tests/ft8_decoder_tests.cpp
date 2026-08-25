// End-to-end characterization tests for the audio decoder used by
// FT8DecoderModule. The generators are only fixture builders here: assertions
// are deliberately limited to messages emitted by the decoder callback.

#include "ft8_etc/gen_ft8.h"
#include "ft8_etc/mshv_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mshv_decoder.h"
#include <utils/wav.h>

namespace {

// GenFt8 hardcodes its symbol length to 4*1920 samples, so it only produces
// correctly timed audio at 48 kHz; samp_rate only scales the carrier phase.
// mshv::decode resamples down to its own 12 kHz internally.
constexpr int GENERATOR_SAMPLE_RATE = 48000;
// 79 symbols * 7680 samples of signal, plus the ~4s of trailing silence
// genft8() unconditionally appends. Undersizing this corrupts the heap.
constexpr int GENERATOR_BUFFER_SIZE = GENERATOR_SAMPLE_RATE * 20;
constexpr int DECODER_SAMPLE_RATE = 12000;
constexpr int TONE_FREQUENCY = 1000;
constexpr std::size_t MESSAGE_FIELD = 4;

// The module normally does this from sdrppModuleInit(), which is not linked
// into the test binary. Without it the mshv lookup tables stay zeroed.
struct MshvFixture {
    MshvFixture() { mshv_init(); }
};

struct AudioFixture {
    int sampleRate;
    std::vector<dsp::stereo_t> samples;
};

AudioFixture loadPcm16Wav(const std::string& path) {
    wav::Reader reader(path);
    REQUIRE(reader.isValid());
    REQUIRE(reader.getBitDepth() == 16);
    const auto channelCount = reader.getChannelCount();
    REQUIRE((channelCount == 1 || channelCount == 2));

    AudioFixture fixture { static_cast<int>(reader.getSampleRate()), {} };
    std::array<int16_t, 8192> pcm;
    while (true) {
        const auto bytesRead = reader.readSamples2(pcm.data(), pcm.size() * sizeof(pcm[0]));
        REQUIRE(bytesRead % (channelCount * sizeof(pcm[0])) == 0);

        const auto sampleCount = bytesRead / sizeof(pcm[0]);
        fixture.samples.reserve(fixture.samples.size() + sampleCount / channelCount);
        for (std::size_t i = 0; i < sampleCount; i += channelCount) {
            const float left = pcm[i] / 32768.0f;
            const float right = channelCount == 1 ? left : pcm[i + 1] / 32768.0f;
            fixture.samples.push_back({ left, right });
        }

        if (bytesRead != pcm.size() * sizeof(pcm[0])) {
            break;
        }
    }
    REQUIRE_FALSE(fixture.samples.empty());
    return fixture;
}

std::vector<dsp::stereo_t> generateFT8(const std::string& message) {
    std::vector<short> mono(GENERATOR_BUFFER_SIZE, 0);
    GenFt8 generator(false);
    int count = generator.genft8(message, mono.data(), GENERATOR_SAMPLE_RATE, TONE_FREQUENCY);
    REQUIRE(count > 0);
    REQUIRE(count <= GENERATOR_BUFFER_SIZE);

    std::vector<dsp::stereo_t> stereo;
    stereo.reserve(count);
    for (int i = 0; i < count; i++) {
        float sample = mono[i] / 32767.0f;
        stereo.push_back({ sample, sample });
    }
    return stereo;
}

std::vector<mshv::DecodeResult> decode(const char *mode, int sampleRate,
                                       const std::vector<dsp::stereo_t>& samples) {
    std::vector<mshv::DecodeResult> messages;
    mshv::decode(1, mode, sampleRate, std::span<const dsp::stereo_t>(samples),
                 [&](mshv::DecodeResult fields) {
                     messages.push_back(std::move(fields));
                 });
    return messages;
}

// The decoder appends "|<hashed callsigns>" to the message field; the module
// strips it the same way before displaying the message.
std::string messageOf(const mshv::DecodeResult& fields) {
    const std::string& raw = fields[MESSAGE_FIELD];
    return raw.substr(0, raw.find('|'));
}

bool containsMessage(const std::vector<mshv::DecodeResult>& decoded, const std::string& message) {
    for (const auto& fields : decoded) {
        if (fields.size() > MESSAGE_FIELD && messageOf(fields) == message) {
            return true;
        }
    }
    return false;
}

std::string describeDecoded(const std::vector<mshv::DecodeResult>& decoded) {
    std::ostringstream out;
    out << "decoded callback count: " << decoded.size();
    for (const auto& fields : decoded) {
        out << "\n---";
        for (std::size_t i = 0; i < fields.size(); i++) {
            out << "\nfield[" << i << "]: " << fields[i];
        }
    }
    return out.str();
}

}

TEST_CASE_METHOD(MshvFixture, "FT8DecoderModule decodes self-generated FT8 CQ message",
                 "[module][ft8_decoder][ft8]") {
    const std::string message = "CQ F5RXL IN94";

    auto decoded = decode("ft8", GENERATOR_SAMPLE_RATE, generateFT8(message));
    INFO(describeDecoded(decoded));
    REQUIRE(containsMessage(decoded, message));
}

TEST_CASE_METHOD(MshvFixture, "FT8DecoderModule decodes an FT4 recording",
                 "[module][ft8_decoder][ft4]") {
    const std::string message = "CQ RU N9OY EN43";
    auto recording = loadPcm16Wav(FT4_TEST_WAV_PATH);

    auto decoded = decode("ft4", recording.sampleRate, recording.samples);
    INFO(describeDecoded(decoded));
    REQUIRE(containsMessage(decoded, message));
}

TEST_CASE_METHOD(MshvFixture, "FT8DecoderModule decodes an FT8 recording",
                 "[module][ft8_decoder][ft8]") {
    const std::string message1 = "CQ F5RXL IN94";
    const std::string message2 = "CQ EA2BFM IN83";
    auto recording = loadPcm16Wav(FT8_TEST_WAV_PATH);

    auto decoded = decode("ft8", recording.sampleRate, recording.samples);
    INFO(describeDecoded(decoded));
    REQUIRE(containsMessage(decoded, message1));
    REQUIRE(containsMessage(decoded, message2));
}

TEST_CASE_METHOD(MshvFixture, "FT8DecoderModule does not decode silence",
                 "[module][ft8_decoder]") {
    std::vector<dsp::stereo_t> silence(15 * DECODER_SAMPLE_RATE, { 0.0f, 0.0f });

    auto decoded = decode("ft8", DECODER_SAMPLE_RATE, silence);
    INFO(describeDecoded(decoded));
    REQUIRE(decoded.empty());
}

TEST_CASE_METHOD(MshvFixture, "FT8DecoderModule rejects an unknown mode",
                 "[module][ft8_decoder]") {
    std::vector<dsp::stereo_t> silence(DECODER_SAMPLE_RATE, { 0.0f, 0.0f });

    REQUIRE_THROWS_AS(decode("wspr", DECODER_SAMPLE_RATE, silence), std::invalid_argument);
}
