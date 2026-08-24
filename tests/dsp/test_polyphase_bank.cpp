// dsp::multirate::buildPolyphaseBank / freePolyphaseBank.
//
// Every resampler and both clock recovery blocks index into a bank built by
// this function. The tap-to-phase mapping is unusual (phases are stored in
// reverse order), so it is pinned exactly here: a rewrite that "tidies up" the
// ordering silently mirrors every interpolator in the program.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include <dsp/math/hz_to_rads.h>
#include <dsp/multirate/polyphase_bank.h>
#include <dsp/taps/from_array.h>
#include <dsp/taps/tap.h>
#include <dsp/taps/windowed_sinc.h>
#include <dsp/window/nuttall.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    struct ScopedTaps {
        explicit ScopedTaps(dsp::tap<float> t) : taps(t) {}
        ~ScopedTaps() { dsp::taps::free(taps); }
        ScopedTaps(const ScopedTaps&) = delete;
        ScopedTaps& operator=(const ScopedTaps&) = delete;
        dsp::tap<float> taps;
    };
}

TEST_CASE("buildPolyphaseBank splits taps across phases in reverse order", "[dsp][multirate][polyphase]") {
    std::vector<float> raw = { 0, 1, 2, 3, 4, 5, 6, 7 };
    ScopedTaps taps(dsp::taps::fromArray<float>((int)raw.size(), raw.data()));

    auto bank = dsp::multirate::buildPolyphaseBank<float>(4, taps.taps);
    REQUIRE(bank.phaseCount == 4);
    REQUIRE(bank.tapsPerPhase == 2);

    // taps[i] lands in phases[(P-1) - (i % P)][i / P].
    for (int i = 0; i < (int)raw.size(); i++) {
        INFO("tap " << i);
        REQUIRE(bank.phases[3 - (i % 4)][i / 4] == Approx(raw[i]));
    }

    dsp::multirate::freePolyphaseBank(bank);
    REQUIRE(bank.phases == nullptr);
    REQUIRE(bank.phaseCount == 0);
    REQUIRE(bank.tapsPerPhase == 0);
}

TEST_CASE("buildPolyphaseBank zero-pads a non-multiple tap count", "[dsp][multirate][polyphase]") {
    std::vector<float> raw = { 1, 2, 3, 4, 5 };
    ScopedTaps taps(dsp::taps::fromArray<float>((int)raw.size(), raw.data()));

    auto bank = dsp::multirate::buildPolyphaseBank<float>(3, taps.taps);
    REQUIRE(bank.phaseCount == 3);
    REQUIRE(bank.tapsPerPhase == 2); // ceil(5/3)

    // Every stored tap is either one of the originals or a zero pad, and the
    // originals all appear exactly once.
    std::vector<float> seen;
    for (int p = 0; p < bank.phaseCount; p++) {
        for (int t = 0; t < bank.tapsPerPhase; t++) { seen.push_back(bank.phases[p][t]); }
    }
    REQUIRE(seen.size() == 6);
    for (float v : raw) {
        REQUIRE(std::count(seen.begin(), seen.end(), v) == 1);
    }
    REQUIRE(std::count(seen.begin(), seen.end(), 0.0f) == 1);

    dsp::multirate::freePolyphaseBank(bank);
}

TEST_CASE("buildPolyphaseBank with one phase keeps the taps in order", "[dsp][multirate][polyphase]") {
    std::vector<float> raw = { 9, 8, 7, 6 };
    ScopedTaps taps(dsp::taps::fromArray<float>((int)raw.size(), raw.data()));

    auto bank = dsp::multirate::buildPolyphaseBank<float>(1, taps.taps);
    REQUIRE(bank.phaseCount == 1);
    REQUIRE(bank.tapsPerPhase == 4);
    for (int i = 0; i < 4; i++) { REQUIRE(bank.phases[0][i] == Approx(raw[i])); }

    dsp::multirate::freePolyphaseBank(bank);
}

TEST_CASE("buildPolyphaseBank preserves the total gain", "[dsp][multirate][polyphase]") {
    // Splitting an interpolation filter into P phases must not lose or duplicate
    // energy: the sum over all phases equals the sum of the taps.
    const int phaseCount = 8;
    ScopedTaps taps(dsp::taps::windowedSinc<float>(
        phaseCount * 12, dsp::math::hzToRads(0.5 / phaseCount, 1.0), dsp::window::nuttall, phaseCount));

    double tapSum = 0.0;
    for (int i = 0; i < taps.taps.size; i++) { tapSum += taps.taps.taps[i]; }

    auto bank = dsp::multirate::buildPolyphaseBank<float>(phaseCount, taps.taps);
    double bankSum = 0.0;
    for (int p = 0; p < bank.phaseCount; p++) {
        for (int t = 0; t < bank.tapsPerPhase; t++) { bankSum += bank.phases[p][t]; }
    }
    REQUIRE(bankSum == Approx(tapSum).epsilon(1e-5));

    dsp::multirate::freePolyphaseBank(bank);
}

TEST_CASE("freePolyphaseBank is safe to call twice", "[dsp][multirate][polyphase]") {
    std::vector<float> raw = { 1, 2, 3, 4 };
    ScopedTaps taps(dsp::taps::fromArray<float>((int)raw.size(), raw.data()));

    auto bank = dsp::multirate::buildPolyphaseBank<float>(2, taps.taps);
    dsp::multirate::freePolyphaseBank(bank);
    dsp::multirate::freePolyphaseBank(bank); // must be a no-op, not a double free
    REQUIRE(bank.phases == nullptr);
}
