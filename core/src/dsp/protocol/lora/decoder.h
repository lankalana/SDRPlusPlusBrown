#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <dsp/sink.h>
#include <dsp/types.h>
#include "types.h"

namespace dsp::protocol::lora {

enum class PhyDecodeStatus {
    NeedMore,
    InvalidHeader,
    Complete
};

PhyDecodeStatus decodePhySymbols(const uint16_t* symbols, std::size_t symbolCount, const Config& config,
                                 Frame& frame, std::size_t* requiredSymbols = nullptr);

class LoRaDecoder : public Sink<complex_t> {
    using base_type = Sink<complex_t>;
public:
    LoRaDecoder();
    LoRaDecoder(stream<complex_t>* input, double inputSampleRate, const Config& config,
                FrameHandler handler, void* context);
    ~LoRaDecoder() override;

    void init(stream<complex_t>* input, double inputSampleRate, const Config& config,
              FrameHandler handler, void* context);
    void initOffline(double inputSampleRate, const Config& config, FrameHandler handler, void* context);
    void setConfig(const Config& config);
    void setInput(stream<complex_t>* input) override;
    void setInputSampleRate(double sampleRate);
    void setFrameHandler(FrameHandler handler, void* context);
    void reset();

    void process(const complex_t* samples, std::size_t count);
    Stats getStats() const;
    SyncDiagnostics getSyncDiagnostics() const;
    int run() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

}
