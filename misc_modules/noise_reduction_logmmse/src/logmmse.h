#pragma once

#include "utils/arrays.h"
#include "math.h"
#include "bgnoise.h"
#include <array>
#include <cassert>
#include <vector>
#include <ctm.h>

namespace dsp {

#if 0
#define ALLOC_AND_CHECK(x, sz, point) \
    { \
        std::vector<float> lower(440, 0); \
        if (x->size() != sz) { \
            flog::info("Abort in {}", point); \
            abort(); \
        } \
    }
#else
#define ALLOC_AND_CHECK(x, sz, point)
#endif


    // ported from https://github.com/rajivpoddar/logmmse/  by sannysanoff

    namespace logmmse {

        // courtesy of https://github.com/jimmyberg/LowPassFilter
        class LowPassFilter {

        public:
            LowPassFilter(float iCutOffFrequency, float iDeltaTime) : output(0), ePow(1 - exp(-iDeltaTime * 2 * M_PI * iCutOffFrequency)) {
            }

            float update(float input) {
                return output += (input - output) * ePow;
            }

            float update(float input, float deltaTime, float cutoffFrequency);

            float getOutput() const { return output; }

            void reconfigureFilter(float deltaTime, float cutoffFrequency);

        private:
            float output;
            float ePow;
        };

        using namespace ::dsp::arrays;
        using namespace ::dsp::math;

        struct LogMMSE {

            struct SavedParamsC {
                int noise_history_len() {
                    if (nFFT < 1000) {
                        return 2000;
                    } else {
                        return 200;
                    }
                }

                std::vector<FloatArray> noise_history;      // circular storage of nFFT*float chunks
                std::vector<FloatArray> dev_history;
                size_t noiseHistoryHead = 0;
                size_t noiseHistorySize = 0;
                size_t devHistoryHead = 0;
                size_t devHistorySize = 0;
                FloatArray sums;      // sliding sum of last N noise_history
                FloatArray devs;      // sliding sum of dev_history
                ComplexArray Xn_prev; // remaining noise

                FloatArray noise_mu2;
                FloatArray candidateNoiseMu2;
                FloatArray Xk_prev;
                ComplexArray x_old;
                struct Workspace {
                    FloatArray magnitude;
                    FloatArray gamma;
                    FloatArray a;
                    FloatArray expInput;
                    FloatArray gain;

                    void configure(int fftSize) {
                        auto configureArray = [fftSize](FloatArray &array) {
                            if (!array || array->size() != fftSize) {
                                array = npzeros(fftSize);
                            }
                        };
                        configureArray(magnitude);
                        configureArray(gamma);
                        configureArray(a);
                        configureArray(expInput);
                        configureArray(gain);
                    }
                } workspace;
                bool forceAudio = false;
                bool forceWideband = false;
                int forceSampleRate = 0;


                int Slen;
                int PERC;
                int len1;
                int len2;
                FloatArray win;
                int nFFT;
                Arg<FFTPlan> forwardPlan;
                Arg<FFTPlan> reversePlan;
                float aa = 0.98;
                float mu = 0.98;
                float ksi_min;
                bool hold = false;
                long long generation = 0;
                float mindb = 0;
                float maxdb = 0;
                bool stable = false;
                bool previousEstimateValid = false;

                void clearHistories() {
                    noiseHistoryHead = 0;
                    noiseHistorySize = 0;
                    devHistoryHead = 0;
                    devHistorySize = 0;
                    if (sums) {
                        std::fill(sums->begin(), sums->end(), 0.0f);
                    }
                    if (devs) {
                        std::fill(devs->begin(), devs->end(), 0.0f);
                    }
                }

                void configureHistories() {
                    const size_t historyCapacity = static_cast<size_t>(noise_history_len());
                    noise_history.resize(historyCapacity);
                    dev_history.resize(historyCapacity);
                    for (size_t index = 0; index < historyCapacity; index++) {
                        if (!noise_history[index] || noise_history[index]->size() != nFFT) {
                            noise_history[index] = npzeros(nFFT);
                        }
                        if (!dev_history[index] || dev_history[index]->size() != nFFT) {
                            dev_history[index] = npzeros(nFFT);
                        }
                    }
                    clearHistories();
                }

