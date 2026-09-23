// Diagnostic harness: run the real calibration/detection/tracking pipeline over a recorded
// baseband file and print what it finds.
//
// Hidden by default (the [.] tag) because it needs a large recording that is not in the repo.
// Run it explicitly:
//
//   sdrpp_core_tests "[fmdiag]" -s
//
// Point it at a file with SDRPP_TEST_BASEBAND=<path> (int16 interleaved IQ WAV).

#include <catch2/catch_test_macros.hpp>

#include <dsp/detector/noise_floor.h>
#include <dsp/detector/signal_tracker.h>
#include <dsp/detector/spectrum_detector.h>
#include <utils/arrays.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace dsp::detector;

namespace {

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

    struct Summary {
        int detectionCount = 0;
        double narrowest = 1e18;
        double widest = 0.0;
        double medianBandwidth = 0.0;
        std::vector<double> bandwidths;
    };

    Summary summarize(const std::vector<DetectedSignal>& signals) {
        Summary s;
        s.detectionCount = (int)signals.size();
        for (const auto& sig : signals) {
            s.narrowest = std::min(s.narrowest, sig.bandwidth);
            s.widest = std::max(s.widest, sig.bandwidth);
            s.bandwidths.push_back(sig.bandwidth);
        }
        if (!s.bandwidths.empty()) {
            auto sorted = s.bandwidths;
            std::sort(sorted.begin(), sorted.end());
            s.medianBandwidth = sorted[sorted.size() / 2];
        }
        else {
            s.narrowest = 0.0;
        }
        return s;
    }
}

TEST_CASE("baseband recording detection survey", "[.][fmdiag]") {
    const char* env = std::getenv("SDRPP_TEST_BASEBAND");
    std::string path = env ? env : "FM_radio_10s_int16.wav";

    BasebandFile file;
    if (!file.open(path)) {
        WARN("Could not open baseband file: " + path);
        return;
    }

    const int FFT_SIZE = 16384;
    const double SPAN = file.sampleRate;
    const double CENTER = 0.0; // report offsets from the capture centre
    const double BIN_HZ = SPAN / FFT_SIZE;

    printf("\n=== %s ===\n", path.c_str());
    printf("sample rate %.0f Hz, FFT %d bins, %.1f Hz/bin\n", file.sampleRate, FFT_SIZE, BIN_HZ);

    FrameFFT fft(FFT_SIZE);
    std::vector<dsp::complex_t> samples;
    std::vector<float> frameDb;

    // Decimate frames in time the way the front end does: one FFT per 1/20 s of signal.
    const int FRAME_STRIDE = (int)(file.sampleRate / 20.0);

    // Pass 1: collect every frame we can afford, so each averaging setting can be replayed.
    std::vector<std::vector<float>> frames;
    while (frames.size() < 200) {
        if (!file.readFrame(samples, FFT_SIZE)) { break; }
        fft.compute(samples, frameDb);
        frames.push_back(frameDb);
        file.stream.seekg((std::streamoff)(FRAME_STRIDE - FFT_SIZE) * 4, std::ios::cur);
    }
    printf("collected %d frames\n\n", (int)frames.size());
    REQUIRE(frames.size() > 40);

    auto averaged = [&](int start, int count) {
        std::vector<float> avg(FFT_SIZE, 0.0f);
        int n = 0;
        for (int f = start; f < start + count && f < (int)frames.size(); f++) {
            for (int b = 0; b < FFT_SIZE; b++) { avg[b] += frames[f][b]; }
            n++;
        }
        for (int b = 0; b < FFT_SIZE; b++) { avg[b] /= (float)std::max(n, 1); }
        return avg;
    };

    // What a flat floor taken from the spectrum itself looks like.
    float flatFloorDb = NoiseFloorModel::estimateFlatFloorDb(frames[0].data(), FFT_SIZE, 0.25f);
    printf("flat floor estimate (25th percentile of one frame): %.1f dB\n\n", flatFloorDb);

    auto runSurvey = [&](const char* label, NoiseFloorModel& floor, int avgFrames, double minBwHz,
                         double mergeGapHz, bool listSignals) {
        SignalTracker tracker;
        tracker.params.activationMs = 300;
        tracker.params.releaseMs = 2000;

        DetectionParams p;
        p.minBins = 2;
        p.minBandwidthHz = minBwHz;
        p.mergeGapHz = mergeGapHz;

        int totalDetections = 0;
        int frameCount = 0;
        uint64_t t = 0;
        for (int f = 0; f + avgFrames <= (int)frames.size(); f += avgFrames) {
            auto avg = averaged(f, avgFrames);
            auto signals = detectSignals(avg.data(), FFT_SIZE, floor, CENTER, SPAN, p);
            totalDetections += (int)signals.size();
            frameCount++;
            tracker.update(signals, t);
            t += (uint64_t)(50 * avgFrames);
        }

        auto tracked = tracker.getTracked();
        std::vector<DetectedSignal> asSignals;
        for (const auto& tr : tracked) { asSignals.push_back(tr.signal); }
        auto s = summarize(asSignals);

        printf("%-10s avg=%2d minBw=%4.0f gap=%3.0f kHz | %5.1f det/frame | %2d tracked, "
               "bw min %6.1f median %6.1f max %7.1f kHz\n",
               label, avgFrames, minBwHz / 1e3, mergeGapHz / 1e3,
               frameCount ? (double)totalDetections / frameCount : 0.0, s.detectionCount,
               s.narrowest / 1e3, s.medianBandwidth / 1e3, s.widest / 1e3);

        if (listSignals) {
            for (const auto& tr : tracked) {
                printf("    %+9.3f MHz  bw %7.1f kHz  snr %5.1f dB  %s\n",
                       tr.signal.centerFrequency / 1e6, tr.signal.bandwidth / 1e3, tr.signal.snrDb,
                       toString(tr.state));
            }
        }
    };

    printf("--- flat manual floor, margin 10 dB ---\n");
    for (int avgFrames : { 1, 4, 16 }) {
        for (double minBwHz : { 0.0, 50e3, 100e3 }) {
            NoiseFloorModel floor;
            floor.configure(FFT_SIZE, BIN_HZ);
            floor.setMode(NoiseFloorMode::MANUAL);
            floor.setManualFloorDb(flatFloorDb);
            floor.setMarginDb(10.0f);
            runSurvey("manual", floor, avgFrames, minBwHz, 20e3,
                      avgFrames == 4 && minBwHz == 100e3);
        }
    }

    printf("\n--- measured spectral floor, 2 MHz window, margin 10 dB ---\n");
    for (int avgFrames : { 1, 4, 16 }) {
        for (double minBwHz : { 0.0, 50e3, 100e3 }) {
            NoiseFloorModel floor;
            floor.configure(FFT_SIZE, BIN_HZ);
            floor.setSpectralWindowHz(2e6);
            floor.setSpectralPercentile(0.25f);
            floor.setMarginDb(10.0f);
            floor.startMeasurement(20);
            for (int f = 0; f < 20; f++) { floor.addFrame(frames[f].data(), FFT_SIZE); }
            REQUIRE(floor.isUsable());
            runSurvey("measured", floor, avgFrames, minBwHz, 20e3,
                      avgFrames == 4 && minBwHz == 100e3);
        }
    }
    printf("\n");
}
