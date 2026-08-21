#include "decoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <dsp/multirate/rational_resampler.h>
#include "chirp.h"
#include "chirp_demod.h"
#include "crc.h"
#include "detail/math.h"
#include "gray.h"
#include "hamming.h"
#include "header.h"
#include "interleaver.h"
#include "preamble_detector.h"
#include "synchronizer.h"
#include "whitening.h"

namespace dsp::protocol::lora {

namespace {

void appendDecodedBlock(const uint16_t* symbols, int spreadingFactor, int codingRate, bool reducedRate,
                        bool allowAdjacentRecovery, std::vector<uint8_t>& nibbles,
                        uint32_t& corrected, uint32_t& invalid) {
    const int codewordLength = 4 + codingRate;
    const int effectiveSf = spreadingFactor - (reducedRate ? 2 : 0);
    const int bins = 1 << spreadingFactor;
    auto decode = [&](const int* offsets, std::vector<uint8_t>& decoded, uint32_t& blockCorrected,
                      uint32_t& blockInvalid) {
        std::vector<uint16_t> mapped(codewordLength);
        for (int i = 0; i < codewordLength; i++) {
            int demodulated = detail::modulo(static_cast<int>(symbols[i]) + (offsets ? offsets[i] : 0) - 1, bins);
            if (reducedRate) { demodulated /= 4; }
            mapped[i] = grayEncode(static_cast<uint16_t>(demodulated));
        }
        std::vector<uint8_t> codewords(effectiveSf);
        if (!deinterleave(mapped.data(), mapped.size(), spreadingFactor, codingRate, reducedRate,
                          codewords.data(), codewords.size())) {
            blockInvalid = effectiveSf;
            return;
        }
        decoded.clear();
        for (uint8_t codeword : codewords) {
            const FecResult result = hammingDecode(codeword, codingRate);
            decoded.push_back(result.nibble);
            blockCorrected += result.corrected ? 1u : 0u;
            blockInvalid += result.valid ? 0u : 1u;
        }
    };

    std::vector<uint8_t> best;
    uint32_t bestCorrected = 0;
    uint32_t bestInvalid = 0;
    decode(nullptr, best, bestCorrected, bestInvalid);
    if (allowAdjacentRecovery && !reducedRate && codingRate >= 3 && bestInvalid) {
        int bestChanges = 0;
        int offsets[8] = {};
        int combinations = 1;
        for (int i = 0; i < codewordLength; i++) { combinations *= 3; }
        for (int combination = 1; combination < combinations; combination++) {
            int value = combination;
            int changes = 0;
            for (int i = 0; i < codewordLength; i++) {
                offsets[i] = value % 3 - 1;
                changes += offsets[i] != 0;
                value /= 3;
            }
            if (bestInvalid == 0 && changes > bestChanges) { continue; }
            std::vector<uint8_t> candidate;
            uint32_t candidateCorrected = 0;
            uint32_t candidateInvalid = 0;
            decode(offsets, candidate, candidateCorrected, candidateInvalid);
            if (candidateInvalid < bestInvalid ||
                (candidateInvalid == bestInvalid && (changes < bestChanges ||
                 (changes == bestChanges && candidateCorrected < bestCorrected)))) {
                best = std::move(candidate);
                bestCorrected = candidateCorrected;
                bestInvalid = candidateInvalid;
                bestChanges = changes;
            }
        }
    }
    nibbles.insert(nibbles.end(), best.begin(), best.end());
    corrected += bestCorrected;
    invalid += bestInvalid;
}

std::size_t requiredSymbolCount(std::size_t payloadNibbles, int spreadingFactor, int codingRate, bool ldro) {
    const int firstCapacity = spreadingFactor - 2;
    const int payloadCapacity = spreadingFactor - (ldro ? 2 : 0);
    const std::size_t remaining = payloadNibbles > static_cast<std::size_t>(firstCapacity)
        ? payloadNibbles - firstCapacity : 0;
    const std::size_t blocks = (remaining + payloadCapacity - 1) / payloadCapacity;
    return 8 + blocks * static_cast<std::size_t>(4 + codingRate);
}

float symbolSnr(const SymbolEstimate& estimate) {
    return 10.0f * std::log10((std::max)(estimate.peakRatio(), 1.0e-12f));
}

}

PhyDecodeStatus decodePhySymbols(const uint16_t* symbols, std::size_t symbolCount, const Config& config,
                                 Frame& frame, std::size_t* requiredSymbols) {
    frame = {};
    frame.spreadingFactor = config.spreadingFactor;
    frame.bandwidth = config.bandwidth;
    frame.implicitHeader = config.implicitHeader;
    frame.syncWord = config.syncWord;
    if (requiredSymbols) { *requiredSymbols = 8; }
    if (!symbols || symbolCount < 8) { return PhyDecodeStatus::NeedMore; }

    std::vector<uint8_t> nibbles;
    nibbles.reserve(520);
    appendDecodedBlock(symbols, config.spreadingFactor, 4, true, false, nibbles,
                       frame.correctedCodewords, frame.invalidCodewords);

    std::size_t payloadNibbleOffset = 0;
    int payloadLength = config.implicitPayloadLength;
    int codingRate = config.codingRate;
    bool crcPresent = config.payloadCrc;
    if (config.implicitHeader) {
        frame.headerValid = true;
    }
    else {
        const Header header = decodeHeader(nibbles.data(), nibbles.size());
        frame.headerValid = header.valid;
        if (!header.valid) { return PhyDecodeStatus::InvalidHeader; }
        payloadLength = header.payloadLength;
        codingRate = header.codingRate;
        crcPresent = header.payloadCrcPresent;
        payloadNibbleOffset = 5;
    }

    frame.codingRate = codingRate;
    frame.payloadCrcPresent = crcPresent;
    const std::size_t payloadNibbleCount = static_cast<std::size_t>(payloadLength) * 2 + (crcPresent ? 4u : 0u);
    const std::size_t totalNibbles = payloadNibbleOffset + payloadNibbleCount;
    const std::size_t neededSymbols = requiredSymbolCount(totalNibbles, config.spreadingFactor, codingRate,
                                                          config.lowDataRateOptimize);
    if (requiredSymbols) { *requiredSymbols = neededSymbols; }
    if (symbolCount < neededSymbols) { return PhyDecodeStatus::NeedMore; }

    std::size_t symbolOffset = 8;
    while (nibbles.size() < totalNibbles) {
        appendDecodedBlock(symbols + symbolOffset, config.spreadingFactor, codingRate,
                           config.lowDataRateOptimize, crcPresent, nibbles,
                           frame.correctedCodewords, frame.invalidCodewords);
        symbolOffset += 4 + codingRate;
    }

    std::vector<uint8_t> bytes((payloadNibbleCount + 1) / 2);
    for (std::size_t i = 0; i < payloadNibbleCount; i += 2) {
        const uint8_t low = nibbles[payloadNibbleOffset + i] & 0x0Fu;
        const uint8_t high = i + 1 < payloadNibbleCount ? nibbles[payloadNibbleOffset + i + 1] & 0x0Fu : 0;
        bytes[i / 2] = static_cast<uint8_t>(low | (high << 4u));
    }
    dewhiten(bytes.data(), static_cast<std::size_t>(payloadLength));
    frame.payload.assign(bytes.begin(), bytes.begin() + payloadLength);
    frame.payloadCrcValid = !crcPresent || checkPayloadCrc(bytes.data(), static_cast<std::size_t>(payloadLength) + 2);
    return PhyDecodeStatus::Complete;
}

class LoRaDecoder::Impl {
public:
    enum class State {
        Search,
        Preamble,
        Sync,
        Header,
        Payload
    };

