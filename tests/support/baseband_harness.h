#pragma once

// Shared plumbing for the diagnostic harnesses that replay a recorded baseband file through the
// real detection pipeline.
//
// These harnesses are hidden ([.] tag) because they need large recordings that are not in the
// repo. What lives here is only the file reader and the front-end FFT replica, so that several
// harnesses can agree on exactly what the detector is being fed.

#include <dsp/types.h>
#include <utils/arrays.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace baseband {

    /**
     * An int16 interleaved IQ WAV, read a frame at a time.
     */
    struct BasebandFile {
        std::ifstream stream;
        double sampleRate = 0.0;
        uint64_t dataBytes = 0;

        bool open(const std::string& path) {
            stream.open(path, std::ios::binary);
            if (!stream.is_open()) { return false; }

            char riff[12];
            stream.read(riff, 12);
            if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) { return false; }

            // Walk the chunk list rather than assuming a 44 byte header.
            while (stream.good()) {
                char id[4];
                uint32_t size = 0;
                stream.read(id, 4);
                stream.read((char*)&size, 4);
                if (!stream.good()) { return false; }

                if (memcmp(id, "fmt ", 4) == 0) {
                    std::vector<char> fmt(size);
                    stream.read(fmt.data(), size);
                    uint32_t sr = 0;
                    memcpy(&sr, fmt.data() + 4, 4);
                    sampleRate = sr;
                }
                else if (memcmp(id, "data", 4) == 0) {
                    dataBytes = size;
                    return sampleRate > 0.0;
                }
                else {
                    stream.seekg(size, std::ios::cur);
                }
            }
            return false;
        }

        // Seconds of signal in the file.
        double duration() const {
            if (sampleRate <= 0.0) { return 0.0; }
            return (double)dataBytes / 4.0 / sampleRate;
        }

        // Read one frame of `n` complex samples. Returns false at end of file.
        bool readFrame(std::vector<dsp::complex_t>& out, int n) {
            out.resize(n);
            std::vector<int16_t> raw(n * 2);
            stream.read((char*)raw.data(), raw.size() * sizeof(int16_t));
            if (stream.gcount() < (std::streamsize)(raw.size() * sizeof(int16_t))) { return false; }
            for (int i = 0; i < n; i++) {
                out[i].re = raw[i * 2] / 32768.0f;
                out[i].im = raw[(i * 2) + 1] / 32768.0f;
            }
            return true;
        }
    };

    // Reproduces what IQFrontEnd::handler does: windowed FFT, fftshifted, power spectrum in dB.
    class FrameFFT {
    public:
        explicit FrameFFT(int size) : size(size) {
            plan = dsp::arrays::allocateFFTWPlan(false, size);
            in = std::make_shared<std::vector<dsp::complex_t>>(size);
            window.resize(size);
            for (int i = 0; i < size; i++) {
                // Nuttall, matching the default front end window.
                double x = 2.0 * M_PI * i / (size - 1);
                window[i] = (float)(0.355768 - 0.487396 * cos(x) + 0.144232 * cos(2 * x) -
                                    0.012604 * cos(3 * x));
            }
        }

        void compute(const std::vector<dsp::complex_t>& samples, std::vector<float>& outDb) {
            auto& iv = *in;
            for (int i = 0; i < size; i++) {
                iv[i].re = samples[i].re * window[i];
                iv[i].im = samples[i].im * window[i];
            }
            dsp::arrays::npfftfft(in, plan);
            dsp::arrays::swapfft(plan->getOutput());
            auto mag = dsp::arrays::npabsolute(plan->getOutput());
            outDb.resize(size);
            for (int i = 0; i < size; i++) {
                outDb[i] = 20.0f * log10f((mag->at(i) / size) + 1e-12f);
            }
        }

    private:
        int size;
        dsp::arrays::Arg<dsp::arrays::FFTPlan> plan;
        dsp::arrays::ComplexArray in;
        std::vector<float> window;
    };

    /**
     * Collect up to `maxFrames` dB frames, decimated in time the way the front end does
     * (one FFT per 1/`frameRate` s of signal).
     */
    inline std::vector<std::vector<float>> collectFrames(BasebandFile& file, int fftSize,
                                                         int maxFrames, double frameRate = 20.0) {
        FrameFFT fft(fftSize);
        std::vector<dsp::complex_t> samples;
        std::vector<float> frameDb;
        std::vector<std::vector<float>> frames;

        const int stride = (int)(file.sampleRate / frameRate);
        while ((int)frames.size() < maxFrames) {
            if (!file.readFrame(samples, fftSize)) { break; }
            fft.compute(samples, frameDb);
            frames.push_back(frameDb);
            if (stride > fftSize) {
                file.stream.seekg((std::streamoff)(stride - fftSize) * 4, std::ios::cur);
            }
        }
        return frames;
    }

    // Bin index holding `frequency`, given a frame spanning [center - span/2, center + span/2].
    inline int binFor(double frequency, double centerFrequency, double spanHz, int binCount) {
        double low = centerFrequency - (spanHz / 2.0);
        return (int)std::lround((frequency - low) / (spanHz / (double)binCount) - 0.5);
    }
}
