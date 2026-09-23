#include <signal_path/reception_profile.h>
#include <algorithm>
#include <cmath>

const char* toString(ProfileDemod demod) {
    switch (demod) {
    case ProfileDemod::NFM: return "NFM";
    case ProfileDemod::WFM: return "WFM";
    case ProfileDemod::AM: return "AM";
    case ProfileDemod::DSB: return "DSB";
    case ProfileDemod::USB: return "USB";
    case ProfileDemod::CW: return "CW";
    case ProfileDemod::LSB: return "LSB";
    case ProfileDemod::RAW: return "RAW";
    default: return "?";
    }
}

const char* profileDemodComboItems() {
    return "NFM\0WFM\0AM\0DSB\0USB\0CW\0LSB\0RAW\0";
}

double snapToStep(double frequency, double step) {
    if (step <= 0.0) { return frequency; }
    return std::round(frequency / step) * step;
}

double ReceptionProfile::snapFrequency(double frequency) const {
    return snapToStep(frequency, frequencyStep);
}

bool ReceptionProfile::matchesFrequency(double frequency) const {
    // Both bounds zero means "the whole spectrum", which is what a single global profile wants.
    if (minFrequency == 0.0 && maxFrequency == 0.0) { return true; }
    return frequency >= minFrequency && frequency <= maxFrequency;
}

const ReceptionProfile* ReceptionProfileSet::findFor(double frequency) const {
    const ReceptionProfile* best = nullptr;
    for (const auto& p : profiles) {
        if (!p.enabled || !p.matchesFrequency(frequency)) { continue; }
        // Strictly greater, so the earliest profile wins a tie.
        if (best == nullptr || p.priority > best->priority) { best = &p; }
    }
    return best;
}

bool ReceptionProfileSet::detectionLimitsAt(double frequency, double& minBandwidth,
                                            double& maxBandwidth) const {
    bool found = false;
    double lowest = 0.0;
    double highest = 0.0;
    bool unbounded = false;

    for (const auto& p : profiles) {
        if (!p.enabled || !p.matchesFrequency(frequency)) { continue; }
        if (!found) {
            found = true;
            lowest = p.minDetectionBandwidth;
        }
        else {
            lowest = std::min(lowest, p.minDetectionBandwidth);
        }
        if (p.maxDetectionBandwidth <= 0.0) { unbounded = true; }
        else {
            highest = std::max(highest, p.maxDetectionBandwidth);
        }
    }

    if (!found) { return false; }
    minBandwidth = lowest;
    maxBandwidth = unbounded ? 0.0 : highest;
    return true;
}

bool ReceptionProfileSet::covers(double frequency) const { return findFor(frequency) != nullptr; }

bool ReceptionProfileSet::anyEnabled() const {
    for (const auto& p : profiles) {
        if (p.enabled) { return true; }
    }
    return false;
}

ReceptionProfileSet ReceptionProfileSet::defaults() {
    ReceptionProfileSet set;

    auto add = [&](const char* name, double lo, double hi, ProfileDemod demod, double bw,
                   double minDet, double maxDet, double step, double mergeGap, bool enabled) {
        ReceptionProfile p;
        p.name = name;
        p.minFrequency = lo;
        p.maxFrequency = hi;
        p.demod = demod;
        p.bandwidth = bw;
        p.minDetectionBandwidth = minDet;
        p.maxDetectionBandwidth = maxDet;
        p.frequencyStep = step;
        p.mergeGapHz = mergeGap;
        p.enabled = enabled;
        p.priority = 0;
        set.profiles.push_back(p);
    };

    // Broadcast FM channels are ~180 kHz wide on a 100 kHz raster. The minimum detection
    // bandwidth and the raster together are what keep one station from being reported as a
    // handful of narrow fragments.
    add("Broadcast FM", 87.5e6, 108e6, ProfileDemod::WFM, 150e3, 60e3, 400e3, 100e3, 20e3, true);
    // 25 kHz channels. No minimum detection bandwidth: real air traffic is a narrow carrier with
    // weak sidebands, so requiring the measured extent to approach the channel width would reject
    // exactly the quiet transmissions worth catching.
    add("Airband", 118e6, 137e6, ProfileDemod::AM, 10e3, 0.0, 25e3, 25e3, 4e3, true);
    add("2 m voice", 144e6, 146e6, ProfileDemod::NFM, 12.5e3, 0.0, 25e3, 12.5e3, 3e3, true);
    add("70 cm voice", 430e6, 440e6, ProfileDemod::NFM, 12.5e3, 0.0, 25e3, 12.5e3, 3e3, true);
    add("40 m LSB", 7.0e6, 7.3e6, ProfileDemod::LSB, 2.8e3, 1e3, 6e3, 0.0, 500.0, true);
    add("20 m USB", 14.0e6, 14.35e6, ProfileDemod::USB, 2.8e3, 1e3, 6e3, 0.0, 500.0, true);

    return set;
}