    void configure(double sampleRate, const Config& newConfig, FrameHandler newHandler, void* newContext) {
        std::string error;
        if (!validateConfig(newConfig, &error)) { throw std::invalid_argument(error); }
        if (!(sampleRate > 0.0)) { throw std::invalid_argument("input sample rate must be positive"); }
        config = newConfig;
        derived = derive(config);
        inputSampleRate = sampleRate;
        handler = newHandler;
        context = newContext;
        chirps.configure(config.spreadingFactor, config.oversampling);
        demodulator.configure(&chirps, derived.internalSampleRate);
        detector.configure(derived.bins, config.minimumPreambleSymbols);
        resampler.reset(new multirate::RationalResampler<complex_t>());
        resampler->init(nullptr, inputSampleRate, derived.internalSampleRate);
        resetState();
    }

    void resetState() {
        if (resampler) { resampler->reset(); }
        samples.clear();
        samples.reserve(static_cast<std::size_t>(derived.samplesPerSymbol) * 6);
        resampled.clear();
        detector.reset();
        state = State::Search;
        cursor = 0;
        syncStart = 0;
        frameCursor = 0;
        preambleBin = 0.0f;
        cfoBins = 0.0f;
        frameSymbols.clear();
        expectedFrameSymbols = 8;
        snrSum = 0.0;
        snrCount = 0;
        powerSum = 0.0;
        powerCount = 0;
        {
            std::lock_guard<std::mutex> lock(metricsMutex);
            currentDiagnostics = {};
        }
    }

