// Phase 0 autopsy: why does the current pipeline miss quiet airband transmissions?
//
// Hidden by default ([.]) because it needs recordings that are not in the repo. Run it with:
//
//   SDRPP_TEST_BASEBAND_NOISE=baseband_no_only_noise.wav \
//   SDRPP_TEST_BASEBAND_SIGNAL=baseband_small_transmissions_119_1_mhz.wav \
//   SDRPP_TEST_CENTER=119000000 SDRPP_TEST_EXPECT=119100000 \
//   sdrpp_core_tests "[airdiag]" -s
//
// The question this answers is narrow and specific: does detectSignals() find the quiet
// transmissions and does filterLocked() then throw them away? Everything else waits on the answer.

#include <catch2/catch_test_macros.hpp>

#include <support/baseband_harness.h>

#include <dsp/detector/noise_floor.h>
#include <dsp/detector/signal_tracker.h>
#include <dsp/detector/spectrum_detector.h>
#include <signal_path/auto_receiver.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace dsp::detector;

namespace {

    const int FFT_SIZE = 16384;
    const int MAX_FRAMES = 400;

    double envDouble(const char* name, double fallback) {
        const char* v = std::getenv(name);
        return v ? atof(v) : fallback;
    }

    std::string envString(const char* name, const char* fallback) {
        const char* v = std::getenv(name);
        return v ? v : fallback;
    }

    double median(std::vector<double> v) {
        if (v.empty()) { return 0.0; }
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    }

    /**
     * Per-bin standard deviation of the dB value across frames, averaged over bins.
     *
     * For stationary single-look noise this must land near 5.57 dB. If it does not, the recording
     * is not what it claims to be and every number downstream is suspect.
     */
    double meanPerBinDbStd(const std::vector<std::vector<float>>& frames) {
        if (frames.size() < 2) { return 0.0; }
        int bins = (int)frames[0].size();
        double total = 0.0;
        for (int b = 0; b < bins; b++) {
            double sum = 0.0;
            double sumSq = 0.0;
            for (const auto& f : frames) {
                sum += f[b];
                sumSq += (double)f[b] * f[b];
            }
            double n = (double)frames.size();
            double var = (sumSq / n) - ((sum / n) * (sum / n));
            total += std::sqrt(std::max(var, 0.0));
        }
        return total / bins;
    }

    void printPreamble(const char* label, baseband::BasebandFile& file,
                       const std::vector<std::vector<float>>& frames, double binHz) {
        printf("\n=== %s ===\n", label);
        printf("  sample rate %.0f Hz, %.2f s of signal, FFT %d bins, %.1f Hz/bin\n",
               file.sampleRate, file.duration(), FFT_SIZE, binHz);
        printf("  collected %d frames\n", (int)frames.size());
        if (frames.empty()) { return; }

        float flat = NoiseFloorModel::estimateFlatFloorDb(frames[0].data(), FFT_SIZE, 0.25f);
        printf("  flat floor estimate (25th pct of one frame): %.1f dB\n", flat);
        printf("  mean per-bin dB std across time: %.2f dB (expect ~5.57 for single-look noise)\n",
               meanPerBinDbStd(frames));
    }
}