                const FloatArray& noiseHistoryAt(size_t index) const {
                    return noise_history[(noiseHistoryHead + index) % noise_history.size()];
                }

//                float *devsD = nullptr;
//                float *hiD = nullptr;
//                float *diffD = nullptr;

                void reset() {
                    clearHistories();
                    Xk_prev.reset();
                    Xn_prev.reset();
                    noise_mu2.reset();
                    candidateNoiseMu2.reset();
                    x_old.reset();
                    generation = 0;
                    stable = false;
                    previousEstimateValid = false;
                    backgroundNoiseCalculator.reset();

                }

                void add_noise_history(const float *noise) {
                    if (hold) {
                        return;
                    }
                    const size_t historyCapacity = static_cast<size_t>(noise_history_len());
                    if (!noise || noise_history.size() != historyCapacity || dev_history.size() != historyCapacity ||
                        !sums || !devs) {
                        flog::error("LogMMSE noise history is not configured");
                        return;
                    }
                    size_t noiseIndex;
                    if (noiseHistorySize < historyCapacity) {
                        noiseIndex = (noiseHistoryHead + noiseHistorySize) % historyCapacity;
                        noiseHistorySize++;
                    }
                    else {
                        noiseIndex = noiseHistoryHead;
                        volk_32f_x2_subtract_32f(sums->data(), sums->data(), noise_history[noiseIndex]->data(), nFFT);
                        noiseHistoryHead = (noiseHistoryHead + 1) % historyCapacity;
                    }
                    std::copy_n(noise, nFFT, noise_history[noiseIndex]->data());
                    volk_32f_x2_add_32f(sums->data(), sums->data(), noise, nFFT);
                    size_t devIndex;
                    bool replacingDev = false;
                    if (devHistorySize < historyCapacity) {
                        devIndex = (devHistoryHead + devHistorySize) % historyCapacity;
                        devHistorySize++;
                    }
                    else {
                        devIndex = devHistoryHead;
                        devHistoryHead = (devHistoryHead + 1) % historyCapacity;
                        replacingDev = true;
                    }
                    auto devFrame = dev_history[devIndex]->data();
                    auto sumsData = sums->data();
                    auto devsData = devs->data();
                    if (replacingDev) {
                        volk_32f_x2_subtract_32f(devsData, devsData, devFrame, nFFT);
                    }
                    const float inverseHistorySize = 1.0f / static_cast<float>(noiseHistorySize);
                    for (int i = 0; i < nFFT; i++) {
                        const float difference = noise[i] - sumsData[i] * inverseHistorySize;
                        const float deviation = difference * difference;
                        devFrame[i] = deviation;
                    }
                    volk_32f_x2_add_32f(devsData, devsData, devFrame, nFFT);

                }

                BackgroundNoiseCalculator backgroundNoiseCalculator;


#ifdef SDRPP_NR_PROFILE
#define ADD_STEP_STATS()          ctm2 = currentTimeNanos(); muSum[statIndex++] += ctm2-ctm; ctm = ctm2
#else
#define ADD_STEP_STATS()          do {} while (0)
#endif