    void setRate(double sampleRate) {
        if (!(sampleRate > 0.0)) { throw std::invalid_argument("input sample rate must be positive"); }
        inputSampleRate = sampleRate;
        resampler->setRates(inputSampleRate, derived.internalSampleRate);
        resetState();
    }

    void feed(const complex_t* input, std::size_t count) {
        if (!input || !count) { return; }
        if (std::fabs(inputSampleRate - derived.internalSampleRate) < 0.5) {
            appendInternal(input, count);
            return;
        }
        const double ratio = derived.internalSampleRate / inputSampleRate;
        const std::size_t capacity = static_cast<std::size_t>(std::ceil((count + 256) * (std::max)(1.0, ratio))) + 4096;
        resampled.resize(capacity);
        const int produced = resampler->process(static_cast<int>(count), input, resampled.data());
        if (produced > 0) { appendInternal(resampled.data(), static_cast<std::size_t>(produced)); }
    }

    Stats statsSnapshot() const {
        std::lock_guard<std::mutex> lock(metricsMutex);
        return counters;
    }

    SyncDiagnostics diagnosticsSnapshot() const {
        std::lock_guard<std::mutex> lock(metricsMutex);
        return currentDiagnostics;
    }

    Config config;
    double inputSampleRate = 0.0;
    FrameHandler handler = nullptr;
    void* context = nullptr;

private:
    void appendInternal(const complex_t* input, std::size_t count) {
        samples.insert(samples.end(), input, input + count);
        bool progressed = true;
        while (progressed) {
            const std::size_t oldCursor = cursor;
            const State oldState = state;
            switch (state) {
                case State::Search: processSearch(); break;
                case State::Preamble: processPreamble(); break;
                case State::Sync: processSync(); break;
                case State::Header: processFrameSymbols(); break;
                case State::Payload: processFrameSymbols(); break;
            }
            progressed = oldCursor != cursor || oldState != state;
        }
    }

    bool haveSymbol(std::size_t position, std::size_t margin = 0) const {
        return position + derived.samplesPerSymbol + margin <= samples.size();
    }

    void processSearch() {
        const std::size_t symbolSamples = derived.samplesPerSymbol;
        while (haveSymbol(cursor)) {
            const SymbolEstimate estimate = demodulator.demodulateUpchirp(samples.data() + cursor, symbolSamples);
            const bool found = detector.observe(estimate);
            cursor += symbolSamples;
            if (found) {
                preambleBin = detector.bin();
                {
                    std::lock_guard<std::mutex> lock(metricsMutex);
                    currentDiagnostics.preambleConfidence = detector.confidence();
                    currentDiagnostics.fftPeakRatio = estimate.peakRatio();
                    counters.preamblesDetected++;
                }
                state = State::Preamble;
                return;
            }
            if (cursor > symbolSamples * 3) {
                const std::size_t eraseCount = cursor - symbolSamples * 2;
                samples.erase(samples.begin(), samples.begin() + eraseCount);
                cursor -= eraseCount;
            }
        }
    }

