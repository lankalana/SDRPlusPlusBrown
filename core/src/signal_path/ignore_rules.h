#pragma once
#include <string>
#include <vector>

/**
 * A frequency range that should never be automatically received or recorded.
 *
 * This is how constant interference is suppressed: a pager transmitter, a switching supply
 * harmonic or a local carrier is added here once and stops allocating receivers, while remaining
 * visible so the user can see it is still there.
 */
struct IgnoreRule {
    double lowerFrequency = 0.0;
    double upperFrequency = 0.0;
    std::string reason;
    bool enabled = true;

    bool contains(double frequency) const;

    // True if any part of [lower, upper] falls inside this rule.
    bool overlaps(double lower, double upper) const;
};

class IgnoreRuleSet {
public:
    std::vector<IgnoreRule> rules;

    /**
     * Whether a detected signal should be suppressed. A signal counts as ignored when its centre
     * falls inside a rule, or when a rule covers most of it -- so a rule written around a narrow
     * carrier still catches the slightly wider detection that carrier produces.
     */
    bool isIgnored(double lowerFrequency, double upperFrequency) const;
    bool isIgnored(double centerFrequency) const;

    // The first enabled rule matching, or nullptr. Useful for showing why something is ignored.
    const IgnoreRule* find(double lowerFrequency, double upperFrequency) const;

    /**
     * Add a rule covering a detected signal, padded by `paddingHz` on each side so that normal
     * frequency drift stays inside it. Merges into an existing overlapping rule rather than
     * accumulating near-duplicates.
     */
    void addForSignal(double lowerFrequency, double upperFrequency, const std::string& reason,
                      double paddingHz = 0.0);

    // Fraction of a detection that must be covered for a rule to claim it.
    static constexpr double COVERAGE_THRESHOLD = 0.5;
};
