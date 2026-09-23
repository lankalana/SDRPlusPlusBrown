#pragma once
#include <string>
#include <vector>

/**
 * Demodulators, mirroring the radio module's DemodID so that core can talk about modes without
 * depending on the radio module's headers. Kept in the same order.
 */
enum class ProfileDemod {
    NFM,
    WFM,
    AM,
    DSB,
    USB,
    CW,
    LSB,
    RAW,
    _COUNT
};

const char* toString(ProfileDemod demod);

// "NFM\0WFM\0AM\0..." for ImGui::Combo.
const char* profileDemodComboItems();

/**
 * What to do with a signal detected in a given frequency range: which demodulator to use, how
 * wide to make the receiver, and what counts as a signal worth receiving at all.
 *
 * Also the answer to "why is the detector finding lots of narrow things": the bandwidth limits
 * here are what stops a wide transmission's instantaneous notches being reported as a scatter of
 * separate signals.
 */
struct ReceptionProfile {
    std::string name;
    bool enabled = true;

    // Inclusive range this profile applies to. An empty range (both zero) matches everything.
    double minFrequency = 0.0;
    double maxFrequency = 0.0;

    ProfileDemod demod = ProfileDemod::NFM;
    double bandwidth = 12500.0;

    // Detection limits, in Hz. A detected signal outside these is not receivable under this
    // profile. Zero means "no limit".
    double minDetectionBandwidth = 0.0;
    double maxDetectionBandwidth = 0.0;

    /**
     * Channel raster, in Hz. Zero disables rounding.
     *
     * On a channelized band this does two jobs. It puts the receiver exactly on channel instead
     * of on the detected centre, which for a modulated carrier wanders by tens of kHz. And it
     * lets several fragments of one transmission be recognised as the same channel and merged,
     * rather than being reported as a crowd of narrow signals.
     */
    double frequencyStep = 0.0;

    // Highest priority wins when several profiles match.
    int priority = 0;

    bool matchesFrequency(double frequency) const;

    // `frequency` rounded to the nearest multiple of frequencyStep, or unchanged if no step.
    double snapFrequency(double frequency) const;
};

// Nearest multiple of `step` (from 0 Hz), or `frequency` unchanged when step <= 0.
double snapToStep(double frequency, double step);

/**
 * An ordered set of profiles, plus the lookup the allocator uses.
 */
class ReceptionProfileSet {
public:
    std::vector<ReceptionProfile> profiles;

    /**
     * The enabled profile covering `frequency` with the highest priority, or nullptr if none
     * does. Ties are broken by declaration order, so the list itself is a visible tiebreak.
     */
    const ReceptionProfile* findFor(double frequency) const;

    /**
     * Detection limits to apply around `frequency`: the widest window any profile there would
     * accept. Detection is profile-independent, so the union is the correct bound -- narrowing
     * per profile happens when a receiver is allocated.
     *
     * Returns false if no enabled profile covers the frequency at all.
     */
    bool detectionLimitsAt(double frequency, double& minBandwidth, double& maxBandwidth) const;

    // True if any enabled profile covers the frequency.
    bool covers(double frequency) const;

    bool anyEnabled() const;

    // A sensible starting set, so the panel is not empty on first run.
    static ReceptionProfileSet defaults();
};