                void update_noise_mu2(const ComplexArray &x) {
                    auto sz = x->size();
                    ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 1.5a")
#ifdef SDRPP_NR_PROFILE
                    static long long muSum[30] = {0,}, muCount = 0; auto ctm = currentTimeNanos();long long ctm2; auto statIndex = 0;
#endif
                    auto nframes = noiseHistorySize;
                    bool audioFrequency = nFFT < 1200;
                    if (forceAudio) audioFrequency = true;
                    if (forceWideband) audioFrequency = false;
//                    auto dump = dumpEnabler == 10;
                    ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 0")

                    if (nframes > 100 && !hold) {
//                        if (dump) {
//                            std::cout << "Mu2 history" << std::endl;
//                        }
                        if (audioFrequency) {
                            ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 1")

                            // recalculate noise floor
                            if (generation > 0) {
                                std::vector<float> lower(nFFT, 0);
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 1.5")
                                const int nlower = 12;
                                for (size_t ix = nframes - nlower; ix < nframes; ix++) {
                                    auto nhFrame = noiseHistoryAt(ix)->data();
                                    for (auto w = 0; w < nFFT; w++) {
                                        lower[w] += nhFrame[w];
                                    }
                                }
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 2")
                                for (auto w = 0; w < nFFT; w++) {
                                    lower[w] /= nlower;
                                    lower[w] *= lower[w];
                                }
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 3")
                                auto tnm = std::make_shared<std::vector<float>>(lower);
                                auto tnoise_mu2 = npmavg(tnm, 6);
                                auto tmindb = *std::min_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                auto tmaxdb = *std::max_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 4")
                                if (tmindb + tmaxdb < mindb + maxdb) {
                                    
//                                    spdlog::info("Updated noise floor...{0} ( {1}, {2} )", (tmindb + tmaxdb)/2, tmindb, tmaxdb);
                                    mindb = tmindb;
                                    maxdb = tmaxdb;
                                    noise_mu2 = tnm;
                                    stable = true;
                                }
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 5")
                            }

                            if (!stable) {

                                // scale the noise figure
                                if (generation == 0) {
                                    auto tnoise_mu2 = npmavg(noise_mu2, 6);
                                    mindb = *std::min_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                    maxdb = *std::max_element(tnoise_mu2->begin(), tnoise_mu2->end());
                                    std::cout << "Inited noise floor..." << mindb << std::endl;
                                }
                                ALLOC_AND_CHECK(x, sz, "update_noise_mu2 point 6")

                            }
                            generation++;
                        } else {

                            if (!backgroundNoiseCalculator.updateDue()) {
                                backgroundNoiseCalculator.skipFrame();
                                return;
                            }

                            auto noiseAvg = mul(sums, 1 / (float)nframes);

                            ADD_STEP_STATS();

                            auto hi = mul(devs, 1 / (float)nframes);
                            auto devSquare = muleach(hi, hi);
                            auto devSquareD = devSquare->data();
                            ADD_STEP_STATS();
                            for (int z = 0; z < nFFT; z++) {
                                if (abs(z - nFFT/2) < nFFT * 15 / 100) {
                                    // after fft, rightmost and leftmost sides of real frequencies range are at the center of the resulting table.
                                    // We exclude middle of the table from lookup
                                    devSquareD[z] = BackgroundNoiseCalculator::ERASED_SAMPLE;
                                }
                            }
                            memset(candidateNoiseMu2->data(), 0, nFFT*sizeof(candidateNoiseMu2->at(0)));
                            ADD_STEP_STATS();
                            std::vector<float> devs(devSquareD, devSquareD +nFFT);
                            float detectedNoise = backgroundNoiseCalculator.addFrame(devs);
                            ADD_STEP_STATS();
                            auto acceptible_stdev = detectedNoise;
                            auto nmu2 = candidateNoiseMu2->data();
                            auto navg =  noiseAvg->data();
                            for(int q=0; q < nFFT; q++) {
                                if (devs[q] < acceptible_stdev) {
                                    nmu2[q] = navg[q] * navg[q];
                                }
                            }

                            if (linearInterpolateHoles(nmu2, nFFT)) {
                                noise_mu2.swap(candidateNoiseMu2);
                            }

                            ADD_STEP_STATS();

                        }  // end if audio frequency



                    }
                    // 192 non-volk: 984000 0    888000 32000 8000 316000
                    // 192 volk:      136000 4000 340000 12000 8000 260000
                    // 192 volk:      130000 4000 300000 12000 8000 260000  // after volk_alloc
                    // 192 mu2:       146625 2250 377750 3125  4250 333875  averages after np** conv
                    // 384             211	3	534	5	7	494             // avg
                    //                 274	3	684	8	6	618	41          // ? fixed alloc
                    //                 604	4	718	5	9	248	37
                    // 768 mu2:        8 8 16 753 133
                    // 768 mu2:        8 9 10 557 176
                    // 768 mu2:        16 18 23 347 225         // after sample count instead of sort
                    // 768 mu2:        13 8 12 52 195         // after dropping each 10th frame for noise dev calculation
                    // 768 mu2:        13 8 12 52 115         // replaced at() with direct data access.
#ifdef SDRPP_NR_PROFILE
                    muCount++;
                    if (muCount == 1000 && false) {
                        std::cout << "mu2: ";
                        for(int z=0; z<statIndex; z++) {
                            std::cout << " " << std::to_string(muSum[z] / 1000);
                            muSum[z] = 0;
                        }
                        std::cout << std::endl;
                        muCount = 0;
                    }
#endif
                }
            };

