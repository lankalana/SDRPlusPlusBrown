#pragma once

#include <utils/flog.h>
#include <algorithm>
#include <deque>
#include <numeric>
#include <string>
#include <ctm.h>


struct StreamTracker {
    int lastCount = 0;
    int lastCountSecond = 0;
    std::string title;

    std::deque<int> queue;
    float xavg = 0;

    StreamTracker(const std::string &title) : title(title) {
    }

    void add(int count) {
        auto ctm = currentTimeMillis();
        if (lastCountSecond != ctm / 1000) {
            lastCountSecond = ctm / 1000;
            queue.emplace_back(lastCount);
            while(queue.size() > 100) {
                queue.pop_front();
            }
            const auto averageLast = [this](std::size_t count) {
                const auto sampleCount = (std::min)(count, queue.size());
                return std::accumulate(queue.rbegin(), queue.rbegin() + sampleCount, 0) / static_cast<int>(sampleCount);
            };
            const int per8second = averageLast(8);
            const int per30second = averageLast(30);
            if (queue.size() == 8) {
                xavg = per8second;
            }
            xavg = xavg * 0.9f + lastCount * 0.1f;
            flog::info("Stream tracker: {}  per second: {}  xavg: {} per 8 sec: {}  per 30 sec: {}", title, lastCount, xavg, per8second, per30second);
            lastCount = 0;
        }
        lastCount += count;
    }


};
