// Receiver ownership naming and fixed-center placement validation.
//
// These are the two pure pieces of the multi-receiver foundation:
//   * receivers::ownerFromName / makeName decide which receivers the automatic
//     reception subsystem is allowed to touch, purely from the name;
//   * VFOManager::passbandFitsInSpectrum answers "does this receiver fit inside
//     the spectrum the user has tuned?" without ever consulting the source.
//
// receivers::nextFreeName is not covered here: it queries the global module
// manager and VFO manager, which need a running GUI.

#include <catch2/catch_test_macros.hpp>

#include <signal_path/receiver.h>
#include <signal_path/vfo_manager.h>

TEST_CASE("ownerFromName treats AUTO<n> as automatic", "[receiver]") {
    CHECK(receivers::ownerFromName("AUTO1") == ReceiverOwner::AUTOMATIC);
    CHECK(receivers::ownerFromName("AUTO12") == ReceiverOwner::AUTOMATIC);
}

TEST_CASE("ownerFromName treats everything else as manual", "[receiver]") {
    CHECK(receivers::ownerFromName("RX1") == ReceiverOwner::MANUAL);
    CHECK(receivers::ownerFromName("Radio") == ReceiverOwner::MANUAL);
    CHECK(receivers::ownerFromName("") == ReceiverOwner::MANUAL);

    // A bare prefix, or a prefix with a non-numeric suffix, is a user name.
    CHECK(receivers::ownerFromName("AUTO") == ReceiverOwner::MANUAL);
    CHECK(receivers::ownerFromName("AUTOMATIC") == ReceiverOwner::MANUAL);
    CHECK(receivers::ownerFromName("AUTO1b") == ReceiverOwner::MANUAL);
}

TEST_CASE("makeName round-trips through ownerFromName", "[receiver]") {
    CHECK(receivers::makeName(ReceiverOwner::MANUAL, 3) == "RX3");
    CHECK(receivers::makeName(ReceiverOwner::AUTOMATIC, 3) == "AUTO3");

    for (int i = 1; i <= 20; i++) {
        CHECK(receivers::ownerFromName(receivers::makeName(ReceiverOwner::MANUAL, i)) == ReceiverOwner::MANUAL);
        CHECK(receivers::ownerFromName(receivers::makeName(ReceiverOwner::AUTOMATIC, i)) == ReceiverOwner::AUTOMATIC);
    }
}

// Usable spectrum for the cases below: 100.0 MHz center, 2 MHz wide, so
// [99.0 MHz, 101.0 MHz].
static constexpr double CENTER = 100e6;
static constexpr double USABLE = 2e6;

static bool fits(double tune, double bw, int reference) {
    return VFOManager::passbandFitsInSpectrum(CENTER, USABLE, tune, bw, reference);
}

TEST_CASE("center-referenced passbands must fit symmetrically", "[receiver]") {
    const int REF = ImGui::WaterfallVFO::REF_CENTER;

    CHECK(fits(CENTER, 12500.0, REF));

    // Exactly touching either edge still fits.
    CHECK(fits(99e6 + 6250.0, 12500.0, REF));
    CHECK(fits(101e6 - 6250.0, 12500.0, REF));

    // One hertz further out does not.
    CHECK_FALSE(fits(99e6 + 6249.0, 12500.0, REF));
    CHECK_FALSE(fits(101e6 - 6249.0, 12500.0, REF));

    // A bandwidth wider than the capture never fits, wherever it is placed.
    CHECK_FALSE(fits(CENTER, USABLE + 1.0, REF));
}

TEST_CASE("USB occupies the spectrum above the tune frequency", "[receiver]") {
    const int REF = ImGui::WaterfallVFO::REF_LOWER;

    // Tuned at the very bottom edge: the whole 2.8 kHz passband is above it.
    CHECK(fits(99e6, 2800.0, REF));
    CHECK(fits(101e6 - 2800.0, 2800.0, REF));

    // Just below the bottom edge, or too close to the top edge, does not fit.
    CHECK_FALSE(fits(99e6 - 1.0, 2800.0, REF));
    CHECK_FALSE(fits(101e6 - 2799.0, 2800.0, REF));
}

TEST_CASE("LSB occupies the spectrum below the tune frequency", "[receiver]") {
    const int REF = ImGui::WaterfallVFO::REF_UPPER;

    CHECK(fits(101e6, 2800.0, REF));
    CHECK(fits(99e6 + 2800.0, 2800.0, REF));

    CHECK_FALSE(fits(101e6 + 1.0, 2800.0, REF));
    CHECK_FALSE(fits(99e6 + 2799.0, 2800.0, REF));
}

TEST_CASE("sideband references are not interchangeable at the edges", "[receiver]") {
    // The bottom edge is reachable by USB but not by LSB, and vice versa at the top.
    CHECK(fits(99e6, 2800.0, ImGui::WaterfallVFO::REF_LOWER));
    CHECK_FALSE(fits(99e6, 2800.0, ImGui::WaterfallVFO::REF_UPPER));

    CHECK(fits(101e6, 2800.0, ImGui::WaterfallVFO::REF_UPPER));
    CHECK_FALSE(fits(101e6, 2800.0, ImGui::WaterfallVFO::REF_LOWER));
}