            static void logmmse_sample(const ComplexArray &x, int Srate, float eta, SavedParamsC *params, int noise_frames) {
                params->Slen = floor(0.02 * Srate);
                if (params->Slen % 2 == 1) params->Slen++;
                params->PERC = 50;
                params->len1 = floor(params->Slen * params->PERC / 100);
                params->clearHistories();
                params->len2 = params->Slen - params->len1;         // len1+len2
                auto audioFrequency = Srate <= 24000;
                if (params->forceAudio) audioFrequency = true;
                if (params->forceWideband) audioFrequency = false;
                if (audioFrequency) {
                    // probably audio frequency
                    params->win = nphanning(params->Slen);
                    params->win = div(mul(params->win, params->len2), npsum(params->win));
                } else {
                    // probably wide band
                    params->win = nphanning(params->Slen);
                    params->win = div(mul(params->win, params->len2), npsum(params->win));
//                    params->win = npzeros(params->Slen);
//                    for (int i = 0; i < params->win->size(); i++) {
//                        params->win->at(i) = 1.0;
//                    }
                }
                params->nFFT = 2 * params->Slen;
                params->forwardPlan = allocateFFTWPlan(false, params->nFFT);
                params->reversePlan = allocateFFTWPlan(true, params->nFFT);
                params->sums = npzeros(params->nFFT);
                params->devs = npzeros(params->nFFT);
                params->workspace.configure(params->nFFT);
                params->configureHistories();

                std::cout << "Sampling piece... srate=" << Srate << " Slen=" << params->Slen << " nFFT=" << params->nFFT << std::endl;
                auto Nframes = floor(x->size() / params->len2) - floor(params->Slen / params->len2);
                auto xfinal = npzeros(Nframes * params->len2);
                auto noise_mean = npzeros(params->nFFT);
                for (int j = 0; j < params->Slen * noise_frames; j += params->Slen) {
                    npfftfft((muleach(params->win, nparange(x, j, j + params->Slen))), params->forwardPlan);
                    auto noise = npabsolute(params->forwardPlan->getOutput());
                    params->add_noise_history(noise->data());
                    noise_mean = addeach(noise_mean, noise);
                }
                params->noise_mu2 = div(noise_mean, noise_frames);
                if (!audioFrequency) {
                    params->noise_mu2 = npmavg(params->noise_mu2, 120);
                }
                params->noise_mu2 = muleach(params->noise_mu2, params->noise_mu2);
                params->candidateNoiseMu2 = npzeros(params->nFFT);
//                for (int ix = 0; ix < params->noise_mu2->size(); ix++) {
//                    std::cout << "Noise\t" << (ix) << "\t" << params->noise_mu2->at(ix) << std::endl;
//                }
                params->Xk_prev = npzeros(params->nFFT);
                params->previousEstimateValid = false;
                params->Xn_prev = npzeros_c(0);
                params->x_old = npzeros_c(params->len1);
                params->ksi_min = ::pow(10, -25.0 / 10.0);
//            std::cout << "sample: noisemu: " << sampleArr(params->noise_mu2) << std::endl;
            }

