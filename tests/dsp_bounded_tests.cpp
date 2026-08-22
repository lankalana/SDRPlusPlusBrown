#include <dsp/multirate/polyphase_resampler.h>
#include <dsp/multirate/power_decimator.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/channel/rx_vfo.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

namespace {
    class InputStream : public dsp::stream<float> {
    public:
        explicit InputStream(const std::vector<float>& samples) : sampleCount((int)samples.size()) {
            setBufferSize((std::max)(sampleCount, 1));
            std::copy(samples.begin(), samples.end(), readBuf);
        }

        int read() override {
            if (readCalled) { return -1; }
            readCalled = true;
            return sampleCount;
        }

        void flush() override { flushed = true; }

        bool flushed = false;

    private:
        int sampleCount;
        bool readCalled = false;
    };

    class OutputStream : public dsp::stream<float> {
    public:
        explicit OutputStream(int capacity, bool captureSamples = true) : captureSamples(captureSamples) { setBufferSize(capacity); }

        bool swap(int count) override {
            if (count > getBufferSize()) { return false; }
            chunkSizes.push_back(count);
            if (captureSamples) { samples.insert(samples.end(), writeBuf, &writeBuf[count]); }
            return true;
        }

        std::vector<int> chunkSizes;
        std::vector<float> samples;

    private:
        bool captureSamples;
    };

    class ResamplerState {
    public:
        int getMaxInputCount(int maxOutputCount) const {
            return offset + (phase + (maxOutputCount * decim)) / interp;
        }

        int process(int count) {
            int outCount = 0;
            while (offset < count) {
                outCount++;
                phase += decim;
                offset += phase / interp;
                phase %= interp;
            }
            offset -= count;
            return outCount;
        }

    private:
        int interp = 12;
        int decim = 5;
        int phase = 4;
        int offset = 0;
    };

    void require(bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }

    dsp::tap<float> makeTaps() {
        dsp::tap<float> taps = dsp::taps::alloc<float>(12);
        for (int i = 0; i < 12; i++) { taps.taps[i] = (float)(i + 1); }
        return taps;
    }

    void warmResampler(dsp::multirate::PolyphaseResampler<float>& resampler) {
        float input[3] = { 0.25f, -0.5f, 0.75f };
        float output[8];
        require(resampler.process(3, input, output) == 8, "unexpected warm-up output count");
    }

    void testStateDependentBound() {
        std::vector<float> input(1000000);
        InputStream inputStream(input);
        OutputStream outputStream(1000000, false);
        ResamplerState resampler;
        int boundCalls = 0;
        int result = dsp::runBounded(&inputStream, outputStream,
            [&]() {
                boundCalls++;
                return resampler.getMaxInputCount(outputStream.getBufferSize());
            },
            [&](int count, const float*, float*) { return resampler.process(count); });

        require(result == std::accumulate(outputStream.chunkSizes.begin(), outputStream.chunkSizes.end(), 0),
            "bounded processing did not return the total output count");
        require(inputStream.flushed, "bounded processing did not flush the input");
        require(boundCalls == (int)outputStream.chunkSizes.size(), "input bound was not recalculated for every chunk");
        require(boundCalls >= 3, "state-dependent test did not exercise multiple chunks");
        require(outputStream.chunkSizes[0] == 1000000, "first chunk did not fill the output buffer");
        require(*std::max_element(outputStream.chunkSizes.begin(), outputStream.chunkSizes.end()) <= outputStream.getBufferSize(),
            "a chunk exceeded the output capacity");

    }

