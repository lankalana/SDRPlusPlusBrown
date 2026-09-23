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

#include <support/baseband_harness.h>

#include <dsp/detector/noise_floor.h>
#include <signal_path/auto_receiver.h>
#include <dsp/detector/signal_tracker.h>
#include <dsp/detector/spectrum_detector.h>
#include <utils/arrays.h>
#include <utils/event.h>

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

    using baseband::BasebandFile;
    using baseband::FrameFFT;

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
    const double BIN_HZ = SPAN / FFT_SIZE;

    // Absolute centre frequency, so detections can be checked against a known channel raster.
    const char* centerEnv = std::getenv("SDRPP_TEST_CENTER");
    const double CENTER = centerEnv ? atof(centerEnv) : 0.0;
    const char* rasterEnv = std::getenv("SDRPP_TEST_RASTER");
    const double RASTER = rasterEnv ? atof(rasterEnv) : 0.0;

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
            std::vector<DetectedSignal> sorted = asSignals;
            std::sort(sorted.begin(), sorted.end(),
                      [](const DetectedSignal& a, const DetectedSignal& b) {
                          return a.centerFrequency < b.centerFrequency;
                      });
            for (const auto& sig : sorted) {
                // If a channel raster is known, how far the detected centre sits from it is a
                // direct check that centre/bandwidth are computed correctly.
                char rasterNote[64] = "";
                if (RASTER > 0.0) {
                    double nearest = std::round(sig.centerFrequency / RASTER) * RASTER;
                    snprintf(rasterNote, sizeof rasterNote, "  raster %+7.1f kHz",
                             (sig.centerFrequency - nearest) / 1e3);
                }
                printf("    %10.4f MHz  bw %7.1f kHz  snr %5.1f dB%s\n",
                       sig.centerFrequency / 1e6, sig.bandwidth / 1e3, sig.snrDb, rasterNote);
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

    printf("\n--- merge gap sweep (flat floor, avg=4, minBw=100 kHz, maxBw=400 kHz) ---\n");
    for (double gapHz : { 10e3, 30e3, 60e3, 100e3, 150e3 }) {
        NoiseFloorModel floor;
        floor.configure(FFT_SIZE, BIN_HZ);
        floor.setMode(NoiseFloorMode::MANUAL);
        floor.setManualFloorDb(flatFloorDb);
        floor.setMarginDb(10.0f);

        SignalTracker tracker;
        tracker.params.activationMs = 300;
        tracker.params.releaseMs = 2000;
        DetectionParams p;
        p.minBins = 2;
        p.minBandwidthHz = 100e3;
        p.maxBandwidthHz = 400e3;
        p.mergeGapHz = gapHz;

        uint64_t t = 0;
        for (int f = 0; f + 4 <= (int)frames.size(); f += 4) {
            auto avg = averaged(f, 4);
            tracker.update(detectSignals(avg.data(), FFT_SIZE, floor, CENTER, SPAN, p), t);
            t += 200;
        }
        auto tracked = tracker.getTracked();
        std::vector<DetectedSignal> sigs;
        for (const auto& tr : tracked) { sigs.push_back(tr.signal); }
        auto s = summarize(sigs);

        double midErr = 0.0;
        double centroidErr = 0.0;
        if (RASTER > 0.0 && !sigs.empty()) {
            for (const auto& sig : sigs) {
                double n1 = std::round(sig.centerFrequency / RASTER) * RASTER;
                midErr += std::fabs(sig.centerFrequency - n1);
                double n2 = std::round(sig.centroidFrequency / RASTER) * RASTER;
                centroidErr += std::fabs(sig.centroidFrequency - n2);
            }
            midErr /= sigs.size();
            centroidErr /= sigs.size();
        }
        printf("gap=%6.0f kHz -> %2d tracked, bw min %6.1f median %6.1f max %7.1f kHz | "
               "raster err: midpoint %5.1f kHz, centroid %5.1f kHz\n",
               gapHz / 1e3, s.detectionCount, s.narrowest / 1e3, s.medianBandwidth / 1e3,
               s.widest / 1e3, midErr / 1e3, centroidErr / 1e3);
    }

    // Full production pipeline: AutoReceiverManager with the shipped Broadcast FM profile.
    if (CENTER > 0.0) {
        printf("\n--- full pipeline, default Broadcast FM profile ---\n");
        AutoReceiverManager mgr;
        AutoReceiverManager::Config cfg;
        cfg.enabled = true;
        cfg.floorMode = NoiseFloorMode::MANUAL;
        cfg.manualFloorDb = flatFloorDb;
        cfg.marginDb = 10.0f;
        cfg.detectionAveragingFrames = 4;
        cfg.mergeGapHz = 20e3;
        cfg.restrictToProfiles = true;
        cfg.activationMs = 300;
        cfg.releaseMs = 2000;
        mgr.setConfig(cfg);
        mgr.setProfiles(ReceptionProfileSet::defaults());

        uint64_t t = 0;
        for (size_t f = 0; f < frames.size(); f++) {
            mgr.onFFTFrame(frames[f].data(), FFT_SIZE, CENTER, SPAN, 1.0, 20.0, t);
            t += 50;
        }

        auto classified = mgr.getClassifiedSignals();
        std::sort(classified.begin(), classified.end(),
                  [](const AutoReceiverManager::ClassifiedSignal& a,
                     const AutoReceiverManager::ClassifiedSignal& b) {
                      return a.tracked.signal.centroidFrequency < b.tracked.signal.centroidFrequency;
                  });

        printf("%d signals tracked\n", (int)classified.size());
        for (const auto& cs : classified) {
            printf("    tune %10.4f MHz  %-4s bw %6.1f kHz | detected %10.4f MHz w %6.1f kHz "
                   "snr %5.1f dB  %s\n",
                   cs.tuneFrequency / 1e6, toString(cs.demod), cs.receiverBandwidth / 1e3,
                   cs.tracked.signal.centroidFrequency / 1e6, cs.tracked.signal.bandwidth / 1e3,
                   cs.tracked.signal.snrDb, toString(cs.tracked.state));
        }
    }

    // Is the channel assignment stable frame to frame? A station whose centroid sits near a
    // raster boundary would otherwise be tracked as two adjacent channels.
    if (CENTER > 0.0 && RASTER > 0.0) {
        printf("\n--- channel assignment stability (per frame, strongest few channels) ---\n");
        AutoReceiverManager mgr;
        AutoReceiverManager::Config cfg;
        cfg.enabled = true;
        cfg.floorMode = NoiseFloorMode::MANUAL;
        cfg.manualFloorDb = flatFloorDb;
        cfg.marginDb = 10.0f;
        cfg.detectionAveragingFrames = 4;
        cfg.restrictToProfiles = true;
        cfg.activationMs = 0;
        cfg.releaseMs = 2000;
        mgr.setConfig(cfg);
        mgr.setProfiles(ReceptionProfileSet::defaults());

        // channel index -> how many detection frames reported it, and the centroid spread.
        std::vector<std::tuple<int64_t, int, double, double>> seen; // idx, count, minC, maxC

        EventHandler<std::vector<DetectedSignal>> handler(
            [](std::vector<DetectedSignal> sigs, void* ctx) {
                auto* s = (std::vector<std::tuple<int64_t, int, double, double>>*)ctx;
                for (const auto& sig : sigs) {
                    if (sig.channelIndex == 0) { continue; }
                    bool found = false;
                    for (auto& [idx, count, minC, maxC] : *s) {
                        if (idx != sig.channelIndex) { continue; }
                        count++;
                        minC = std::min(minC, sig.centroidFrequency);
                        maxC = std::max(maxC, sig.centroidFrequency);
                        found = true;
                        break;
                    }
                    if (!found) {
                        s->emplace_back(sig.channelIndex, 1, sig.centroidFrequency,
                                        sig.centroidFrequency);
                    }
                }
            },
            &seen);
        mgr.onDetectionUpdate.bindHandler(&handler);

        uint64_t t = 0;
        for (size_t f = 0; f < frames.size(); f++) {
            mgr.onFFTFrame(frames[f].data(), FFT_SIZE, CENTER, SPAN, 1.0, 20.0, t);
            t += 50;
        }
        mgr.onDetectionUpdate.unbindHandler(&handler);

        std::sort(seen.begin(), seen.end(), [](const auto& a, const auto& b) {
            return std::get<0>(a) < std::get<0>(b);
        });
        for (const auto& [idx, count, minC, maxC] : seen) {
            double chan = (double)idx * RASTER;
            printf("    channel %10.4f MHz : %3d frames, centroid %10.4f .. %10.4f "
                   "(spread %6.1f kHz, offset %+7.1f kHz)\n",
                   chan / 1e6, count, minC / 1e6, maxC / 1e6, (maxC - minC) / 1e3,
                   (((minC + maxC) / 2.0) - chan) / 1e3);
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
