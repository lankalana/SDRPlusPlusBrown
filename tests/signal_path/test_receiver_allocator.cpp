// Which automatic receiver serves which signal.
//
// Only the decision half is covered here: planAllocation() is pure, while applying a plan creates
// radio instances and touches the waterfall, which needs a running GUI.

#include <catch2/catch_test_macros.hpp>

#include <signal_path/receiver_allocator.h>
#include <signal_path/vfo_manager.h>

#include <vector>

namespace {
    const double CENTER = 100e6;
    const double USABLE = 2e6; // [99, 101] MHz

    AllocationRequest request(uint64_t id, double tune, double bandwidth = 12.5e3,
                              ProfileDemod demod = ProfileDemod::NFM) {
        AllocationRequest r;
        r.signalId = id;
        r.tuneFrequency = tune;
        r.channelFrequency = tune;
        r.bandwidth = bandwidth;
        r.demod = demod;
        r.reference = referenceForDemod(demod);
        return r;
    }

    AutoReceiverSlot activeSlot(const char* name, uint64_t signalId) {
        AutoReceiverSlot s;
        s.name = name;
        s.state = AutoReceiverSlot::ACTIVE;
        s.signalId = signalId;
        return s;
    }

    AutoReceiverSlot idleSlot(const char* name) {
        AutoReceiverSlot s;
        s.name = name;
        s.state = AutoReceiverSlot::IDLE;
        return s;
    }

    AllocationPlan plan(const std::vector<AutoReceiverSlot>& slots,
                        const std::vector<AllocationRequest>& requests, int maxReceivers = 0) {
        return planAllocation(slots, requests, maxReceivers, CENTER, USABLE);
    }
}

TEST_CASE("one signal with no receivers asks for one", "[allocator]") {
    auto p = plan({}, { request(1, 100e6) });
    CHECK(p.slotAssignments.empty());
    REQUIRE(p.newSlots.size() == 1);
    CHECK(p.newSlots[0].signalId == 1);
    CHECK(p.rejected.empty());
}

TEST_CASE("simultaneous signals each get their own receiver", "[allocator]") {
    auto p = plan({}, { request(1, 99.5e6), request(2, 100.0e6), request(3, 100.5e6) });
    CHECK(p.newSlots.size() == 3);
    CHECK(p.rejected.empty());
}

TEST_CASE("an ongoing signal stays on its receiver", "[allocator]") {
    std::vector<AutoReceiverSlot> slots = { activeSlot("AUTO1", 7), idleSlot("AUTO2") };

    // A second signal appears; the first must not be moved to make room.
    auto p = plan(slots, { request(9, 100.5e6), request(7, 100.0e6) });

    REQUIRE(p.slotAssignments.size() == 2);
    REQUIRE(p.slotAssignments[0].has_value());
    CHECK(p.slotAssignments[0]->signalId == 7);
    REQUIRE(p.slotAssignments[1].has_value());
    CHECK(p.slotAssignments[1]->signalId == 9);
    CHECK(p.newSlots.empty());
}

TEST_CASE("an idle receiver is reused before a new one is created", "[allocator]") {
    std::vector<AutoReceiverSlot> slots = { idleSlot("AUTO1") };
    auto p = plan(slots, { request(1, 100e6) });

    REQUIRE(p.slotAssignments.size() == 1);
    CHECK(p.slotAssignments[0].has_value());
    CHECK(p.newSlots.empty());
}

TEST_CASE("a receiver whose signal ended is released", "[allocator]") {
    std::vector<AutoReceiverSlot> slots = { activeSlot("AUTO1", 7) };
    auto p = plan(slots, {});

    REQUIRE(p.slotAssignments.size() == 1);
    CHECK_FALSE(p.slotAssignments[0].has_value());
}

TEST_CASE("the receiver limit caps allocation", "[allocator]") {
    auto p = plan({}, { request(1, 99.5e6), request(2, 100.0e6), request(3, 100.5e6) }, 2);
    CHECK(p.newSlots.size() == 2);
    REQUIRE(p.rejected.size() == 1);
    CHECK(p.rejected[0].signalId == 3);
}

