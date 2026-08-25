#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <dsp/types.h>

namespace mshv {

using DecodeResult = std::vector<std::string>;
using DecodeCallback = std::function<void(DecodeResult)>;

void decode(int threads, std::string_view mode, int sampleRate,
            std::span<const dsp::stereo_t> samples, const DecodeCallback& callback);

}
