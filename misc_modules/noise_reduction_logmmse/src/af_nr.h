
#pragma once

#pragma once
#include <dsp/block.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <dsp/processor.h>
#include <math.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include "utils/arrays.h"
#include "logmmse.h"
#include "omlsa_mcra.h"
#include "utils/stream_tracker.h"
#include <array>

namespace dsp {

    using namespace ::dsp::arrays;
    using namespace ::dsp::logmmse;

    template <int V>
    struct SMAStream {
        std::array<dsp::complex_t, V> delayLine{};
        dsp::complex_t runningSum{};
        size_t delaySize = 0;
        size_t delayPosition = 0;
        std::vector<dsp::complex_t> output;
        size_t outputReadOffset = 0;

        void write(dsp::complex_t* values, size_t size) {
            output.reserve(output.size() + size);
            for (size_t q = 0; q < size; q++) {
                const auto value = values[q];
                if (delaySize < V) {
                    delayLine[delaySize++] = value;
                    runningSum += value;
                    output.emplace_back(value);
                }
                else {
                    runningSum.re -= delayLine[delayPosition].re;
                    runningSum.im -= delayLine[delayPosition].im;
                    delayLine[delayPosition] = value;
                    runningSum += value;
                    delayPosition = (delayPosition + 1) % V;
                    output.emplace_back(complex_t{ runningSum.re / V, runningSum.im / V });
                }
            }
        }

        void read(dsp::complex_t* values, size_t size) {
            if (output.size() - outputReadOffset < size) {
                abort();
            }
            memmove(values, output.data() + outputReadOffset, size * sizeof(dsp::complex_t));
            outputReadOffset += size;
            if (outputReadOffset * 2 >= output.size()) {
                output.erase(output.begin(), output.begin() + outputReadOffset);
                outputReadOffset = 0;
            }
        }

        size_t available() {
            const size_t pending = output.size() - outputReadOffset;
            return pending > V ? pending - V : 0;
        }
    };

    struct AFNR_OMLSA_MCRA : public Processor<stereo_t, stereo_t> {
        using base_type = Processor<stereo_t, stereo_t>;
        EventHandler<bool> txHandler;
        bool failed = false;
        dsp::omlsa_mcra omlsa_mcra;
        std::vector<stereo_t> buffer;
        size_t bufferReadOffset = 0;
        std::vector<short> processIn;
        std::vector<short> processOut;
        bool allowed = false;
        bool allowed2 = true;       // just convenient for various conditions
        float preAmpGain = 0.0f;
        int instanceCount = 0;
//        FILE *dumpIn = nullptr;

        AFNR_OMLSA_MCRA() : nrIn("OMLSA_IN"), nrOut("OMLSA_OUT") {
            static int _cnt = 0;
            instanceCount = _cnt++;
#ifdef __linux__
            char fname[256];
            snprintf(fname, sizeof fname, "/tmp/omlsa_in_%d.raw", instanceCount);
//            dumpIn = fopen(fname, "wb");
#endif
        }

        void init(stream<stereo_t>* in) override {
            base_type::init(in);
        }

        void setInput(stream<stereo_t>* in) override {
            base_type ::setInput(in);
        }

        void start() override {
            txHandler.ctx = this;
            txHandler.handler = [](bool txActive, void *ctx) {
                auto _this = (AFNR_OMLSA_MCRA*)ctx;
//                _this->params.hold = txActive;
            };


            omlsa_mcra.setSampleRate(48000);
            const int size = omlsa_mcra.blockSize();
            processIn.resize(size);
            processOut.resize(3 * size);
            sigpath::txState.bindHandler(&txHandler);
            block::start();
        }

        void stop() override {
            block::stop();
            sigpath::txState.unbindHandler(&txHandler);
        }

        StreamTracker nrIn, nrOut;
        float scaled = 32767.0;      // amplitude shaper