TEST_CASE("the limit counts receivers already serving a signal", "[allocator]") {
    std::vector<AutoReceiverSlot> slots = { activeSlot("AUTO1", 1), activeSlot("AUTO2", 2) };
    auto p = plan(slots, { request(1, 99.5e6), request(2, 100.0e6), request(3, 100.5e6) }, 2);

    CHECK(p.slotAssignments[0].has_value());
    CHECK(p.slotAssignments[1].has_value());
    CHECK(p.newSlots.empty());
    CHECK(p.rejected.size() == 1);
}

TEST_CASE("a limit of zero is unlimited", "[allocator]") {
    auto p = plan({}, { request(1, 99.5e6), request(2, 100.0e6), request(3, 100.5e6) }, 0);
    CHECK(p.newSlots.size() == 3);
    CHECK(p.rejected.empty());
}

TEST_CASE("a signal whose passband leaves the capture is rejected", "[allocator]") {
    // 200 kHz wide, centred 50 kHz from the top edge: the upper half falls outside.
    auto p = plan({}, { request(1, 100.95e6, 200e3, ProfileDemod::WFM) });
    CHECK(p.newSlots.empty());
    REQUIRE(p.rejected.size() == 1);
    CHECK(p.rejected[0].signalId == 1);
}

TEST_CASE("sideband references are respected at the spectrum edge", "[allocator]") {
    // USB occupies the 3 kHz above its tune frequency, so it fits right at the bottom edge...
    auto usb = plan({}, { request(1, 99.0e6, 3e3, ProfileDemod::USB) });
    CHECK(usb.newSlots.size() == 1);
    CHECK(usb.rejected.empty());

    // ...but LSB at the same frequency would need the 3 kHz below it, which is outside.
    auto lsb = plan({}, { request(1, 99.0e6, 3e3, ProfileDemod::LSB) });
    CHECK(lsb.newSlots.empty());
    CHECK(lsb.rejected.size() == 1);
}

TEST_CASE("a rejected signal does not consume a receiver", "[allocator]") {
    // The first cannot fit; the second must still get the one receiver allowed.
    auto p = plan({}, { request(1, 100.99e6, 200e3, ProfileDemod::WFM), request(2, 100.0e6) }, 1);
    REQUIRE(p.newSlots.size() == 1);
    CHECK(p.newSlots[0].signalId == 2);
    REQUIRE(p.rejected.size() == 1);
    CHECK(p.rejected[0].signalId == 1);
}

TEST_CASE("referenceForDemod maps sidebands to the right edge", "[allocator]") {
    CHECK(referenceForDemod(ProfileDemod::USB) == ImGui::WaterfallVFO::REF_LOWER);
    CHECK(referenceForDemod(ProfileDemod::CW) == ImGui::WaterfallVFO::REF_LOWER);
    CHECK(referenceForDemod(ProfileDemod::LSB) == ImGui::WaterfallVFO::REF_UPPER);
    CHECK(referenceForDemod(ProfileDemod::NFM) == ImGui::WaterfallVFO::REF_CENTER);
    CHECK(referenceForDemod(ProfileDemod::WFM) == ImGui::WaterfallVFO::REF_CENTER);
    CHECK(referenceForDemod(ProfileDemod::AM) == ImGui::WaterfallVFO::REF_CENTER);
}

TEST_CASE("releasing and reusing the same slot keeps the pool small", "[allocator]") {
    // One transmission ends, another starts: the plan reuses rather than growing.
    std::vector<AutoReceiverSlot> slots = { activeSlot("AUTO1", 1) };

    auto ended = plan(slots, {});
    REQUIRE(ended.slotAssignments.size() == 1);
    CHECK_FALSE(ended.slotAssignments[0].has_value());

    slots[0] = idleSlot("AUTO1");
    auto next = plan(slots, { request(2, 100.2e6) });
    REQUIRE(next.slotAssignments[0].has_value());
    CHECK(next.slotAssignments[0]->signalId == 2);
    CHECK(next.newSlots.empty());
}