    void testRandomBlocksMatchUnsplitProcessing() {
        constexpr int sampleCount = 256;
        std::vector<float> input(sampleCount);
        unsigned int randomState = 0x13579BDFu;
        for (float& sample : input) {
            randomState = randomState * 1664525u + 1013904223u;
            sample = (float)(randomState & 0xFFFFu) / 32768.0f - 1.0f;
        }

        dsp::tap<float> referenceTaps = makeTaps();
        dsp::multirate::PolyphaseResampler<float> reference(NULL, 12, 5, referenceTaps);
        warmResampler(reference);
        std::vector<float> expected(700);
        int expectedCount = reference.process(sampleCount, input.data(), expected.data());
        expected.resize(expectedCount);

        dsp::tap<float> boundedTaps = makeTaps();
        dsp::multirate::PolyphaseResampler<float> bounded(NULL, 12, 5, boundedTaps);
        warmResampler(bounded);
        OutputStream outputStream(17);
        int inputOffset = 0;
        int totalOutCount = 0;
        randomState = 0x2468ACE0u;
        while (inputOffset < sampleCount) {
            randomState = randomState * 1103515245u + 12345u;
            int blockSize = (std::min)(sampleCount - inputOffset, 1 + (int)(randomState % 37u));
            std::vector<float> block(&input[inputOffset], &input[inputOffset + blockSize]);
            InputStream inputStream(block);
            int result = dsp::runBounded(&inputStream, outputStream,
                [&]() { return bounded.getMaxInputCount(outputStream.getBufferSize()); },
                [&](int count, const float* in, float* out) { return bounded.process(count, in, out); });
            require(result >= 0 && inputStream.flushed, "random input block was not consumed");
            totalOutCount += result;
            inputOffset += blockSize;
        }

        require(totalOutCount == expectedCount, "bounded processing returned the wrong total output count");
        require(outputStream.samples == expected, "bounded output differs from unsplit processing");
        require(*std::max_element(outputStream.chunkSizes.begin(), outputStream.chunkSizes.end()) <= outputStream.getBufferSize(),
            "tiny-buffer processing exceeded the output capacity");

        dsp::taps::free(boundedTaps);
        dsp::taps::free(referenceTaps);
    }

    void testPowerDecimatorBoundedProcessing() {
        std::vector<float> input(257);
        for (int i = 0; i < (int)input.size(); i++) { input[i] = (float)((i % 29) - 14) / 15.0f; }

        dsp::multirate::PowerDecimator<float> reference(NULL, 4);
        std::vector<float> expected(input.size());
        int expectedCount = reference.process((int)input.size(), input.data(), expected.data());
        expected.resize(expectedCount);

        dsp::multirate::PowerDecimator<float> bounded(NULL, 4);
        InputStream inputStream(input);
        OutputStream outputStream(7);
        int result = dsp::runBounded(&inputStream, outputStream,
            [&]() { return bounded.getMaxInputCount(outputStream.getBufferSize()); },
            [&](int count, const float* in, float* out) { return bounded.process(count, in, out); });

        require(result == expectedCount, "power decimator returned the wrong output count");
        require(outputStream.samples == expected, "bounded power decimator output differs from unsplit processing");
    }

    void testRationalResamplerBoundedProcessing() {
        std::vector<float> input(128);
        for (int i = 0; i < (int)input.size(); i++) { input[i] = (float)((i % 23) - 11) / 12.0f; }

        dsp::multirate::RationalResampler<float> reference(NULL, 5000.0, 12000.0);
        std::vector<float> expected(400);
        int expectedCount = reference.process((int)input.size(), input.data(), expected.data());
        expected.resize(expectedCount);

        dsp::multirate::RationalResampler<float> bounded(NULL, 5000.0, 12000.0);
        InputStream inputStream(input);
        OutputStream outputStream(17);
        int result = dsp::runBounded(&inputStream, outputStream,
            [&]() { return bounded.getMaxInputCount(outputStream.getBufferSize()); },
            [&](int count, const float* in, float* out) { return bounded.process(count, in, out); });

        require(result == expectedCount, "rational resampler returned the wrong output count");
        require(outputStream.samples == expected, "bounded rational resampler output differs from unsplit processing");
    }

    void testConcurrentRxVFOReconfiguration() {
        dsp::channel::RxVFO vfo(NULL, 48000.0, 12000.0, 6000.0, 0.0);
        std::thread rateThread([&]() {
            for (int i = 0; i < 8; i++) {
                double outSamplerate = (i & 1) ? 12000.0 : 16000.0;
                vfo.setOutSamplerate(outSamplerate, 6000.0);
            }
        });
        std::thread bandwidthThread([&]() {
            for (int i = 0; i < 8; i++) { vfo.setBandwidth((i & 1) ? 5000.0 : 6000.0); }
        });
        rateThread.join();
        bandwidthThread.join();

        std::vector<dsp::complex_t> input(64);
        std::vector<dsp::complex_t> output(64);
        int outCount = vfo.process((int)input.size(), input.data(), output.data());
        require(outCount >= 0 && outCount <= (int)output.size(), "RxVFO failed after concurrent reconfiguration");
    }
}

int main() {
    testStateDependentBound();
    testRandomBlocksMatchUnsplitProcessing();
    testPowerDecimatorBoundedProcessing();
    testRationalResamplerBoundedProcessing();
    testConcurrentRxVFOReconfiguration();
    return 0;
}
