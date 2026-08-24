#include <catch2/catch_test_macros.hpp>
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

    dsp::tap<float> makeTaps() {
        dsp::tap<float> taps = dsp::taps::alloc<float>(12);
        for (int i = 0; i < 12; i++) { taps.taps[i] = (float)(i + 1); }
        return taps;
    }

    void warmResampler(dsp::multirate::PolyphaseResampler<float>& resampler) {
        float input[3] = { 0.25f, -0.5f, 0.75f };
        float output[8];
        REQUIRE(resampler.process(3, input, output) == 8);
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

        REQUIRE(result == std::accumulate(outputStream.chunkSizes.begin(), outputStream.chunkSizes.end(), 0));
        REQUIRE(inputStream.flushed);
        REQUIRE(boundCalls == (int)outputStream.chunkSizes.size());
        REQUIRE(boundCalls >= 3);
        REQUIRE(outputStream.chunkSizes[0] == 1000000);
        REQUIRE(*std::max_element(outputStream.chunkSizes.begin(), outputStream.chunkSizes.end()) <= outputStream.getBufferSize());

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
            std::vector<float> block(input.begin() + inputOffset, input.begin() + inputOffset + blockSize);
            InputStream inputStream(block);
            int result = dsp::runBounded(&inputStream, outputStream,
                [&]() { return bounded.getMaxInputCount(outputStream.getBufferSize()); },
                [&](int count, const float* in, float* out) { return bounded.process(count, in, out); });
            REQUIRE(result >= 0);
            REQUIRE(inputStream.flushed);
            totalOutCount += result;
            inputOffset += blockSize;
        }

        REQUIRE(totalOutCount == expectedCount);
        REQUIRE(outputStream.samples == expected);
        REQUIRE(*std::max_element(outputStream.chunkSizes.begin(), outputStream.chunkSizes.end()) <= outputStream.getBufferSize());

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

        REQUIRE(result == expectedCount);
        REQUIRE(outputStream.samples == expected);
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

        REQUIRE(result == expectedCount);
        REQUIRE(outputStream.samples == expected);
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
        REQUIRE(outCount >= 0);
        REQUIRE(outCount <= (int)output.size());
    }
}

TEST_CASE("runBounded handles state-dependent bounds", "[dsp][bounded]") {
    testStateDependentBound();
}

TEST_CASE("runBounded matches unsplit processing for random blocks",
          "[dsp][bounded]") {
    testRandomBlocksMatchUnsplitProcessing();
}

TEST_CASE("PowerDecimator supports bounded processing",
          "[dsp][bounded][multirate]") {
    testPowerDecimatorBoundedProcessing();
}

TEST_CASE("RationalResampler supports bounded processing",
          "[dsp][bounded][multirate]") {
    testRationalResamplerBoundedProcessing();
}

TEST_CASE("RxVFO survives concurrent reconfiguration",
          "[dsp][bounded][channel]") {
    testConcurrentRxVFOReconfiguration();
}