TEST_CASE("airband detection autopsy", "[.][airdiag]") {
    const std::string noisePath = envString("SDRPP_TEST_BASEBAND_NOISE", "baseband_no_only_noise.wav");
    const std::string signalPath =
        envString("SDRPP_TEST_BASEBAND_SIGNAL", "baseband_small_transmissions_119_1_mhz.wav");
    const double CENTER = envDouble("SDRPP_TEST_CENTER", 119.0e6);
    const double EXPECT = envDouble("SDRPP_TEST_EXPECT", 119.1e6);

    baseband::BasebandFile noiseFile;
    baseband::BasebandFile signalFile;
    bool haveNoise = noiseFile.open(noisePath);
    bool haveSignal = signalFile.open(signalPath);

    if (!haveSignal) {
        WARN("Could not open signal recording: " + signalPath);
        return;
    }

    const double SPAN = signalFile.sampleRate;
    const double BIN_HZ = SPAN / FFT_SIZE;

    std::vector<std::vector<float>> noiseFrames;
    if (haveNoise) {
        noiseFrames = baseband::collectFrames(noiseFile, FFT_SIZE, MAX_FRAMES);
        printPreamble(noisePath.c_str(), noiseFile, noiseFrames, SPAN / FFT_SIZE);
    }
    else {
        WARN("Could not open noise recording: " + noisePath);
    }

    auto signalFrames = baseband::collectFrames(signalFile, FFT_SIZE, MAX_FRAMES);
    printPreamble(signalPath.c_str(), signalFile, signalFrames, BIN_HZ);
    REQUIRE(signalFrames.size() > 40);

    printf("  centre %.4f MHz, expecting traffic at %.4f MHz (bin %d of %d)\n", CENTER / 1e6,
           EXPECT / 1e6, baseband::binFor(EXPECT, CENTER, SPAN, FFT_SIZE), FFT_SIZE);

    // The floor the user would have: a flat level taken from the spectrum.
    const float flatFloorDb =
        NoiseFloorModel::estimateFlatFloorDb(signalFrames[0].data(), FFT_SIZE, 0.25f);

    // ---------------------------------------------------------------------------------------
    // The autopsy proper. Run detectSignals() exactly as onFFTFrame() does, then apply the
    // shipped Airband profile's limits by hand so we can say which test killed each run.
    // ---------------------------------------------------------------------------------------
    const ReceptionProfileSet defaults = ReceptionProfileSet::defaults();
    const ReceptionProfile* airband = defaults.findFor(EXPECT);
    REQUIRE(airband != nullptr);
    printf("\n--- shipped profile: %s, rx bw %.1f kHz, detect %.1f-%.1f kHz, step %.4f kHz ---\n",
           airband->name.c_str(), airband->bandwidth / 1e3, airband->minDetectionBandwidth / 1e3,
           airband->maxDetectionBandwidth / 1e3, airband->frequencyStep / 1e3);

    for (int avgFrames : { 1, 4 }) {
        NoiseFloorModel floor;
        floor.configure(FFT_SIZE, BIN_HZ);
        floor.setMode(NoiseFloorMode::MANUAL);
        floor.setManualFloorDb(flatFloorDb);
        floor.setMarginDb(10.0f);

        DetectionParams p;
        p.minBins = 2;
        // Global knobs as the detector sees them before profile limits are applied.
        p.mergeGapHz = 20e3;

        int framesWithRun = 0;
        int framesSurviving = 0;
        int runsNearTarget = 0;
        int killedByMin = 0;
        int killedByMax = 0;
        std::vector<double> occupiedBw;
        std::vector<double> peakSnr;
        std::vector<double> offsets;
        int frameCount = 0;

        for (int f = 0; f + avgFrames <= (int)signalFrames.size(); f += avgFrames) {
            std::vector<float> avg(FFT_SIZE, 0.0f);
            for (int k = 0; k < avgFrames; k++) {
                for (int b = 0; b < FFT_SIZE; b++) { avg[b] += signalFrames[f + k][b]; }
            }
            for (int b = 0; b < FFT_SIZE; b++) { avg[b] /= (float)avgFrames; }
            frameCount++;

            auto signals = detectSignals(avg.data(), FFT_SIZE, floor, CENTER, SPAN, p);

            bool anyNear = false;
            bool anySurvived = false;
            for (const auto& sig : signals) {
                // Containment, not centroid proximity: with a wide merge gap the carrier ends up
                // inside a blob whose centroid is somewhere else entirely, and judging by centroid
                // would report "not detected" for a run that does cover the transmission.
                if (EXPECT < sig.lowerFrequency || EXPECT > sig.upperFrequency) { continue; }
                anyNear = true;
                runsNearTarget++;
                occupiedBw.push_back(sig.bandwidth);
                peakSnr.push_back(sig.snrDb);
                offsets.push_back(sig.centroidFrequency - EXPECT);

                bool killMin = airband->minDetectionBandwidth > 0.0 &&
                               sig.bandwidth < airband->minDetectionBandwidth;
                bool killMax = airband->maxDetectionBandwidth > 0.0 &&
                               sig.bandwidth > airband->maxDetectionBandwidth;
                if (killMin) { killedByMin++; }
                if (killMax) { killedByMax++; }
                if (!killMin && !killMax) { anySurvived = true; }
            }
            if (anyNear) { framesWithRun++; }
            if (anySurvived) { framesSurviving++; }
        }

        printf("\n  avg=%d frames (%d detection frames)\n", avgFrames, frameCount);
        printf("    frames with a run near %.3f MHz : %d (%.0f%%)\n", EXPECT / 1e6, framesWithRun,
               frameCount ? 100.0 * framesWithRun / frameCount : 0.0);
        printf("    ...of which survive the profile  : %d (%.0f%%)\n", framesSurviving,
               frameCount ? 100.0 * framesSurviving / frameCount : 0.0);
        printf("    runs near target                 : %d\n", runsNearTarget);
        printf("    killed by minDetectionBandwidth  : %d\n", killedByMin);
        printf("    killed by maxDetectionBandwidth  : %d\n", killedByMax);
        if (!occupiedBw.empty()) {
            auto sortedBw = occupiedBw;
            std::sort(sortedBw.begin(), sortedBw.end());
            printf("    occupied bw   min %.2f  median %.2f  p90 %.2f  max %.2f kHz\n",
                   sortedBw.front() / 1e3, median(occupiedBw) / 1e3,
                   sortedBw[(size_t)(sortedBw.size() * 0.9)] / 1e3, sortedBw.back() / 1e3);
            printf("    peak snr      median %.1f dB\n", median(peakSnr));
            printf("    centroid offset from %.3f MHz: median %+.2f kHz\n", EXPECT / 1e6,
                   median(offsets) / 1e3);
        }
        printf("    KEY=airdiag avg=%d frames=%d near=%d survived=%d killedMin=%d medianBwHz=%.0f\n",
               avgFrames, frameCount, framesWithRun, framesSurviving, killedByMin,
               occupiedBw.empty() ? 0.0 : median(occupiedBw));
    }

    // ---------------------------------------------------------------------------------------
    // Merge gap sweep. The shipped global default is 20 kHz, a broadcast FM number; at 122 Hz/bin
    // that is 164 bins of bridging, which on airband welds the whole capture into one run.
    // ---------------------------------------------------------------------------------------
    {
        printf("\n--- merge gap sweep at %.3f MHz (flat floor, margin 10 dB) ---\n", EXPECT / 1e6);
        printf("    %-4s %-8s %8s %10s %10s %10s\n", "avg", "gap", "hit%", "medianBw", "survive%",
               "detPerFrm");
        for (int avgFrames : { 1, 4 }) {
            for (double gapHz : { 0.0, 500.0, 1e3, 2e3, 4e3, 8e3, 20e3 }) {
                NoiseFloorModel floor;
                floor.configure(FFT_SIZE, BIN_HZ);
                floor.setMode(NoiseFloorMode::MANUAL);
                floor.setManualFloorDb(flatFloorDb);
                floor.setMarginDb(10.0f);

                DetectionParams p;
                p.minBins = 2;
                p.mergeGapHz = gapHz;

                int hit = 0;
                int survived = 0;
                int frameCount = 0;
                int totalDet = 0;
                std::vector<double> bws;
                for (int f = 0; f + avgFrames <= (int)signalFrames.size(); f += avgFrames) {
                    std::vector<float> avg(FFT_SIZE, 0.0f);
                    for (int k = 0; k < avgFrames; k++) {
                        for (int b = 0; b < FFT_SIZE; b++) { avg[b] += signalFrames[f + k][b]; }
                    }
                    for (int b = 0; b < FFT_SIZE; b++) { avg[b] /= (float)avgFrames; }
                    frameCount++;

                    auto sigs = detectSignals(avg.data(), FFT_SIZE, floor, CENTER, SPAN, p);
                    totalDet += (int)sigs.size();
                    for (const auto& s : sigs) {
                        if (EXPECT < s.lowerFrequency || EXPECT > s.upperFrequency) { continue; }
                        hit++;
                        bws.push_back(s.bandwidth);
                        bool ok = !(airband->minDetectionBandwidth > 0.0 &&
                                    s.bandwidth < airband->minDetectionBandwidth) &&
                                  !(airband->maxDetectionBandwidth > 0.0 &&
                                    s.bandwidth > airband->maxDetectionBandwidth);
                        if (ok) { survived++; }
                        break;
                    }
                }
                printf("    %-4d %6.1fk %7.0f%% %9.1fk %9.0f%% %10.1f\n", avgFrames, gapHz / 1e3,
                       frameCount ? 100.0 * hit / frameCount : 0.0, median(bws) / 1e3,
                       frameCount ? 100.0 * survived / frameCount : 0.0,
                       frameCount ? (double)totalDet / frameCount : 0.0);
            }
        }
    }

    // ---------------------------------------------------------------------------------------
    // What is actually there to detect? Integrate linear excess power over the channel and
    // compare against a narrow kernel on the carrier. This decides the kernel set: if the narrow
    // kernel wins, a single channel-width kernel would be the wrong detector.
    // ---------------------------------------------------------------------------------------
    {
        printf("\n--- what is at %.3f MHz: channel energy vs carrier peak, per frame ---\n",
               EXPECT / 1e6);

        // Local noise power per bin, measured from the noise-only file where possible so it is
        // uncontaminated by the traffic itself.
        const auto& refFrames = noiseFrames.empty() ? signalFrames : noiseFrames;
        std::vector<double> noiseLin(FFT_SIZE, 0.0);
        for (const auto& f : refFrames) {
            for (int b = 0; b < FFT_SIZE; b++) { noiseLin[b] += std::pow(10.0, f[b] / 10.0); }
        }
        for (int b = 0; b < FFT_SIZE; b++) { noiseLin[b] /= (double)refFrames.size(); }

        const int center = baseband::binFor(EXPECT, CENTER, SPAN, FFT_SIZE);
        auto kernelBins = [&](double bwHz) {
            return std::max<int>(1, (int)std::lround(bwHz / BIN_HZ));
        };
        // Widths spanning carrier-only up to the full 10 kHz airband channel.
        const double widths[] = { 4 * BIN_HZ, 1e3, 3e3, 10e3 };

        int activeFrames = 0;
        std::vector<double> bestChannelSnr;
        int narrowWins = 0;
        int wideWins = 0;

        printf("    %-6s", "frame");
        for (double w : widths) { printf(" %8.2fkHz", w / 1e3); }
        printf("   winner\n");

        for (size_t f = 0; f < signalFrames.size(); f++) {
            double snr[4] = { 0, 0, 0, 0 };
            int bestIdx = 0;
            for (int k = 0; k < 4; k++) {
                int half = kernelBins(widths[k]) / 2;
                double sig = 0.0;
                double nse = 0.0;
                for (int b = center - half; b <= center + half; b++) {
                    if (b < 0 || b >= FFT_SIZE) { continue; }
                    sig += std::pow(10.0, signalFrames[f][b] / 10.0);
                    nse += noiseLin[b];
                }
                snr[k] = 10.0 * std::log10(std::max(sig, 1e-30) / std::max(nse, 1e-30));
                if (snr[k] > snr[bestIdx]) { bestIdx = k; }
            }
            // "Active" = the channel is clearly lifted above the noise by any measure.
            if (snr[bestIdx] > 3.0) {
                activeFrames++;
                bestChannelSnr.push_back(snr[bestIdx]);
                if (bestIdx <= 1) { narrowWins++; }
                else { wideWins++; }
                if (activeFrames <= 25) {
                    printf("    %-6d", (int)f);
                    for (int k = 0; k < 4; k++) { printf(" %8.1f   ", snr[k]); }
                    printf("   %.2f kHz\n", widths[bestIdx] / 1e3);
                }
            }
        }
        printf("    frames with channel lifted >3 dB: %d of %d\n", activeFrames,
               (int)signalFrames.size());
        if (!bestChannelSnr.empty()) {
            auto s = bestChannelSnr;
            std::sort(s.begin(), s.end());
            printf("    best-kernel channel SNR: min %.1f  median %.1f  p90 %.1f  max %.1f dB\n",
                   s.front(), median(bestChannelSnr), s[(size_t)(s.size() * 0.9)], s.back());
            printf("    narrow kernel (<=1 kHz) won %d frames, wide (>=3 kHz) won %d\n", narrowWins,
                   wideWins);
        }
    }

    // ---------------------------------------------------------------------------------------
    // End to end, through the real manager, which is what the user actually sees.
    // ---------------------------------------------------------------------------------------
    for (int avgFrames : { 1, 4 }) {
        AutoReceiverManager mgr;
        AutoReceiverManager::Config cfg;
        cfg.enabled = true;
        cfg.floorMode = NoiseFloorMode::MANUAL;
        cfg.manualFloorDb = flatFloorDb;
        cfg.marginDb = 10.0f;
        cfg.detectionAveragingFrames = avgFrames;
        cfg.mergeGapHz = 20e3;
        cfg.restrictToProfiles = true;
        cfg.activationMs = 300;
        cfg.releaseMs = 2000;
        mgr.setConfig(cfg);
        mgr.setProfiles(defaults);

        uint64_t t = 0;
        int everActive = 0;
        for (size_t f = 0; f < signalFrames.size(); f++) {
            mgr.onFFTFrame(signalFrames[f].data(), FFT_SIZE, CENTER, SPAN, 1.0, 20.0, t);
            for (const auto& cs : mgr.getClassifiedSignals()) {
                if (cs.tracked.state == SignalState::ACTIVE &&
                    std::fabs(cs.tracked.signal.centroidFrequency - EXPECT) < 12.5e3) {
                    everActive++;
                    break;
                }
            }
            t += 50;
        }

        auto classified = mgr.getClassifiedSignals();
        printf("\n--- full pipeline, avg=%d: %d signals tracked, %d frames ACTIVE at target ---\n",
               avgFrames, (int)classified.size(), everActive);
        std::sort(classified.begin(), classified.end(),
                  [](const AutoReceiverManager::ClassifiedSignal& a,
                     const AutoReceiverManager::ClassifiedSignal& b) {
                      return a.tracked.signal.centroidFrequency < b.tracked.signal.centroidFrequency;
                  });
        for (const auto& cs : classified) {
            printf("    tune %10.4f MHz  %-4s | detected %10.4f MHz w %7.2f kHz snr %5.1f dB  %s\n",
                   cs.tuneFrequency / 1e6, toString(cs.demod),
                   cs.tracked.signal.centroidFrequency / 1e6, cs.tracked.signal.bandwidth / 1e3,
                   cs.tracked.signal.snrDb, toString(cs.tracked.state));
        }
    }

    // ---------------------------------------------------------------------------------------
    // Operating point. False alarms come from the noise-only recording and detections from the
    // signal one, both through the real manager, so a margin can be chosen from measurements
    // rather than guessed. A flat floor and a measured one are compared because a flat level
    // cannot follow the analog filter shape across 2 MHz.
    // ---------------------------------------------------------------------------------------
    if (!noiseFrames.empty()) {
        printf("\n--- operating point: false ACTIVE tracks/min vs detection at %.3f MHz ---\n",
               EXPECT / 1e6);
        printf("    %-9s %-4s %-7s | %14s | %14s %12s\n", "floor", "avg", "margin",
               "false trk/min", "active frames", "tracks");

        for (bool measured : { false, true }) {
            for (int avgFrames : { 1, 4 }) {
                for (float marginDb : { 6.0f, 10.0f, 14.0f, 18.0f }) {
                    auto makeConfig = [&]() {
                        AutoReceiverManager::Config cfg;
                        cfg.enabled = true;
                        cfg.floorMode = measured ? NoiseFloorMode::MEASURED
                                                 : NoiseFloorMode::MANUAL;
                        cfg.manualFloorDb = flatFloorDb;
                        cfg.marginDb = marginDb;
                        cfg.detectionAveragingFrames = avgFrames;
                        cfg.restrictToProfiles = true;
                        cfg.activationMs = 300;
                        cfg.releaseMs = 2000;
                        return cfg;
                    };

                    // Noise-only: how often does a track reach ACTIVE when nothing is on air?
                    AutoReceiverManager noiseMgr;
                    noiseMgr.setConfig(makeConfig());
                    noiseMgr.setProfiles(defaults);
                    uint64_t t = 0;
                    if (measured) {
                        noiseMgr.onFFTFrame(noiseFrames[0].data(), FFT_SIZE, CENTER, SPAN, 1.0,
                                            20.0, t);
                        noiseMgr.startMeasurement();
                    }
                    std::vector<uint64_t> falseIds;
                    for (size_t f = 0; f < noiseFrames.size(); f++) {
                        noiseMgr.onFFTFrame(noiseFrames[f].data(), FFT_SIZE, CENTER, SPAN, 1.0,
                                            20.0, t);
                        for (const auto& tr : noiseMgr.getTrackedSignals()) {
                            if (tr.state != SignalState::ACTIVE) { continue; }
                            if (std::find(falseIds.begin(), falseIds.end(), tr.signal.id) ==
                                falseIds.end()) {
                                falseIds.push_back(tr.signal.id);
                            }
                        }
                        t += 50;
                    }
                    double noiseMinutes = (double)noiseFrames.size() / 20.0 / 60.0;

                    // Signal file: frames where the target channel is ACTIVE.
                    AutoReceiverManager sigMgr;
                    sigMgr.setConfig(makeConfig());
                    sigMgr.setProfiles(defaults);
                    t = 0;
                    if (measured) {
                        sigMgr.onFFTFrame(signalFrames[0].data(), FFT_SIZE, CENTER, SPAN, 1.0,
                                          20.0, t);
                        sigMgr.startMeasurement();
                    }
                    int activeFrames = 0;
                    for (size_t f = 0; f < signalFrames.size(); f++) {
                        sigMgr.onFFTFrame(signalFrames[f].data(), FFT_SIZE, CENTER, SPAN, 1.0,
                                          20.0, t);
                        for (const auto& tr : sigMgr.getTrackedSignals()) {
                            if (tr.state == SignalState::ACTIVE &&
                                std::fabs(tr.signal.centerFrequency - EXPECT) < 12.5e3) {
                                activeFrames++;
                                break;
                            }
                        }
                        t += 50;
                    }

                    printf("    %-9s %-4d %5.1fdB | %14.1f | %8d/%-5d %12d\n",
                           measured ? "measured" : "flat", avgFrames, marginDb,
                           noiseMinutes > 0.0 ? falseIds.size() / noiseMinutes : 0.0, activeFrames,
                           (int)signalFrames.size(), (int)sigMgr.getTrackedSignals().size());
                }
            }
        }
    }
    printf("\n");
}
