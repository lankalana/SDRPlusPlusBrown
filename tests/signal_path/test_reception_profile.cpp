// Reception profiles and ignore rules: the two things that decide what the automatic subsystem
// is allowed to do with a detection.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <signal_path/ignore_rules.h>
#include <signal_path/reception_profile.h>

#include <cmath>

namespace {
    ReceptionProfile makeProfile(const char* name, double lo, double hi, ProfileDemod demod,
                                 int priority = 0) {
        ReceptionProfile p;
        p.name = name;
        p.minFrequency = lo;
        p.maxFrequency = hi;
        p.demod = demod;
        p.priority = priority;
        return p;
    }
}

// ------------------------------------------------------------------ profiles

TEST_CASE("a profile matches only its own range", "[profiles]") {
    auto p = makeProfile("2 m", 144e6, 146e6, ProfileDemod::NFM);
    CHECK(p.matchesFrequency(145e6));
    CHECK(p.matchesFrequency(144e6)); // inclusive
    CHECK(p.matchesFrequency(146e6));
    CHECK_FALSE(p.matchesFrequency(143.999e6));
    CHECK_FALSE(p.matchesFrequency(146.001e6));
}

TEST_CASE("a profile with no range matches everything", "[profiles]") {
    auto p = makeProfile("Global", 0.0, 0.0, ProfileDemod::NFM);
    CHECK(p.matchesFrequency(1e3));
    CHECK(p.matchesFrequency(1e9));
}

TEST_CASE("the highest priority matching profile wins", "[profiles]") {
    ReceptionProfileSet set;
    set.profiles.push_back(makeProfile("Wide", 144e6, 148e6, ProfileDemod::NFM, 0));
    set.profiles.push_back(makeProfile("Narrow", 145e6, 145.5e6, ProfileDemod::USB, 5));

    REQUIRE(set.findFor(145.2e6) != nullptr);
    CHECK(set.findFor(145.2e6)->name == "Narrow");
    // Outside the specific one, the general one still applies.
    REQUIRE(set.findFor(147e6) != nullptr);
    CHECK(set.findFor(147e6)->name == "Wide");
}

TEST_CASE("equal priorities are broken by declaration order", "[profiles]") {
    ReceptionProfileSet set;
    set.profiles.push_back(makeProfile("First", 144e6, 146e6, ProfileDemod::NFM, 1));
    set.profiles.push_back(makeProfile("Second", 144e6, 146e6, ProfileDemod::AM, 1));
    REQUIRE(set.findFor(145e6) != nullptr);
    CHECK(set.findFor(145e6)->name == "First");
}

TEST_CASE("a disabled profile never matches", "[profiles]") {
    ReceptionProfileSet set;
    auto p = makeProfile("Off", 144e6, 146e6, ProfileDemod::NFM);
    p.enabled = false;
    set.profiles.push_back(p);

    CHECK(set.findFor(145e6) == nullptr);
    CHECK_FALSE(set.covers(145e6));
    CHECK_FALSE(set.anyEnabled());
}

TEST_CASE("detection limits come from the union of matching profiles", "[profiles]") {
    ReceptionProfileSet set;
    auto a = makeProfile("Narrow mode", 144e6, 146e6, ProfileDemod::USB);
    a.minDetectionBandwidth = 1e3;
    a.maxDetectionBandwidth = 6e3;
    auto b = makeProfile("Wide mode", 144e6, 146e6, ProfileDemod::NFM);
    b.minDetectionBandwidth = 5e3;
    b.maxDetectionBandwidth = 25e3;
    set.profiles.push_back(a);
    set.profiles.push_back(b);

    double lo = 0.0, hi = 0.0;
    REQUIRE(set.detectionLimitsAt(145e6, lo, hi));
    // Detection must let through anything either profile could receive.
    CHECK(lo == Catch::Approx(1e3));
    CHECK(hi == Catch::Approx(25e3));
}

TEST_CASE("an unbounded profile makes the union unbounded", "[profiles]") {
    ReceptionProfileSet set;
    auto a = makeProfile("Bounded", 144e6, 146e6, ProfileDemod::NFM);
    a.minDetectionBandwidth = 5e3;
    a.maxDetectionBandwidth = 25e3;
    auto b = makeProfile("Unbounded", 144e6, 146e6, ProfileDemod::RAW);
    b.minDetectionBandwidth = 0.0;
    b.maxDetectionBandwidth = 0.0;
    set.profiles.push_back(a);
    set.profiles.push_back(b);

    double lo = 0.0, hi = 0.0;
    REQUIRE(set.detectionLimitsAt(145e6, lo, hi));
    CHECK(lo == Catch::Approx(0.0));
    CHECK(hi == Catch::Approx(0.0)); // zero means no limit
}