            static ComplexArray logmmse_all(const ComplexArray &x, int inputCount, int Srate, float eta, SavedParamsC *params) {
                int sz = inputCount;
#ifdef SDRPP_NR_PROFILE
                static long long muSum[30] = {0,}, muCount = 0; auto ctm = currentTimeNanos();long long ctm2; auto statIndex = 0;
#endif
                ALLOC_AND_CHECK(x, sz, "logmmse_all point -1")

                auto Nframes = (std::max)(0, inputCount / params->len2 - params->Slen / params->len2);
                if (Nframes == 0) {
                    return npzeros_c(0);
                }
                ALLOC_AND_CHECK(x, sz, "logmmse_all point -1.5")
                ADD_STEP_STATS();
                ALLOC_AND_CHECK(x, sz, "logmmse_all point -1.7")
                params->update_noise_mu2(x);
                ALLOC_AND_CHECK(x, sz, "logmmse_all point -1.8")
                ADD_STEP_STATS();
                auto xfinal = npzeros_c(Nframes * params->len2);
                ALLOC_AND_CHECK(x, sz, "logmmse_all point 0")
                auto forwardInput = params->forwardPlan->getInput();
                auto forwardOutput = params->forwardPlan->getOutput();
                auto reverseInput = params->reversePlan->getInput();
                auto reverseOutput = params->reversePlan->getOutput();
#ifndef NDEBUG
                assert(inputCount >= 0);
                assert(inputCount <= static_cast<int>(x->size()));
                assert(params->win->size() == params->Slen);
                assert(forwardInput->size() == params->nFFT && forwardOutput->size() == params->nFFT);
                assert(reverseInput->size() == params->nFFT && reverseOutput->size() == params->nFFT);
                assert(params->workspace.magnitude->size() == params->nFFT);
                assert(params->workspace.gamma->size() == params->nFFT);
                assert(params->workspace.a->size() == params->nFFT);
                assert(params->workspace.expInput->size() == params->nFFT);
                assert(params->workspace.gain->size() == params->nFFT);
                assert(params->noise_mu2->size() == params->nFFT);
                assert(params->Xk_prev->size() == params->nFFT);
                assert(params->x_old->size() == params->len1);
                for (const auto &frame : params->noise_history) {
                    assert(frame && frame->size() == params->nFFT);
                }
                for (const auto &frame : params->dev_history) {
                    assert(frame && frame->size() == params->nFFT);
                }
#endif
                auto magnitude = params->workspace.magnitude->data();
                auto gamma = params->workspace.gamma->data();
                auto a = params->workspace.a->data();
                auto expInput = params->workspace.expInput->data();
                auto gain = params->workspace.gain->data();
                auto forwardInputData = forwardInput->data();
                auto forwardOutputData = forwardOutput->data();
                auto reverseInputData = reverseInput->data();
                auto reverseOutputData = reverseOutput->data();
                auto xData = x->data();
                auto noiseMu2Data = params->noise_mu2->data();
                auto previousEstimateData = params->Xk_prev->data();
                auto windowData = params->win->data();
                auto xOldData = params->x_old->data();
                auto xfinalData = xfinal->data();
                for (int k = 0; k < Nframes * params->len2; k += params->len2) {
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 1")
                    for (int i = 0; i < params->Slen; i++) {
                        forwardInputData[i].re = xData[k + i].re * windowData[i];
                        forwardInputData[i].im = xData[k + i].im * windowData[i];
                    }
                    std::fill(forwardInputData + params->Slen, forwardInputData + params->nFFT, complex_t{});
                    npfftfft(params->forwardPlan);
                    volk_32fc_magnitude_32f(magnitude, (const lv_32fc_t*)forwardOutputData, params->nFFT);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 2")
                    for (int z = 1; z < params->nFFT; z++) {
                        if (magnitude[z] == 0) {
                            magnitude[z] = magnitude[z - 1];      // for some reason fft returns 0 instead if small value
                        }
                    }
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 3")
                    params->add_noise_history(magnitude);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 4")
                    // posterior SNR
                    for (int i = 0; i < params->nFFT; i++) {
                        gamma[i] = (std::min)(magnitude[i] * magnitude[i] / noiseMu2Data[i], 40.0f);
                    }
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 5")
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 6")
                    if (!params->previousEstimateValid) {
                        for (int i = 0; i < params->nFFT; i++) {
                            a[i] = (std::max)(gamma[i] - 1.0f, 0.0f) * (1.0f - params->aa) + params->aa;
                        }
                    } else {
                        for (int i = 0; i < params->nFFT; i++) {
                            a[i] = (std::max)(previousEstimateData[i] * params->aa / noiseMu2Data[i] +
                                              (std::max)(gamma[i] - 1.0f, 0.0f) * (1.0f - params->aa),
                                              params->ksi_min);
                        }
                    }
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 10")
                    // A = ksi / (1 + ksi), then expInput = 0.5 * E1(A * gamma).
                    for (int i = 0; i < params->nFFT; i++) {
                        a[i] = a[i] / (a[i] + 1.0f);
                        gamma[i] = a[i] * gamma[i];
                        expInput[i] = 0.5f * dsp::math::expn(gamma[i]);
                    }
                    volk_32f_expfast_32f(gain, expInput, params->nFFT);
                    for (int i = 0; i < params->nFFT; i++) {
                        gain[i] *= a[i];
                        const float enhancedMagnitude = magnitude[i] * gain[i];
                        previousEstimateData[i] = enhancedMagnitude * enhancedMagnitude;
                        reverseInputData[i].re = forwardOutputData[i].re * gain[i];
                        reverseInputData[i].im = forwardOutputData[i].im * gain[i];
                    }
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 14")
                    params->previousEstimateValid = true;
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 15")
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 16")
                    npfftfft(params->reversePlan);
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 17")
                    for (int i = 0; i < params->len1; i++) {
                        xfinalData[k + i] = xOldData[i] + reverseOutputData[i];
                        xOldData[i] = reverseOutputData[params->len1 + i];
                    }
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 18")
                    ALLOC_AND_CHECK(x, sz, "logmmse_all point 19")
                }
                ALLOC_AND_CHECK(x, sz, "logmmse_all point 20")
                ADD_STEP_STATS();
                ALLOC_AND_CHECK(x, sz, "logmmse_all point 21")
                #ifdef SDRPP_NR_PROFILE
                muCount++;

                if (muCount == 1000) {
                    // 192 logmmse_all:  371000 684000 806000 (avgs)
                    // 192 logmmse_all:  307000 892000 286000 (avgs) - some np moved to volk
                    // 192 logmmse_all:  332000 886000 247000 (avgs) - all np moved to volk
                    // 384               606	1284	441          avg
                    //                   836	1635	598          ?? fixed
                    //                   819	1620	580
                    //                   874	1612	534         // file source, local allocs (!)
                    //                   0	    1738	790         // radio src
                    // 768 logmmse_all:  0      920     786
                    // 768 logmmse_all:  0      762     841         //
                    if (false) {
                        std::cout << "logmmse_all: ";
                        for (int z = 0; z < statIndex; z++) {
                            std::cout << " " << std::to_string(muSum[z] / 1000);
                            muSum[z] = 0;
                        }
                        std::cout << std::endl;
                    }
                    muCount = 0;
                }
                #endif
                ADD_STEP_STATS();
                ALLOC_AND_CHECK(x, sz, "logmmse_all point 23")
                return xfinal;
            }

            static ComplexArray logmmse_all(const ComplexArray &x, int Srate, float eta, SavedParamsC *params) {
                return logmmse_all(x, (int)x->size(), Srate, eta, params);
            }

        };

    }
}