    void processPreamble() {
        const std::size_t symbolSamples = derived.samplesPerSymbol;
        while (haveSymbol(cursor)) {
            const SymbolEstimate estimate = demodulator.demodulateUpchirp(samples.data() + cursor, symbolSamples);
            if (estimate.peakRatio() >= 3.0f && detail::circularDistance(estimate.fractionalBin, preambleBin, derived.bins) <= 2.0f) {
                const float delta = detail::signedBins(estimate.fractionalBin - preambleBin, derived.bins);
                preambleBin = detail::wrapBins(preambleBin + delta * 0.2f, derived.bins);
                cursor += symbolSamples;
                continue;
            }
            syncStart = cursor;
            state = State::Sync;
            return;
        }
    }

    void processSync() {
        const std::size_t symbolSamples = derived.samplesPerSymbol;
        const std::size_t downStart = syncStart + 2 * symbolSamples;
        if (!haveSymbol(downStart)) { return; }

        const SymbolEstimate first = demodulator.demodulateUpchirp(samples.data() + syncStart, symbolSamples);
        const SymbolEstimate second = demodulator.demodulateUpchirp(samples.data() + syncStart + symbolSamples, symbolSamples);
        const SymbolEstimate down = demodulator.demodulateDownchirp(samples.data() + downStart, symbolSamples);
        const float upSigned = detail::signedBins(preambleBin, derived.bins);
        const float downSigned = detail::signedBins(down.fractionalBin, derived.bins);
        const float timingBins = 0.5f * (upSigned - downSigned);
        const long timingSamples = std::lround(timingBins * config.oversampling);
        if ((timingSamples > 0 && syncStart < static_cast<std::size_t>(timingSamples)) ||
            std::abs(timingSamples) > static_cast<long>(symbolSamples / 2)) {
            failSync();
            return;
        }
        const std::size_t alignedSync = static_cast<std::size_t>(static_cast<long long>(syncStart) - timingSamples);
        if (!haveSymbol(alignedSync + 2 * symbolSamples)) { return; }

        const SymbolEstimate alignedUp = demodulator.demodulateUpchirp(samples.data() + alignedSync - (alignedSync >= symbolSamples ? symbolSamples : 0), symbolSamples);
        const SymbolEstimate alignedDown = demodulator.demodulateDownchirp(samples.data() + alignedSync + 2 * symbolSamples, symbolSamples);
        const float alignedUpSigned = detail::signedBins(alignedUp.fractionalBin, derived.bins);
        const float alignedDownSigned = detail::signedBins(alignedDown.fractionalBin, derived.bins);
        cfoBins = 0.5f * (alignedUpSigned + alignedDownSigned);
        const float frequencyError = cfoBins * config.bandwidth / derived.bins;

        const SymbolEstimate alignedFirst = demodulator.demodulateUpchirp(samples.data() + alignedSync, symbolSamples, frequencyError);
        const SymbolEstimate alignedSecond = demodulator.demodulateUpchirp(samples.data() + alignedSync + symbolSamples, symbolSamples, frequencyError);
        const int firstNibble = syncNibbleFromBin(static_cast<int>(std::lround(alignedFirst.fractionalBin)), derived.bins, false);
        const int secondNibble = syncNibbleFromBin(static_cast<int>(std::lround(alignedSecond.fractionalBin)), derived.bins, true);
        {
            std::lock_guard<std::mutex> lock(metricsMutex);
            currentDiagnostics.observedSyncWord = static_cast<uint16_t>((firstNibble << 4u) | secondNibble);
            currentDiagnostics.cfoHz = frequencyError;
            currentDiagnostics.timingOffset = static_cast<float>(timingSamples);
            currentDiagnostics.preambleBin = preambleBin;
            currentDiagnostics.downchirpBin = down.fractionalBin;
            currentDiagnostics.firstSyncBin = alignedFirst.fractionalBin;
            currentDiagnostics.secondSyncBin = alignedSecond.fractionalBin;
        }
        if (!matchSyncWord(firstNibble, secondNibble, config.syncWord)) {
            failSync();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(metricsMutex);
            currentDiagnostics.cfoHz = frequencyError;
            currentDiagnostics.timingOffset = static_cast<float>(timingSamples);
            currentDiagnostics.fftPeakRatio = 0.5f * (first.peakRatio() + second.peakRatio());
        }
        frameCursor = alignedSync + 4 * symbolSamples + symbolSamples / 4;
        cursor = frameCursor;
        frameSymbols.clear();
        expectedFrameSymbols = 8;
        snrSum = symbolSnr(alignedFirst) + symbolSnr(alignedSecond);
        snrCount = 2;
        powerSum = 0.0;
        powerCount = 0;
        state = State::Header;
    }