TEST_CASE("detection limits are unavailable outside every profile", "[profiles]") {
    ReceptionProfileSet set;
    set.profiles.push_back(makeProfile("2 m", 144e6, 146e6, ProfileDemod::NFM));

    double lo = 0.0, hi = 0.0;
    CHECK_FALSE(set.detectionLimitsAt(100e6, lo, hi));
}

TEST_CASE("the default profile set covers broadcast FM with a wide minimum", "[profiles]") {
    auto set = ReceptionProfileSet::defaults();
    const auto* fm = set.findFor(98e6);
    REQUIRE(fm != nullptr);
    CHECK(fm->demod == ProfileDemod::WFM);
    // The point of the default: narrow fragments of an FM channel are not signals.
    CHECK(fm->minDetectionBandwidth >= 50e3);
}

// ------------------------------------------------------------ channel raster

TEST_CASE("snapToStep rounds to the nearest multiple", "[profiles]") {
    CHECK(snapToStep(101.087e6, 100e3) == Catch::Approx(101.1e6));
    CHECK(snapToStep(101.049e6, 100e3) == Catch::Approx(101.0e6));
    CHECK(snapToStep(101.051e6, 100e3) == Catch::Approx(101.1e6));
    // Exactly halfway rounds away from zero, consistently.
    CHECK(snapToStep(101.05e6, 100e3) == Catch::Approx(101.1e6));
}

TEST_CASE("a step of zero leaves the frequency alone", "[profiles]") {
    CHECK(snapToStep(14.19512e6, 0.0) == Catch::Approx(14.19512e6));

    auto p = makeProfile("SSB", 14e6, 14.35e6, ProfileDemod::USB);
    p.frequencyStep = 0.0;
    CHECK(p.snapFrequency(14.19512e6) == Catch::Approx(14.19512e6));
}

TEST_CASE("a profile snaps to its own raster", "[profiles]") {
    auto p = makeProfile("FM", 87.5e6, 108e6, ProfileDemod::WFM);
    p.frequencyStep = 100e3;
    // Detected centres wander by tens of kHz; the raster puts the receiver on channel.
    CHECK(p.snapFrequency(103.6937e6) == Catch::Approx(103.7e6));
    CHECK(p.snapFrequency(105.5554e6) == Catch::Approx(105.6e6));

    auto air = makeProfile("Airband", 118e6, 137e6, ProfileDemod::AM);
    air.frequencyStep = 8.33e3;
    CHECK(air.snapFrequency(118.1e6) == Catch::Approx(std::round(118.1e6 / 8.33e3) * 8.33e3));
}

TEST_CASE("the default FM profile uses the 100 kHz raster", "[profiles]") {
    auto set = ReceptionProfileSet::defaults();
    const auto* fm = set.findFor(98e6);
    REQUIRE(fm != nullptr);
    CHECK(fm->frequencyStep == Catch::Approx(100e3));
}

// -------------------------------------------------------------- ignore rules

TEST_CASE("a rule ignores a signal centred inside it", "[ignore]") {
    IgnoreRuleSet set;
    set.rules.push_back({ 433.919e6, 433.921e6, "weather station", true });

    CHECK(set.isIgnored(433.9199e6, 433.9201e6));
    CHECK(set.isIgnored(433.920e6));
    CHECK_FALSE(set.isIgnored(433.930e6));
}

TEST_CASE("a disabled rule ignores nothing", "[ignore]") {
    IgnoreRuleSet set;
    set.rules.push_back({ 433.919e6, 433.921e6, "weather station", false });
    CHECK_FALSE(set.isIgnored(433.920e6));
}

TEST_CASE("a rule covering most of a detection claims it", "[ignore]") {
    IgnoreRuleSet set;
    // A rule written tightly around a carrier still catches the wider detection it produces.
    set.rules.push_back({ 100.000e6, 100.010e6, "spur", true });
    CHECK(set.isIgnored(100.000e6, 100.012e6));
}

TEST_CASE("a rule barely clipping a wide detection does not claim it", "[ignore]") {
    IgnoreRuleSet set;
    set.rules.push_back({ 100.000e6, 100.010e6, "spur", true });
    // 10 kHz of a 200 kHz detection: the transmission is not the spur.
    CHECK_FALSE(set.isIgnored(100.000e6, 100.200e6));
}

