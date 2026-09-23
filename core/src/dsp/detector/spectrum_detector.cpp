#include "spectrum_detector.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dsp::detector {

    const char* toString(SignalState state) {
        switch (state) {
        case SignalState::CANDIDATE: return "Candidate";
        case SignalState::ACTIVE: return "Active";
        case SignalState::RELEASING: return "Releasing";
        case SignalState::ENDED: return "Ended";
        }
        return "Unknown";
    }

    std::vector<DetectedSignal> detectSignals(const float* fft, int count,
                                              const NoiseFloorModel& floor,
                                              double centerFrequency, double spanHz,
                                              const DetectionParams& params) {
        std::vector<DetectedSignal> signals;

        if (fft == nullptr || count <= 0 || spanHz <= 0.0) { return signals; }
        if (!floor.isUsable()) { return signals; }
        // A measured floor is per-bin, so it only applies to a frame of the same width. A manual
        // floor is flat and applies to any frame.
        if (floor.getMode() == NoiseFloorMode::MEASURED && floor.getBinCount() != count) {
            return signals;
        }

        // Restrict to the usable, centered part of the capture.
        double ratio = std::clamp(params.usableSpectrumRatio, 0.0, 1.0);
        int usableBins = (int)std::floor(count * ratio);
        if (usableBins < 1) { return signals; }
        int loBin = (count - usableBins) / 2;
        int hiBin = loBin + usableBins; // exclusive

        double binWidth = spanHz / (double)count;
        double specLow = centerFrequency - (spanHz / 2.0);

        int minBins = std::max<int>(params.minBins, 1);
        if (params.minBandwidthHz > 0.0) {
            minBins = std::max<int>(minBins, (int)std::floor(params.minBandwidthHz / binWidth));
        }
        int maxGap = 0;
        if (params.mergeGapHz > 0.0) {
            maxGap = (int)std::lround(params.mergeGapHz / binWidth);
        }
        int maxBins = std::numeric_limits<int>::max();
        if (params.maxBandwidthHz > 0.0) {
            maxBins = std::max<int>(1, (int)std::ceil(params.maxBandwidthHz / binWidth));
        }

        int runStart = -1;  // first bin of the run being built
        int runEnd = -1;    // last above-threshold bin of the run being built
        int gap = 0;

        auto emit = [&](int start, int end) {
            if (start < 0 || end < start) { return; }
            int width = end - start + 1;
            if (width < minBins || width > maxBins) { return; }

            float peak = -std::numeric_limits<float>::infinity();
            double floorSum = 0.0;
            // Power-weighted centroid, using each bin's excess over its own floor in linear
            // power. Working in linear power rather than dB keeps a strong carrier from being
            // outvoted by a wide spread of barely-above-threshold bins.
            double weightSum = 0.0;
            double weightedFreqSum = 0.0;

            for (int i = start; i <= end; i++) {
                peak = std::max<float>(peak, fft[i]);
                double binFloor = floor.getFloorDb(i);
                floorSum += binFloor;

                double excessDb = fft[i] - binFloor;
                if (excessDb > 0.0) {
                    double weight = std::pow(10.0, excessDb / 10.0) - 1.0;
                    double binCenter = specLow + ((i + 0.5) * binWidth);
                    weightSum += weight;
                    weightedFreqSum += weight * binCenter;
                }
            }
            float noiseFloor = (float)(floorSum / (double)width);

            DetectedSignal sig;
            sig.lowerFrequency = specLow + (start * binWidth);
            sig.upperFrequency = specLow + ((end + 1) * binWidth);
            sig.centerFrequency = (sig.lowerFrequency + sig.upperFrequency) / 2.0;
            sig.bandwidth = sig.upperFrequency - sig.lowerFrequency;
            sig.centroidFrequency =
                (weightSum > 0.0) ? (weightedFreqSum / weightSum) : sig.centerFrequency;
            sig.peakDb = peak;
            sig.noiseFloorDb = noiseFloor;
            sig.snrDb = peak - noiseFloor;
            signals.push_back(sig);
        };

        for (int i = loBin; i < hiBin; i++) {
            bool above = fft[i] > floor.getThresholdDb(i);
            if (above) {
                if (runStart < 0) { runStart = i; }
                runEnd = i;
                gap = 0;
            }
            else if (runStart >= 0) {
                gap++;
                if (gap > maxGap) {
                    emit(runStart, runEnd);
                    runStart = -1;
                    runEnd = -1;
                    gap = 0;
                }
            }
        }
        emit(runStart, runEnd);

        return signals;
    }

    // ------------------------------------------------------------------------------------------
    // Integrated detection.
    // ------------------------------------------------------------------------------------------

    double cfarRatio(double looks, double falseAlarmRate) {
        double n = std::max(looks, 0.5);
        double pfa = std::clamp(falseAlarmRate, 1e-15, 0.5);

        // Integrated noise power over n independent looks is Gamma(n, 1/n). Wilson-Hilferty maps
        // that onto a normal, which is accurate to well under a dB for n >= 1 and errs high (i.e.
        // conservative) for the very small n that a four-bin kernel gives.
        double z = -std::log(pfa);
        // Normal quantile of 1 - pfa, via the same tail approximation used for the floor bias.
        double t = std::sqrt(2.0 * z);
        z = t - (((0.010328 * t + 0.802853) * t + 2.515517) /
                 (((0.001308 * t + 0.189269) * t + 1.432788) * t + 1.0));

        double a = 1.0 / (9.0 * n);
        double root = 1.0 - a + (z * std::sqrt(a));
        return std::max(1.0, root * root * root);
    }

    std::vector<int> kernelSetFor(double bandwidthHz, double binWidthHz, int maxBins) {
        std::vector<int> out;
        if (binWidthHz <= 0.0 || maxBins < 1) { return out; }

        const double fractions[] = { 1.0 / 32.0, 1.0 / 8.0, 1.0 / 2.0, 1.0 };
        for (double f : fractions) {
            int k = (int)std::lround((bandwidthHz * f) / binWidthHz);
            k = std::clamp(k, 4, maxBins);
            if (out.empty() || out.back() != k) { out.push_back(k); }
        }
        return out;
    }

    namespace {

        // Everything a kernel sweep needs to say "this bin is carrying a signal".
        struct KernelTest {
            int halfWidth = 0;
            double threshold = 1.0; // linear power ratio
        };

        std::vector<KernelTest> buildTests(const PassParams& params) {
            std::vector<KernelTest> tests;
            double marginRatio = std::pow(10.0, (double)params.marginDb / 10.0);

            for (int k : params.kernelBins) {
                if (k < 1) { continue; }
                KernelTest t;
                t.halfWidth = k / 2;
                // The user's margin is the sensitivity they asked for; CFAR only ever raises the
                // bar, so a low margin cannot make a wide kernel fire on noise.
                double looks = std::max(1.0, (double)k * params.looksPerBin);
                t.threshold = std::max(marginRatio, cfarRatio(looks, params.falseAlarmRate));
                tests.push_back(t);
            }
            return tests;
        }

        /**
         * Best "how far over threshold" across all kernels centred on `bin`, as a ratio. Above 1
         * means detected. Kernels that would run off the end of the pass are skipped rather than
         * truncated: a short kernel has a wider noise distribution and produces edge false alarms.
         */
        double bestOverThreshold(const SpectrumIntegrator& integ,
                                 const std::vector<KernelTest>& tests, int bin, int loBin,
                                 int hiBin, int* winnerOut = nullptr) {
            double best = 0.0;
            for (size_t i = 0; i < tests.size(); i++) {
                int half = tests[i].halfWidth;
                int lo = bin - half;
                int hi = bin + half;
                if (lo < loBin || hi >= hiBin) { continue; }

                double ratio = integ.snrRatio(lo, hi) / tests[i].threshold;
                if (ratio > best) {
                    best = ratio;
                    if (winnerOut != nullptr) { *winnerOut = (int)i; }
                }
            }
            return best;
        }

        /**
         * Integrated SNR of the kernel that decided the detection, in dB.
         *
         * This, and not the SNR over the reported extent, is what the threshold compared against,
         * so a detection always reports at least the configured margin. Measuring over the full
         * extent instead would read several dB lower for a narrow carrier in a wide channel --
         * every noise bin either side dilutes it -- and the user would see detections sitting
         * below a margin that supposedly admitted them.
         */
        float winningSnrDb(const SpectrumIntegrator& integ, const std::vector<KernelTest>& tests,
                           int bin, int loBin, int hiBin) {
            int winner = -1;
            if (bestOverThreshold(integ, tests, bin, loBin, hiBin, &winner) <= 0.0 || winner < 0) {
                return 0.0f;
            }
            int half = tests[winner].halfWidth;
            return (float)integ.snrDb(bin - half, bin + half);
        }

        // Strongest-scoring bin in a range, which is the one whose kernel decided the detection.
        int strongestBin(const SpectrumIntegrator& integ, const std::vector<KernelTest>& tests,
                         int lo, int hi, int loBin, int hiBin) {
            int best = lo;
            double bestScore = -1.0;
            for (int b = lo; b <= hi; b++) {
                double s = bestOverThreshold(integ, tests, b, loBin, hiBin);
                if (s > bestScore) {
                    bestScore = s;
                    best = b;
                }
            }
            return best;
        }

        /**
         * Shortest contiguous window inside [lo, hi] holding `fraction` of the excess power --
         * the occupied bandwidth, in the ITU sense.
         *
         * Not a symmetric expansion around the peak: a signal whose energy sits off to one side
         * of its strongest bin would then be reported far wider than it is, which both overstates
         * the bandwidth and dilutes the integrated SNR with noise-only bins.
         */
        void occupiedWindow(const SpectrumIntegrator& integ, int lo, int hi, double fraction,
                            int& outLo, int& outHi) {
            outLo = lo;
            outHi = hi;

            double total = integ.excessPower(lo, hi);
            if (total <= 0.0) { return; }
            double target = total * fraction;

            int bestLo = lo;
            int bestHi = hi;
            int bestWidth = hi - lo + 1;

            // Two pointers: grow the right edge until the window is heavy enough, then pull the
            // left edge in as far as it will go.
            int left = lo;
            for (int right = lo; right <= hi; right++) {
                while (left <= right && integ.excessPower(left + 1, right) >= target) { left++; }
                if (integ.excessPower(left, right) >= target) {
                    int width = right - left + 1;
                    if (width < bestWidth) {
                        bestWidth = width;
                        bestLo = left;
                        bestHi = right;
                    }
                }
            }
            outLo = bestLo;
            outHi = bestHi;
        }

        // ITU convention: the band holding 99% of the emission's power.
        constexpr double OCCUPIED_FRACTION = 0.99;

        void fillGeometry(DetectedSignal& sig, const SpectrumIntegrator& integ, int lo, int hi,
                          double specLow, double binWidth) {
            sig.lowerFrequency = specLow + (lo * binWidth);
            sig.upperFrequency = specLow + ((hi + 1) * binWidth);
            sig.centerFrequency = (sig.lowerFrequency + sig.upperFrequency) / 2.0;
            sig.bandwidth = sig.upperFrequency - sig.lowerFrequency;

            // Power-weighted centre, in linear excess power.
            double weightSum = 0.0;
            double weightedFreqSum = 0.0;
            double peakExcess = 0.0;
            int peakBin = lo;
            for (int b = lo; b <= hi; b++) {
                double w = integ.binExcess(b);
                if (w <= 0.0) { continue; }
                weightSum += w;
                weightedFreqSum += w * (specLow + ((b + 0.5) * binWidth));
                if (w > peakExcess) {
                    peakExcess = w;
                    peakBin = b;
                }
            }
            sig.centroidFrequency =
                (weightSum > 0.0) ? (weightedFreqSum / weightSum) : sig.centerFrequency;

            double noise = integ.noisePower(lo, hi);
            int width = hi - lo + 1;
            sig.noiseFloorDb =
                (float)(10.0 * std::log10(std::max(noise / std::max(width, 1), 1e-30)));
            sig.peakDb = (float)(10.0 * std::log10(std::max(
                peakExcess + (noise / std::max(width, 1)), 1e-30)));
            sig.snrDb = (float)integ.snrDb(lo, hi);
            sig.occupiedBandwidth = sig.bandwidth;
        }

        std::vector<DetectedSignal> detectChannelized(const SpectrumIntegrator& integ,
                                                      const std::vector<KernelTest>& tests,
                                                      double specLow, double binWidth,
                                                      const PassParams& params) {
            std::vector<DetectedSignal> out;

            const double step = params.channelStepHz;
            const double chanBw =
                (params.channelBandwidthHz > 0.0) ? params.channelBandwidthHz : step;
            const int halfChanBins = std::max<int>(1, (int)std::lround((chanBw / 2.0) / binWidth));

            // Score every bin once, then pick peaks and snap those to the raster. Evaluating each
            // channel independently does not work: a kernel centred just outside a channel still
            // reaches the signal inside it, so neighbouring channels each find a "peak" that
            // trivially snaps back to themselves and one transmission is reported several times.
            const int width = params.hiBin - params.loBin;
            std::vector<double> score((size_t)std::max(width, 0), 0.0);
            for (int b = params.loBin; b < params.hiBin; b++) {
                score[b - params.loBin] =
                    bestOverThreshold(integ, tests, b, params.loBin, params.hiBin);
            }

            // Suppress within half a channel spacing. Wider would merge two genuinely adjacent
            // occupied channels into one; narrower would let the skirts of one signal register.
            const int suppressRadius =
                std::max<int>(1, (int)std::lround((step / 2.0) / binWidth));

            // Strongest surviving peak per channel.
            std::vector<std::pair<int64_t, int>> channelPeak; // channel index -> bin

            for (int b = params.loBin; b < params.hiBin; b++) {
                double s = score[b - params.loBin];
                if (s <= 1.0) { continue; }

                bool isLocalMax = true;
                int lo = std::max<int>(params.loBin, b - suppressRadius);
                int hi = std::min<int>(params.hiBin - 1, b + suppressRadius);
                for (int o = lo; o <= hi && isLocalMax; o++) {
                    if (o == b) { continue; }
                    double so = score[o - params.loBin];
                    // Ties go to the lower bin, so a flat top yields exactly one peak.
                    if (so > s || (so == s && o < b)) { isLocalMax = false; }
                }
                if (!isLocalMax) { continue; }

                double peakFreq = specLow + ((b + 0.5) * binWidth);
                int64_t idx = (int64_t)std::llround(peakFreq / step);

                bool merged = false;
                for (auto& [existing, bin] : channelPeak) {
                    if (existing != idx) { continue; }
                    if (s > score[bin - params.loBin]) { bin = b; }
                    merged = true;
                    break;
                }
                if (!merged) { channelPeak.emplace_back(idx, b); }
            }

            // A signal wider than the suppression radius can carry two local maxima far enough
            // apart to survive it and land on different channels -- a 150 kHz FM station is 123
            // bins across, so it would be reported twice, once per adjacent raster slot.
            //
            // What separates "one wide station" from "two adjacent stations" is not distance but
            // connectivity: one emission is a continuous above-threshold span, two are divided by
            // a dip. Keep the stronger of any two peaks that nothing below threshold separates.
            {
                std::stable_sort(channelPeak.begin(), channelPeak.end(),
                                 [&](const auto& a, const auto& b) {
                                     return score[a.second - params.loBin] >
                                            score[b.second - params.loBin];
                                 });

                auto connected = [&](int a, int b) {
                    int lo = std::min(a, b);
                    int hi = std::max(a, b);
                    for (int i = lo; i <= hi; i++) {
                        if (score[i - params.loBin] <= 1.0) { return false; }
                    }
                    return true;
                };

                std::vector<std::pair<int64_t, int>> accepted;
                for (const auto& cand : channelPeak) {
                    bool absorbed = false;
                    for (const auto& kept : accepted) {
                        if (connected(cand.second, kept.second)) {
                            absorbed = true;
                            break;
                        }
                    }
                    if (!absorbed) { accepted.push_back(cand); }
                }
                channelPeak.swap(accepted);
            }

            // Assign each emission to a channel by the power-weighted centroid of its whole
            // above-threshold region, not by its strongest bin.
            //
            // The peak of a modulated carrier wanders tens of kHz between frames. When a station
            // sits near a channel boundary that makes the peak snap to one slot in one frame and
            // the next slot in the next, so one station is tracked as two and neither is stable.
            // The centroid of the whole emission barely moves.
            std::vector<std::pair<int64_t, int>> byChannel;
            for (const auto& [peakIdx, peakBin] : channelPeak) {
                // Bounded by the channel width. Left to run, the above-threshold region of a
                // strong station merges into its neighbours, and the centroid then drifts with
                // whatever those neighbours are doing -- which is exactly the drift that makes a
                // station flip between raster slots.
                int regLoLimit = std::max<int>(params.loBin, peakBin - halfChanBins);
                int regHiLimit = std::min<int>(params.hiBin - 1, peakBin + halfChanBins);

                int regLo = peakBin;
                int regHi = peakBin;
                while (regLo > regLoLimit && score[regLo - 1 - params.loBin] > 1.0) { regLo--; }
                while (regHi < regHiLimit && score[regHi + 1 - params.loBin] > 1.0) { regHi++; }

                double weightSum = 0.0;
                double weightedFreqSum = 0.0;
                for (int b = regLo; b <= regHi; b++) {
                    double w = integ.binExcess(b);
                    if (w <= 0.0) { continue; }
                    weightSum += w;
                    weightedFreqSum += w * (specLow + ((b + 0.5) * binWidth));
                }
                double centroid = (weightSum > 0.0) ? (weightedFreqSum / weightSum)
                                                    : (specLow + ((peakBin + 0.5) * binWidth));
                int64_t idx = (int64_t)std::llround(centroid / step);

                bool dup = false;
                for (auto& [existing, bin] : byChannel) {
                    if (existing != idx) { continue; }
                    if (score[peakBin - params.loBin] > score[bin - params.loBin]) { bin = peakBin; }
                    dup = true;
                    break;
                }
                if (!dup) { byChannel.emplace_back(idx, peakBin); }
            }
            channelPeak.swap(byChannel);

            for (const auto& [idx, peakBin] : channelPeak) {
                double channelFreq = (double)idx * step;

                // Measure over the channel slot itself, not over a window centred on the peak.
                // The peak only decides which channel owns the energy; the slot is fixed by the
                // raster, and it is what the receiver will be tuned to. Centring on the peak
                // would push part of a signal that sits off-centre into the guard band, where the
                // spill test would then read it as a wideband intruder.
                int channelBin = (int)std::lround(((channelFreq - specLow) / binWidth) - 0.5);
                int chanLo = channelBin - halfChanBins;
                int chanHi = channelBin + halfChanBins;

                // A channel only half inside the capture cannot be measured, and a receiver could
                // not be placed on it either.
                if (chanLo < params.loBin || chanHi >= params.hiBin) { continue; }

                // Wideband intruder: a guard ring as hot per bin as the channel means this is not
                // a channel at all.
                if (params.guardRatio > 1.0) {
                    int guardHalf = (int)std::lround(halfChanBins * params.guardRatio);
                    double inPower = integ.excessPower(chanLo, chanHi);
                    double ringPower = integ.excessPower(peakBin - guardHalf, peakBin + guardHalf) -
                                       inPower;
                    int inBins = (chanHi - chanLo) + 1;
                    int ringBins = std::max<int>(1, (2 * guardHalf) + 1 - inBins);
                    double inDensity = inPower / std::max(inBins, 1);
                    double ringDensity = ringPower / ringBins;
                    if (inDensity > 0.0 &&
                        ringDensity > inDensity * std::pow(10.0, -(double)params.maxSpillDb / 10.0)) {
                        continue;
                    }
                }

                DetectedSignal sig;
                // Geometry and SNR come from the occupied part of the channel, not the whole
                // channel: a narrow carrier in a 25 kHz slot would otherwise report an SNR
                // diluted by the empty spectrum either side of it.
                int occLo = chanLo;
                int occHi = chanHi;
                occupiedWindow(integ, chanLo, chanHi, OCCUPIED_FRACTION, occLo, occHi);
                fillGeometry(sig, integ, occLo, occHi, specLow, binWidth);
                sig.snrDb = winningSnrDb(integ, tests, peakBin, params.loBin, params.hiBin);

                // On a raster the channel is the signal's identity and its receiver width; the
                // measured extent is reported separately.
                sig.occupiedBandwidth = ((occHi - occLo) + 1) * binWidth;
                sig.channelFrequency = channelFreq;
                sig.channelIndex = idx;
                sig.lowerFrequency = channelFreq - (chanBw / 2.0);
                sig.upperFrequency = channelFreq + (chanBw / 2.0);
                sig.centerFrequency = channelFreq;
                sig.bandwidth = chanBw;

                out.push_back(sig);
            }
            return out;
        }

        std::vector<DetectedSignal> detectGrouped(const SpectrumIntegrator& integ,
                                                  const std::vector<KernelTest>& tests,
                                                  double specLow, double binWidth,
                                                  const PassParams& params) {
            std::vector<DetectedSignal> out;

            int minBins = std::max<int>(params.minBins, 1);
            if (params.minBandwidthHz > 0.0) {
                minBins = std::max<int>(minBins, (int)std::floor(params.minBandwidthHz / binWidth));
            }
            int maxGap = 0;
            if (params.mergeGapHz > 0.0) {
                maxGap = (int)std::lround(params.mergeGapHz / binWidth);
            }
            int maxBins = std::numeric_limits<int>::max();
            if (params.maxBandwidthHz > 0.0) {
                maxBins = std::max<int>(1, (int)std::ceil(params.maxBandwidthHz / binWidth));
            }

            int runStart = -1;
            int runEnd = -1;
            int gap = 0;

            auto emit = [&](int start, int end) {
                if (start < 0 || end < start) { return; }

                // The flagged run is as wide as the widest kernel that fired, which for a narrow
                // carrier is far wider than the signal. Reporting that as the bandwidth would be
                // wrong, and integrating SNR over it would dilute a real signal with noise-only
                // bins. Shrink to the extent actually carrying the power.
                int oLo = start;
                int oHi = end;
                occupiedWindow(integ, start, end, OCCUPIED_FRACTION, oLo, oHi);

                int width = oHi - oLo + 1;
                if (width < minBins || width > maxBins) { return; }

                DetectedSignal sig;
                fillGeometry(sig, integ, oLo, oHi, specLow, binWidth);
                sig.snrDb = winningSnrDb(
                    integ, tests, strongestBin(integ, tests, start, end, params.loBin, params.hiBin),
                    params.loBin, params.hiBin);
                out.push_back(sig);
            };

            for (int b = params.loBin; b < params.hiBin; b++) {
                bool above = bestOverThreshold(integ, tests, b, params.loBin, params.hiBin) > 1.0;
                if (above) {
                    if (runStart < 0) { runStart = b; }
                    runEnd = b;
                    gap = 0;
                }
                else if (runStart >= 0) {
                    gap++;
                    if (gap > maxGap) {
                        emit(runStart, runEnd);
                        runStart = -1;
                        runEnd = -1;
                        gap = 0;
                    }
                }
            }
            emit(runStart, runEnd);

            return out;
        }
    }

    std::vector<DetectedSignal> detectPass(const SpectrumIntegrator& integ, double centerFrequency,
                                           double spanHz, const PassParams& params) {
        std::vector<DetectedSignal> out;
        if (integ.empty() || spanHz <= 0.0) { return out; }
        if (params.hiBin <= params.loBin) { return out; }
        if (params.kernelBins.empty()) { return out; }

        double binWidth = spanHz / (double)integ.binCount();
        double specLow = centerFrequency - (spanHz / 2.0);

        auto tests = buildTests(params);
        if (tests.empty()) { return out; }

        if (params.channelStepHz > 0.0) {
            return detectChannelized(integ, tests, specLow, binWidth, params);
        }
        return detectGrouped(integ, tests, specLow, binWidth, params);
    }
}