        void process(stereo_t *readBuf, int count, stereo_t *writeBuf, int &wrote) {
            auto mult = std::pow(10.0f, preAmpGain/20.0f);
            for(int q=0; q<count; q++) {
                readBuf[q].l *= mult;
                readBuf[q].r *= mult;
            }
            if (!allowed || !allowed2) {
                std::copy(readBuf, readBuf+count, writeBuf);
                wrote = count;
                buffer.clear();
                bufferReadOffset = 0;
                return;
            } else {
                buffer.reserve(buffer.size() + count);
                buffer.insert(buffer.end(), readBuf, readBuf + count);
                int blockSize = omlsa_mcra.blockSize();
                if (buffer.size() - bufferReadOffset >= blockSize) {
                    double max = 0;
                    if (scaled < 32757) {
                        scaled += 10;
                    }
                    for(int q=0; q<blockSize; q++) {
                        if (fabs(buffer[bufferReadOffset + q].l) > max) {
                            max = fabs(buffer[bufferReadOffset + q].l);
                        }
                        processIn[q] = buffer[bufferReadOffset + q].l * scaled;
                    }
                    bool processedOk = true;
                    if (max > 32767/scaled) {
                        float newScaled = 32767 / max;
                        for (int q = 0; q < blockSize; q++) {
                            processIn[q] = buffer[bufferReadOffset + q].l * newScaled;
                        }
                        scaled = newScaled;
                    }
//                    auto ctm = currentTimeNanos();
//                    if (dumpIn) {
//                        fwrite((short*)processIn.data(), 1, blockSize * sizeof(short), dumpIn);
//                    }
                    processedOk = omlsa_mcra.process((short*)processIn.data(), blockSize, (short*)processOut.data(), wrote);
//                    ctm = currentTimeNanos() - ctm;
//                    nrIn.add(blockSize);
//                    nrOut.add(wrote);
                    if (!processedOk) {
                        flog::warn("OMLSA !processedOk");
                        omlsa_mcra.reset();
                        std::copy(buffer.begin() + bufferReadOffset, buffer.end(), writeBuf);
                        wrote = buffer.size() - bufferReadOffset;
                        buffer.clear();
                        bufferReadOffset = 0;
                    }
                    else {
                        bufferReadOffset += blockSize;
                        if (bufferReadOffset * 2 >= buffer.size()) {
                            buffer.erase(buffer.begin(), buffer.begin() + bufferReadOffset);
                            bufferReadOffset = 0;
                        }
                        for(int q=0; q<wrote; q++) {
                            writeBuf[q].r = writeBuf[q].l = processOut[q] / scaled;
                        }
                    }
                }
                else {
                    wrote = 0;
                }
            }
        }

        int run() override {

            int count = _in->read();
            if (count < 0) { return -1; }
//            flog::info("Sample count: {}", count);
            int wrote;
//            auto ctm = currentTimeMillis();
            process(_in->readBuf, count, out.writeBuf, wrote);
            _in->flush();
//            flog::info("afnr.omlsa_mcra: input = {}, output = {}", count, wrote);
            if (!out.swap(wrote)) {
                flog::info("afnr.omlsa_mcra: swap failed");
                return 0;
            }

            return 1;
        }






    };


    struct AFNRLogMMSE : public Processor<complex_t, complex_t> {

        using base_type = Processor<complex_t, complex_t>;

        ComplexArray worker1c;

        void init(stream<complex_t>* in) override {
            base_type::init(in);
        }

        void setInput(stream<complex_t>* in) override {
            base_type ::setInput(in);
            params.reset();
        }

        AFNRLogMMSE() {
            worker1c = std::make_shared<std::vector<complex_t>>();
            params.forceAudio = true;
        }


        LogMMSE::SavedParamsC params;

        double getVFOFrequency() {
            if (gui::waterfall.selectedVFO == "") {
                return gui::waterfall.getCenterFrequency();
            }
            else {
                return gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(gui::waterfall.selectedVFO);
            }
        }

        double getVFOBandwidth() {
            if (gui::waterfall.selectedVFO == "") {
                return gui::waterfall.getBandwidth();
            }
            else {
                return sigpath::vfoManager.getBandwidth(gui::waterfall.selectedVFO);
            }
        }

        int processingBandwidthHz = 48000/2;

        std::mutex freqMutex;

        void setProcessingBandwidth(int bandwidthHz) {
            flog::info("Refreshing noise profile for AF NR (logmmse)");
            freqMutex.lock();
            this->processingBandwidthHz = bandwidthHz;
            params.reset();
            freqMutex.unlock();
        }