    void failSync() {
        {
            std::lock_guard<std::mutex> lock(metricsMutex);
            counters.syncFailures++;
        }
        abortFrame(syncStart + derived.samplesPerSymbol);
    }

    bool demodulateTracked(uint16_t& symbol) {
        const int margin = config.oversampling;
        if (frameCursor < static_cast<std::size_t>(margin) || !haveSymbol(frameCursor, margin)) { return false; }
        const float frequencyError = cfoBins * config.bandwidth / derived.bins;
        const std::size_t bestPosition = frameCursor;
        const SymbolEstimate best = demodulator.demodulateUpchirp(samples.data() + bestPosition,
                                                                  derived.samplesPerSymbol,
                                                                  frequencyError);
        symbol = static_cast<uint16_t>(detail::modulo(static_cast<int>(std::lround(best.fractionalBin)), derived.bins));
        frameCursor = bestPosition + derived.samplesPerSymbol;
        cursor = frameCursor;
        snrSum += symbolSnr(best);
        snrCount++;
        for (std::size_t i = bestPosition; i < bestPosition + derived.samplesPerSymbol; i++) {
            powerSum += samples[i].re * samples[i].re + samples[i].im * samples[i].im;
        }
        powerCount += derived.samplesPerSymbol;
        return true;
    }

    void processFrameSymbols() {
        while (frameSymbols.size() < expectedFrameSymbols) {
            uint16_t symbol = 0;
            if (!demodulateTracked(symbol)) { return; }
            frameSymbols.push_back(symbol);
            compactFrameBuffer();
            if (state == State::Header && frameSymbols.size() == 8) {
                Frame frame;
                std::size_t required = 8;
                const PhyDecodeStatus status = decodePhySymbols(frameSymbols.data(), frameSymbols.size(), config, frame, &required);
                if (status == PhyDecodeStatus::InvalidHeader) {
                    populateMetrics(frame);
                    {
                        std::lock_guard<std::mutex> lock(metricsMutex);
                        counters.headerFailures++;
                    }
                    emit(frame);
                    abortFrame(frameCursor);
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(metricsMutex);
                    counters.headersDecoded++;
                }
                expectedFrameSymbols = required;
                state = State::Payload;
            }
        }

        Frame frame;
        if (decodePhySymbols(frameSymbols.data(), frameSymbols.size(), config, frame) != PhyDecodeStatus::Complete) {
            abortFrame(frameCursor);
            return;
        }
        populateMetrics(frame);
        {
            std::lock_guard<std::mutex> lock(metricsMutex);
            counters.payloadsDecoded++;
            if (frame.payloadCrcPresent && !frame.payloadCrcValid) { counters.payloadCrcFailures++; }
        }
        emit(frame);
        abortFrame(frameCursor);
    }

    void populateMetrics(Frame& frame) const {
        frame.snrDb = snrCount ? static_cast<float>(snrSum / snrCount) : 0.0f;
        frame.rssiDb = powerCount ? 10.0f * std::log10(static_cast<float>(powerSum / powerCount) + 1.0e-20f) : -200.0f;
        frame.frequencyErrorHz = cfoBins * config.bandwidth / derived.bins;
        frame.timingOffsetSamples = currentDiagnostics.timingOffset;
    }

    void emit(const Frame& frame) {
        if (handler) { handler(frame, context); }
    }

