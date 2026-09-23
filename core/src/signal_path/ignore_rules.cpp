#include <signal_path/ignore_rules.h>
#include <algorithm>

bool IgnoreRule::contains(double frequency) const {
    return frequency >= lowerFrequency && frequency <= upperFrequency;
}

bool IgnoreRule::overlaps(double lower, double upper) const {
    return upper >= lowerFrequency && lower <= upperFrequency;
}

const IgnoreRule* IgnoreRuleSet::find(double lowerFrequency, double upperFrequency) const {
    double center = (lowerFrequency + upperFrequency) / 2.0;
    double width = upperFrequency - lowerFrequency;

    for (const auto& rule : rules) {
        if (!rule.enabled) { continue; }
        if (rule.contains(center)) { return &rule; }

        if (width > 0.0) {
            double lo = std::max(lowerFrequency, rule.lowerFrequency);
            double hi = std::min(upperFrequency, rule.upperFrequency);
            if ((hi - lo) / width >= COVERAGE_THRESHOLD) { return &rule; }
        }
    }
    return nullptr;
}

bool IgnoreRuleSet::isIgnored(double lowerFrequency, double upperFrequency) const {
    return find(lowerFrequency, upperFrequency) != nullptr;
}

bool IgnoreRuleSet::isIgnored(double centerFrequency) const {
    return find(centerFrequency, centerFrequency) != nullptr;
}

void IgnoreRuleSet::addForSignal(double lowerFrequency, double upperFrequency,
                                 const std::string& reason, double paddingHz) {
    double lo = lowerFrequency - paddingHz;
    double hi = upperFrequency + paddingHz;

    // Absorb any existing rules this one touches, so repeatedly ignoring the same drifting
    // carrier widens one rule instead of piling up near-duplicates.
    for (auto it = rules.begin(); it != rules.end();) {
        if (it->enabled && it->overlaps(lo, hi)) {
            lo = std::min(lo, it->lowerFrequency);
            hi = std::max(hi, it->upperFrequency);
            it = rules.erase(it);
        }
        else {
            ++it;
        }
    }

    IgnoreRule rule;
    rule.lowerFrequency = lo;
    rule.upperFrequency = hi;
    rule.reason = reason;
    rule.enabled = true;
    rules.push_back(rule);

    std::sort(rules.begin(), rules.end(), [](const IgnoreRule& a, const IgnoreRule& b) {
        return a.lowerFrequency < b.lowerFrequency;
    });
}
