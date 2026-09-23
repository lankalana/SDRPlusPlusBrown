#include "signal_tracker.h"
#include <algorithm>

namespace dsp::detector {

    bool SignalTracker::sameChannel(const DetectedSignal& a, const DetectedSignal& b) {
        return a.channelFrequency != 0.0 && a.channelFrequency == b.channelFrequency;
    }

    double SignalTracker::overlapRatio(const DetectedSignal& a, const DetectedSignal& b) {
        double lo = std::max(a.lowerFrequency, b.lowerFrequency);
        double hi = std::min(a.upperFrequency, b.upperFrequency);
        double overlap = hi - lo;
        if (overlap <= 0.0) { return 0.0; }

        double narrower = std::min(a.bandwidth, b.bandwidth);
        if (narrower <= 0.0) { return 0.0; }
        return overlap / narrower;
    }

    void SignalTracker::clear() {
        tracked.clear();
        recentlyEnded.clear();
    }

    void SignalTracker::update(const std::vector<DetectedSignal>& detections, uint64_t nowMs) {
        recentlyEnded.clear();

        std::vector<bool> detectionUsed(detections.size(), false);
        std::vector<bool> trackMatched(tracked.size(), false);

        // Greedy association: every track claims its best unused detection. Tracks are few and
        // detections are few, so the quadratic scan is not worth optimizing.
        for (size_t t = 0; t < tracked.size(); t++) {
            auto& track = tracked[t];
            int bestIdx = -1;
            double bestRatio = params.minOverlapRatio;

            // A matching channel is decisive: two fragments of one station need not overlap.
            for (size_t d = 0; d < detections.size(); d++) {
                if (detectionUsed[d]) { continue; }
                if (sameChannel(track.signal, detections[d])) {
                    bestIdx = (int)d;
                    break;
                }
            }

            if (bestIdx < 0) {
                for (size_t d = 0; d < detections.size(); d++) {
                    if (detectionUsed[d]) { continue; }
                    // Never steal a detection that belongs to some other channel.
                    if (detections[d].channelFrequency != 0.0 &&
                        track.signal.channelFrequency != 0.0) {
                        continue;
                    }
                    double ratio = overlapRatio(track.signal, detections[d]);
                    if (ratio >= bestRatio) {
                        bestRatio = ratio;
                        bestIdx = (int)d;
                    }
                }
            }

            if (bestIdx < 0) { continue; }
            detectionUsed[bestIdx] = true;
            trackMatched[t] = true;

            uint64_t id = track.signal.id;
            uint64_t firstSeen = track.signal.firstSeen;
            track.signal = detections[bestIdx];
            track.signal.id = id;
            track.signal.firstSeen = firstSeen;
            track.signal.lastSeen = nowMs;
            track.lastDetectedMs = nowMs;

            // A releasing signal that comes back is the same transmission resuming, not a new one.
            if (track.state == SignalState::RELEASING) {
                track.state = track.wasActive ? SignalState::ACTIVE : SignalState::CANDIDATE;
                track.stateSinceMs = nowMs;
            }

            // Promotion is only ever evaluated for a track that is present in this frame, so a
            // transient cannot mature into ACTIVE while it is absent.
            if (track.state == SignalState::CANDIDATE &&
                (nowMs - track.signal.firstSeen) >= params.activationMs) {
                track.state = SignalState::ACTIVE;
                track.stateSinceMs = nowMs;
                track.wasActive = true;
            }
        }

        // Tracks with no detection this frame age towards ENDED.
        for (size_t t = 0; t < tracked.size(); t++) {
            auto& track = tracked[t];
            if (trackMatched[t]) { continue; }

            if (track.state == SignalState::ACTIVE) {
                track.state = SignalState::RELEASING;
                track.stateSinceMs = nowMs;
            }
            if ((nowMs - track.lastDetectedMs) >= params.releaseMs) {
                track.state = SignalState::ENDED;
                track.stateSinceMs = nowMs;
            }
        }

        for (auto& track : tracked) {
            if (track.state == SignalState::ENDED) { recentlyEnded.push_back(track); }
        }
        tracked.erase(std::remove_if(tracked.begin(), tracked.end(),
                                     [](const TrackedSignal& t) { return t.state == SignalState::ENDED; }),
                      tracked.end());

        // Anything left over is a new candidate.
        for (size_t d = 0; d < detections.size(); d++) {
            if (detectionUsed[d]) { continue; }
            TrackedSignal track;
            track.signal = detections[d];
            track.signal.id = nextId++;
            track.signal.firstSeen = nowMs;
            track.signal.lastSeen = nowMs;
            track.state = SignalState::CANDIDATE;
            track.stateSinceMs = nowMs;
            track.lastDetectedMs = nowMs;
            track.wasActive = false;

            // Zero activation delay means the very first frame confirms the signal.
            if (params.activationMs == 0) {
                track.state = SignalState::ACTIVE;
                track.wasActive = true;
            }
            tracked.push_back(track);
        }
    }

    std::vector<DetectedSignal> SignalTracker::getActiveSignals() const {
        std::vector<DetectedSignal> out;
        for (const auto& track : tracked) {
            if (track.state == SignalState::ACTIVE) { out.push_back(track.signal); }
        }
        return out;
    }
}
