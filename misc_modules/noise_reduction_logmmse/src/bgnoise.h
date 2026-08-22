//
// Created by san on 10/07/22.
//
#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

class BackgroundNoiseCaltulator {

    double lastNoise = ERASED_SAMPLE;
    static constexpr auto NBUCKETS = 1000;
    static constexpr auto SKIP_FRAMES = 10;
    std::vector<int> buckets;
    std::vector<float> logFrame;
    int frameCount = 0;

public:

    static constexpr auto ERASED_SAMPLE = 1e9f;

    void reset() {
        lastNoise = ERASED_SAMPLE;
        frameCount = 0;
    }

    float addFrame(const std::vector<float> &fftFrame) {
        if (frameCount > 0 && frameCount % SKIP_FRAMES != 0) {
            frameCount++;
            return lastNoise;
        }
        frameCount++;
        float minn = ERASED_SAMPLE;
        float maxx = -ERASED_SAMPLE;
        logFrame.clear();
        logFrame.reserve(fftFrame.size());
        for(float q : fftFrame) {
            if(q == ERASED_SAMPLE || !std::isfinite(q) || q < 0.0f) {
                continue;
            }
            if (q == 0.0f) {
                q = (std::numeric_limits<float>::min)();
            }
            float logged = std::log10(q);
            if (!std::isfinite(logged)) {
                continue;
            }
            minn = (std::min)(minn, logged);
            maxx = (std::max)(maxx, logged);
            logFrame.push_back(logged);
        }
        if (logFrame.empty()) {
            return lastNoise;
        }
        auto width = maxx - minn;
        buckets.resize(NBUCKETS);
        memset(buckets.data(), 0, sizeof(int) * NBUCKETS);
        if (std::isfinite(width) && width > 0) {
            for(auto f : logFrame) {
                float bucketPosition = (f - minn) / width;
                if (!std::isfinite(bucketPosition)) {
                    continue;
                }
                bucketPosition = (std::max)(0.0f, (std::min)(bucketPosition, 1.0f));
                int bucket = (std::min)((int) (NBUCKETS * bucketPosition), NBUCKETS - 1);
                buckets[bucket]++;
            }
        }
        else {
            buckets[0] = (int)logFrame.size();
        }
        auto ix = std::max_element(buckets.begin(), buckets.end()) - buckets.begin();
        double maxf = pow(10, ((((double)ix)/NBUCKETS) * width + minn));
        if (lastNoise == ERASED_SAMPLE) {
            lastNoise = maxf;
        } else {
            lastNoise = 0.9 * lastNoise + 0.1 * maxf;
        }
        return lastNoise;

    }

};
