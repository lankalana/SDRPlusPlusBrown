#include "mshv_decoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>

#include <dsp/multirate/rational_resampler.h>
#include <utils/strings.h>

#include "ft8_etc/decoderms.h"

namespace mshv {
namespace {

constexpr int DECODER_SAMPLE_RATE = 12000;
constexpr int DMS_FT8 = 11;
constexpr int DMS_FT4 = 13;

int decoderMode(std::string_view mode) {
    if (mode == "ft8") {
        return DMS_FT8;
    }
    if (mode == "ft4") {
        return DMS_FT4;
    }
    throw std::invalid_argument("Invalid decoder mode: " + std::string(mode));
}

std::span<const dsp::stereo_t> resample(
        int sampleRate, std::span<const dsp::stereo_t> samples,
        std::vector<dsp::stereo_t>& storage) {
    if (sampleRate == DECODER_SAMPLE_RATE) {
        return samples;
    }

    const auto outputCapacity = 3 * (samples.size() * DECODER_SAMPLE_RATE) / sampleRate;
    storage.resize(outputCapacity);

    dsp::multirate::RationalResampler<dsp::stereo_t> resampler;
    resampler.init(nullptr, sampleRate, DECODER_SAMPLE_RATE);
    const auto outputSize = resampler.process(samples.size(), samples.data(), storage.data());
    storage.resize(outputSize);
    return storage;
}

}

void decode(int threads, std::string_view mode, int sampleRate,
            std::span<const dsp::stereo_t> samples, const DecodeCallback& callback) {
    if (sampleRate <= 0) {
        throw std::invalid_argument("Decoder sample rate must be positive");
    }

    int maxAmplitude = 0;
    for (const auto& sample : samples) {
        maxAmplitude = static_cast<int>((std::max)({
            static_cast<float>(maxAmplitude), std::abs(sample.l), std::abs(sample.r)
        }));
    }
    const auto scale = 16383.52f / (std::max)(maxAmplitude, 1);

    std::vector<dsp::stereo_t> resampled;
    samples = resample(sampleRate, samples, resampled);

    std::vector<short> monoSamples(samples.size());
    std::ranges::transform(samples, monoSamples.begin(), [scale](const dsp::stereo_t& sample) {
        return static_cast<short>(sample.l * scale);
    });

    DecoderMs decoder;
    decoder.setMode(decoderMode(mode));

    QStringList words;
    words << "CALL";
    words << "CALL";
    decoder.SetWords(words, 0, 0);

    QStringList calls;
    calls << "CALL";
    calls << "";
    calls << "";
    calls << "";
    calls << "";
    decoder.SetCalsHash(calls);

    decoder.SetResultsCallback([&callback](const char* line) {
        std::vector<std::string> fields;
        splitStringV(line, "\t\n", fields);
        if (fields.size() <= 18) {
            return;
        }
        callback({
            fields[1],
            fields[6],
            "",
            fields[18],
            fields[12]
        });
    });
    decoder.SetDecoderDeep(3);
    decoder.SetThrLevel(threads);
    decoder.SetDecode(monoSamples.data(), static_cast<int>(monoSamples.size()),
                      "120000", 0, 4, false, true, false);

    while (decoder.IsWorking()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

}