TEST_CASE("addForSignal pads the range", "[ignore]") {
    IgnoreRuleSet set;
    set.addForSignal(100.000e6, 100.010e6, "spur", 5e3);

    REQUIRE(set.rules.size() == 1);
    CHECK(set.rules[0].lowerFrequency == Catch::Approx(99.995e6));
    CHECK(set.rules[0].upperFrequency == Catch::Approx(100.015e6));
    CHECK(set.rules[0].reason == "spur");
}

TEST_CASE("addForSignal merges into an overlapping rule", "[ignore]") {
    IgnoreRuleSet set;
    set.addForSignal(100.000e6, 100.010e6, "first", 1e3);
    // The same carrier, drifted a little: one widened rule, not two near-duplicates.
    set.addForSignal(100.005e6, 100.015e6, "second", 1e3);

    REQUIRE(set.rules.size() == 1);
    CHECK(set.rules[0].lowerFrequency == Catch::Approx(99.999e6));
    CHECK(set.rules[0].upperFrequency == Catch::Approx(100.016e6));
}

TEST_CASE("addForSignal keeps unrelated rules separate and sorted", "[ignore]") {
    IgnoreRuleSet set;
    set.addForSignal(200.0e6, 200.001e6, "high", 0.0);
    set.addForSignal(100.0e6, 100.001e6, "low", 0.0);

    REQUIRE(set.rules.size() == 2);
    CHECK(set.rules[0].reason == "low");
    CHECK(set.rules[1].reason == "high");
}

TEST_CASE("find reports which rule matched", "[ignore]") {
    IgnoreRuleSet set;
    set.rules.push_back({ 100.0e6, 100.1e6, "local noise", true });
    const auto* rule = set.find(100.05e6, 100.06e6);
    REQUIRE(rule != nullptr);
    CHECK(rule->reason == "local noise");
    CHECK(set.find(200e6, 200.01e6) == nullptr);
}

TEST_CASE("the shipped Airband raster is exact 25 kHz", "[profiles]") {
    auto set = ReceptionProfileSet::defaults();
    const ReceptionProfile* air = set.findFor(119.1e6);
    REQUIRE(air != nullptr);
    CHECK(air->name == "Airband");
    CHECK(air->frequencyStep == Catch::Approx(25e3));

    // 8330 Hz, which used to ship, is neither the 25 kHz grid nor the real 8.33 kHz one
    // (25/3 kHz), so it drifted by a third of a channel across the band. These are exact now.
    CHECK(air->snapFrequency(119.1e6) == Catch::Approx(119.1e6));
    CHECK(air->snapFrequency(121.5e6) == Catch::Approx(121.5e6));
    CHECK(air->snapFrequency(118.0e6) == Catch::Approx(118.0e6));
    CHECK(snapToStep(118.0e6, 8330.0) != Catch::Approx(118.0e6));
}

TEST_CASE("Airband sets no minimum detection bandwidth", "[profiles]") {
    // Real air traffic is a narrow carrier with weak sidebands. A minimum measured against the
    // above-threshold extent rejects exactly the quiet transmissions worth catching.
    auto set = ReceptionProfileSet::defaults();
    const ReceptionProfile* air = set.findFor(119.1e6);
    REQUIRE(air != nullptr);
    CHECK(air->minDetectionBandwidth == 0.0);
}

TEST_CASE("Broadcast FM keeps its raster and widths", "[profiles]") {
    // The FM band was tuned earlier and must not move.
    auto set = ReceptionProfileSet::defaults();
    const ReceptionProfile* fm = set.findFor(97.2e6);
    REQUIRE(fm != nullptr);
    CHECK(fm->name == "Broadcast FM");
    CHECK(fm->frequencyStep == Catch::Approx(100e3));
    CHECK(fm->bandwidth == Catch::Approx(150e3));
    CHECK(fm->demod == ProfileDemod::WFM);
}

TEST_CASE("every shipped profile sets its own merge gap", "[profiles]") {
    // A single global gap cannot serve both bands: 20 kHz is a broadcast FM figure, and at
    // 2 MSPS it is 164 FFT bins, which on airband welds the whole capture into one run.
    auto set = ReceptionProfileSet::defaults();
    for (const auto& p : set.profiles) {
        INFO("profile " << p.name);
        CHECK(p.mergeGapHz > 0.0);
        // A gap wider than the channel spacing would bridge straight across a neighbour.
        if (p.frequencyStep > 0.0) { CHECK(p.mergeGapHz < p.frequencyStep); }
    }
}