    void compactFrameBuffer() {
        const std::size_t keep = static_cast<std::size_t>(derived.samplesPerSymbol) * 2;
        if (frameCursor <= keep) { return; }
        const std::size_t eraseCount = frameCursor - keep;
        samples.erase(samples.begin(), samples.begin() + eraseCount);
        frameCursor -= eraseCount;
        cursor = frameCursor;
    }

    void abortFrame(std::size_t consumed) {
        consumed = (std::min)(consumed, samples.size());
        samples.erase(samples.begin(), samples.begin() + consumed);
        cursor = 0;
        syncStart = 0;
        frameCursor = 0;
        detector.reset();
        frameSymbols.clear();
        state = State::Search;
    }

    DerivedConfig derived;
    ChirpGenerator chirps;
    ChirpDemodulator demodulator;
    PreambleDetector detector;
    std::unique_ptr<multirate::RationalResampler<complex_t>> resampler;
    std::vector<complex_t> samples;
    std::vector<complex_t> resampled;
    State state = State::Search;
    std::size_t cursor = 0;
    std::size_t syncStart = 0;
    std::size_t frameCursor = 0;
    float preambleBin = 0.0f;
    float cfoBins = 0.0f;
    std::vector<uint16_t> frameSymbols;
    std::size_t expectedFrameSymbols = 8;
    double snrSum = 0.0;
    std::size_t snrCount = 0;
    double powerSum = 0.0;
    std::size_t powerCount = 0;
    mutable std::mutex metricsMutex;
    Stats counters;
    SyncDiagnostics currentDiagnostics;
};

LoRaDecoder::LoRaDecoder() : impl(new Impl()) {}

LoRaDecoder::LoRaDecoder(stream<complex_t>* input, double inputSampleRate, const Config& config,
                         FrameHandler handler, void* context) : LoRaDecoder() {
    init(input, inputSampleRate, config, handler, context);
}

LoRaDecoder::~LoRaDecoder() = default;

void LoRaDecoder::init(stream<complex_t>* input, double inputSampleRate, const Config& config,
                       FrameHandler handler, void* context) {
    impl->configure(inputSampleRate, config, handler, context);
    base_type::init(input);
}

void LoRaDecoder::initOffline(double inputSampleRate, const Config& config, FrameHandler handler, void* context) {
    impl->configure(inputSampleRate, config, handler, context);
    base_type::init(nullptr);
}

void LoRaDecoder::setConfig(const Config& config) {
    std::lock_guard<std::recursive_mutex> lock(base_type::ctrlMtx);
    base_type::tempStop();
    impl->configure(impl->inputSampleRate, config, impl->handler, impl->context);
    base_type::tempStart();
}

void LoRaDecoder::setInput(stream<complex_t>* input) {
    base_type::setInput(input);
    reset();
}

void LoRaDecoder::setInputSampleRate(double sampleRate) {
    std::lock_guard<std::recursive_mutex> lock(base_type::ctrlMtx);
    base_type::tempStop();
    impl->setRate(sampleRate);
    base_type::tempStart();
}

void LoRaDecoder::setFrameHandler(FrameHandler handler, void* context) {
    std::lock_guard<std::recursive_mutex> lock(base_type::ctrlMtx);
    impl->handler = handler;
    impl->context = context;
}

void LoRaDecoder::reset() {
    std::lock_guard<std::recursive_mutex> lock(base_type::ctrlMtx);
    impl->resetState();
}

void LoRaDecoder::process(const complex_t* samples, std::size_t count) {
    impl->feed(samples, count);
}

Stats LoRaDecoder::getStats() const {
    return impl->statsSnapshot();
}

SyncDiagnostics LoRaDecoder::getSyncDiagnostics() const {
    return impl->diagnosticsSnapshot();
}

int LoRaDecoder::run() {
    if (!base_type::_in) { return -1; }
    const int count = base_type::_in->read();
    if (count < 0) { return -1; }
    impl->feed(base_type::_in->readBuf, static_cast<std::size_t>(count));
    base_type::_in->flush();
    return count;
}

}