        void refreshNoiseProfile() {
            flog::info("Refreshing noise profile for AF NR (logmmse)");
            freqMutex.lock();
            params.reset();
            freqMutex.unlock();
        }


        double lastVFOFrequency = 0.0;
        double lastVFOBandwidth = 0.0;
        int switchTrigger = 0;
        int overlapTrigger = -100000;

        bool allowed = false;   // initial value
        int afnrBandwidth = 10; // this is UI model value, just stored there.
        SMAStream<5> sma;
        EventHandler<bool> txHandler;

        void start() override {
            txHandler.ctx = this;
            txHandler.handler = [](bool txActive, void *ctx) {
                auto _this = (AFNRLogMMSE*)ctx;
                _this->params.hold = txActive;
            };
            sigpath::txState.bindHandler(&txHandler);
            block::start();
        }

        void process(complex_t *readBuf, int count, complex_t *writeBuf, int &wrote) {
            wrote = 0;
            std::lock_guard<std::mutex> lock(freqMutex);
            auto curSize = worker1c->size();
            worker1c->resize(curSize + count);
            memmove(worker1c->data() + curSize, readBuf, count * sizeof(complex_t));
            switchTrigger += count;
            overlapTrigger += count;

            int noiseFrames = 12;
            int fram = processingBandwidthHz / 100;
            auto Slen = (int)floor(0.02 * processingBandwidthHz);
            if (!params.noise_mu2) {
                if (worker1c->size() >= noiseFrames * Slen) {
                    // finally can sample
                    flog::info("Sampling, total samples: {0}, will be used: {1}", (int64_t)worker1c->size(), noiseFrames * Slen);
                    LogMMSE::logmmse_sample(worker1c, processingBandwidthHz, 0.15f, &params, noiseFrames);
                    worker1c->erase(worker1c->begin(), worker1c->begin() + curSize); // skip everything already sent to the output before
                } else {
                    // pass throug until it fills
                    memmove(writeBuf, worker1c->data() + curSize, count * sizeof(complex_t));
                    wrote = count;
                    return;
                }
            }
            int size1 = worker1c->size();
            if (worker1c->size() >= 4 * params.Slen && params.noise_mu2) {
                ALLOC_AND_CHECK(worker1c, size1, "afnr point -5")
                auto rv = LogMMSE::logmmse_all(worker1c, size1, processingBandwidthHz, 0.15f, &params);
                int limit = rv->size();
                auto dta = rv->data();
                ALLOC_AND_CHECK(worker1c, size1, "afnr point -3")

                sma.write(dta, limit);
                ALLOC_AND_CHECK(worker1c, size1, "afnr point -2")

                if (sma.available() >= limit) {
                    wrote = limit;
                    ALLOC_AND_CHECK(worker1c, size1, "afnr point -1")
                    sma.read(writeBuf, limit);
                    ALLOC_AND_CHECK(worker1c, size1, "afnr point 0")
                    memmove(worker1c->data(), ((complex_t*)worker1c->data()) + limit, sizeof(complex_t) * (worker1c->size() - limit));
                    ALLOC_AND_CHECK(worker1c, size1, "afnr point 0.1")
                    unsigned long nsize = worker1c->size() - limit;
                    char buf[100];
                    snprintf(buf, sizeof(buf), "afnr point 0.2 size = %lld curr=%lld",(long long)nsize, (long long)worker1c->size());
                    worker1c->resize(nsize);
                    size1 = nsize;
                    ALLOC_AND_CHECK(worker1c, size1, buf)
                }
            } else {

            }
            return;
        }

        void stop() override {
            block::stop();
            sigpath::txState.unbindHandler(&txHandler);
        }

        int run() override {

            if (getVFOFrequency() != lastVFOFrequency) {
                lastVFOFrequency = getVFOFrequency();
                refreshNoiseProfile();
            }
            if (getVFOBandwidth() != lastVFOBandwidth) {
                lastVFOBandwidth = getVFOBandwidth();
                refreshNoiseProfile();
            }

            int count = _in->read();
            if (count < 0) { return -1; }

            int wrote;
            process(_in->readBuf, count, out.writeBuf, wrote);
            _in->flush();
            if (!out.swap(wrote)) {
                flog::info("afnr.mmse: swap failed");
                return 0;
            }

            return 1;
        }
    };


}
