#pragma once
#include "auto_receiver.h"
#include "receiver.h"
#include "reception_profile.h"
#include <optional>
#include <string>
#include <vector>

/**
 * What a detected signal needs from a receiver.
 */
struct AllocationRequest {
    uint64_t signalId = 0;
    double channelFrequency = 0.0;
    double tuneFrequency = 0.0;
    double bandwidth = 0.0;
    ProfileDemod demod = ProfileDemod::NFM;

    // ImGui::WaterfallVFO reference implied by the demodulator, for the fit check.
    int reference = 1; // REF_CENTER
};

/**
 * One automatic receiver: a radio module instance plus its dedicated recorder.
 */
struct AutoReceiverSlot {
    enum State {
        IDLE,
        ACTIVE
    };

    std::string name;         // AUTO1, AUTO2, ...
    std::string recorderName; // REC_AUTO1, ...
    State state = IDLE;

    uint64_t signalId = 0;
    double tuneFrequency = 0.0;
    double bandwidth = 0.0;
    ProfileDemod demod = ProfileDemod::NFM;

    bool recording = false;
    uint64_t recordingStartedMs = 0;
    // Set when a recording could not be started, so it is not retried every frame.
    bool recordingFailed = false;
    uint64_t idleSinceMs = 0;
};

/**
 * The decision half of allocation, kept free of modules and GUI so it can be tested directly.
 *
 * `slotAssignments` is parallel to the slots passed in: a value means "serve this request",
 * nullopt means "release this slot". Requests that need a receiver that does not exist yet land
 * in `newSlots`, and those that cannot be served at all in `rejected`.
 */
struct AllocationPlan {
    std::vector<std::optional<AllocationRequest>> slotAssignments;
    std::vector<AllocationRequest> newSlots;
    std::vector<AllocationRequest> rejected;
};

/**
 * Decide which receiver serves which signal.
 *
 * A slot already serving a signal keeps it, so an ongoing transmission is never handed to a
 * different receiver mid-way. Otherwise idle slots are reused before new ones are created --
 * creating and destroying radio instances per transmission would be far more expensive than
 * keeping a small pool.
 *
 * `maxReceivers` of 0 means unlimited. A request whose passband does not fit inside
 * [centerFrequency +/- usableBandwidth/2] is rejected rather than served badly, and the source is
 * never retuned to make it fit.
 */
AllocationPlan planAllocation(const std::vector<AutoReceiverSlot>& slots,
                              const std::vector<AllocationRequest>& requests, int maxReceivers,
                              double centerFrequency, double usableBandwidth);

// The waterfall VFO reference a demodulator implies.
int referenceForDemod(ProfileDemod demod);

/**
 * Creates, tunes and records automatic receivers.
 *
 * Everything here runs on the GUI thread: creating a radio instance touches the module manager,
 * the VFO manager and the waterfall, none of which are safe to poke from the DSP thread. The
 * detection side hands over a snapshot and this decides what to do with it.
 *
 * Manual receivers are never touched: only instances this class created, named AUTO<n>, are ever
 * retuned, reconfigured or deleted.
 */
class ReceiverAllocator {
public:
    struct Config {
        bool allocateReceivers = false;
        bool recordAudio = false;
        int maxReceivers = 4;
        std::string recordingPath = "%ROOT%/recordings/automatic";
        // Put each day's recordings in their own subdirectory.
        bool dateSubfolders = true;
        // Remove an idle receiver after this long. 0 keeps the pool forever.
        uint64_t idleTimeoutMs = 60000;
        // Don't keep a recording shorter than this; it is almost certainly a false trigger.
        uint64_t minRecordingMs = 1000;
    };

    void setConfig(const Config& config);
    Config getConfig() const { return config; }

    /**
     * Reconcile the receiver pool against the current detections. Call once per GUI frame.
     */
    void update(const std::vector<AutoReceiverManager::ClassifiedSignal>& signals, uint64_t nowMs);

    // Stop every recording and release every receiver, keeping the instances.
    void releaseAll();

    // Release and delete every automatic receiver and recorder.
    void destroyAll();

    const std::vector<AutoReceiverSlot>& getSlots() const { return slots; }

    // Why the last update could not do its job, for the UI. Empty when fine.
    const std::string& getStatusMessage() const { return statusMessage; }

private:
    bool createSlot(const AllocationRequest& request, uint64_t nowMs);
    bool ensureRecorder(AutoReceiverSlot& slot);
    void applyToSlot(AutoReceiverSlot& slot, const AllocationRequest& request);
    void releaseSlot(AutoReceiverSlot& slot, uint64_t nowMs);
    void startRecording(AutoReceiverSlot& slot, uint64_t nowMs);
    void stopRecording(AutoReceiverSlot& slot);
    void pruneIdleSlots(uint64_t nowMs);
    std::string resolveRecordingFolder() const;

    Config config;
    std::vector<AutoReceiverSlot> slots;
    std::string statusMessage;
};
