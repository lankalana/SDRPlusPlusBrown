#include <signal_path/receiver_allocator.h>
#include <signal_path/signal_path.h>
#include <gui/gui.h>
#include <core.h>
#include <utils/flog.h>
#include <algorithm>
#include <ctime>
#include <filesystem>

#include "../../../decoder_modules/radio/src/radio_interface.h"
#include "../../../misc_modules/recorder/src/recorder_interface.h"

static const char* RADIO_MODULE = "radio";
static const char* RECORDER_MODULE = "recorder";

int referenceForDemod(ProfileDemod demod) {
    switch (demod) {
    case ProfileDemod::USB:
    case ProfileDemod::CW:
        return ImGui::WaterfallVFO::REF_LOWER;
    case ProfileDemod::LSB:
        return ImGui::WaterfallVFO::REF_UPPER;
    default:
        return ImGui::WaterfallVFO::REF_CENTER;
    }
}

AllocationPlan planAllocation(const std::vector<AutoReceiverSlot>& slots,
                              const std::vector<AllocationRequest>& requests, int maxReceivers,
                              double centerFrequency, double usableBandwidth) {
    AllocationPlan plan;
    plan.slotAssignments.assign(slots.size(), std::nullopt);

    std::vector<bool> requestHandled(requests.size(), false);

    // Anything that cannot physically be received inside the captured spectrum is out before any
    // receiver is considered. The source is the user's; we do not retune it to make room.
    for (size_t r = 0; r < requests.size(); r++) {
        const auto& req = requests[r];
        if (!VFOManager::passbandFitsInSpectrum(centerFrequency, usableBandwidth, req.tuneFrequency,
                                                req.bandwidth, req.reference)) {
            plan.rejected.push_back(req);
            requestHandled[r] = true;
        }
    }

    // A slot already serving a signal keeps it, so a transmission in progress is never moved to a
    // different receiver.
    for (size_t s = 0; s < slots.size(); s++) {
        if (slots[s].state != AutoReceiverSlot::ACTIVE) { continue; }
        for (size_t r = 0; r < requests.size(); r++) {
            if (requestHandled[r]) { continue; }
            if (requests[r].signalId == slots[s].signalId) {
                plan.slotAssignments[s] = requests[r];
                requestHandled[r] = true;
                break;
            }
        }
    }

    int inUse = 0;
    for (const auto& a : plan.slotAssignments) {
        if (a.has_value()) { inUse++; }
    }

    auto roomLeft = [&]() {
        return maxReceivers <= 0 || inUse < maxReceivers;
    };

    // Reuse an idle receiver before building another one.
    for (size_t r = 0; r < requests.size(); r++) {
        if (requestHandled[r]) { continue; }
        if (!roomLeft()) { break; }

        for (size_t s = 0; s < slots.size(); s++) {
            if (plan.slotAssignments[s].has_value()) { continue; }
            plan.slotAssignments[s] = requests[r];
            requestHandled[r] = true;
            inUse++;
            break;
        }
    }

    // Whatever is left needs a receiver that does not exist yet.
    for (size_t r = 0; r < requests.size(); r++) {
        if (requestHandled[r]) { continue; }
        if (!roomLeft()) {
            plan.rejected.push_back(requests[r]);
            continue;
        }
        plan.newSlots.push_back(requests[r]);
        inUse++;
    }

    return plan;
}

// ----------------------------------------------------------------- allocator

void ReceiverAllocator::setConfig(const Config& newConfig) {
    bool wasAllocating = config.allocateReceivers;
    config = newConfig;
    if (wasAllocating && !config.allocateReceivers) { destroyAll(); }
}

static bool moduleAvailable(const char* type) {
    return core::moduleManager.modules.find(type) != core::moduleManager.modules.end();
}

std::string ReceiverAllocator::resolveRecordingFolder() const {
    std::string folder = config.recordingPath;
    if (config.dateSubfolders) {
        time_t now = time(nullptr);
        tm* lt = localtime(&now);
        char date[32];
        strftime(date, sizeof date, "%Y-%m-%d", lt);
        folder += "/";
        folder += date;
    }

    // Create it now rather than leaving the recorder to discover a missing directory: the folder
    // widget marks an absent path invalid, and a dated subdirectory never exists on its first use.
    std::string expanded = folder;
    size_t rootPos = expanded.find("%ROOT%");
    if (rootPos != std::string::npos) {
        expanded.replace(rootPos, 6, std::string(core::getRoot()));
    }
    try {
        if (!std::filesystem::exists(expanded)) { std::filesystem::create_directories(expanded); }
    }
    catch (const std::exception& e) {
        flog::error("Automatic reception: cannot create {}: {}", expanded, e.what());
    }
    return folder;
}

// The recorder must not be bound before the receiver's audio stream is registered: the recorder
// silently falls back to the first available stream, which would record a different receiver.
bool ReceiverAllocator::ensureRecorder(AutoReceiverSlot& slot) {
    if (!slot.recorderName.empty()) { return true; }
    if (!config.recordAudio || !moduleAvailable(RECORDER_MODULE)) { return false; }

    auto streams = sigpath::sinkManager.getStreamNames();
    if (std::find(streams.begin(), streams.end(), slot.name) == streams.end()) {
        // The radio instance has not published its audio yet; try again next frame.
        return false;
    }

    std::string recorderName = "REC_" + slot.name;
    if (core::moduleManager.createInstance(recorderName, RECORDER_MODULE) != 0) {
        flog::error("Automatic reception: could not create recorder for {}", slot.name);
        return false;
    }
    core::moduleManager.postInit(recorderName);

    int mode = RECORDER_MODE_AUDIO;
    core::modComManager.callInterface(recorderName, RECORDER_IFACE_CMD_SET_MODE, &mode, NULL);
    core::modComManager.callInterface(recorderName, RECORDER_IFACE_CMD_SET_STREAM,
                                      (void*)slot.name.c_str(), NULL);
    slot.recorderName = recorderName;
    return true;
}

bool ReceiverAllocator::createSlot(const AllocationRequest& request, uint64_t nowMs) {
    if (!moduleAvailable(RADIO_MODULE)) {
        statusMessage = "The radio module is not loaded.";
        return false;
    }

    AutoReceiverSlot slot;
    slot.name = receivers::nextFreeName(ReceiverOwner::AUTOMATIC);

    if (core::moduleManager.createInstance(slot.name, RADIO_MODULE) != 0) {
        statusMessage = "Could not create " + slot.name;
        return false;
    }
    core::moduleManager.postInit(slot.name);
    flog::info("Automatic reception: created receiver {}", slot.name);

    slot.idleSinceMs = nowMs;
    slots.push_back(slot);
    applyToSlot(slots.back(), request);
    return true;
}

void ReceiverAllocator::applyToSlot(AutoReceiverSlot& slot, const AllocationRequest& request) {
    // Mode first: changing the demodulator resets the bandwidth limits.
    if (slot.demod != request.demod || slot.state == AutoReceiverSlot::IDLE) {
        int mode = (int)request.demod;
        core::modComManager.callInterface(slot.name, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
        slot.demod = request.demod;
    }

    if (slot.bandwidth != request.bandwidth) {
        float bw = (float)request.bandwidth;
        core::modComManager.callInterface(slot.name, RADIO_IFACE_CMD_SET_BANDWIDTH, &bw, NULL);
        slot.bandwidth = request.bandwidth;
    }

    // Only the VFO offset moves. The SDR centre frequency belongs to the user.
    sigpath::vfoManager.setAbsoluteFrequency(slot.name, gui::waterfall.getCenterFrequency(),
                                             request.tuneFrequency);
    sigpath::vfoManager.setStatusText(slot.name, slot.recording ? "REC" : "");

    slot.signalId = request.signalId;
    slot.tuneFrequency = request.tuneFrequency;
    slot.state = AutoReceiverSlot::ACTIVE;
}

void ReceiverAllocator::startRecording(AutoReceiverSlot& slot, uint64_t nowMs) {
    if (slot.recording) { return; }
    if (!ensureRecorder(slot)) { return; }
    if (!core::modComManager.interfaceExists(slot.recorderName)) { return; }

    std::string folder = resolveRecordingFolder();
    core::modComManager.callInterface(slot.recorderName, RECORDER_IFACE_CMD_SET_PATH,
                                      (void*)folder.c_str(), NULL);

    // date_time_frequency_mode_receiver, with the receiver name literal because this recorder is
    // dedicated to one slot.
    std::string templ = "$y$M$d_$h$m$s_$f_$r_" + slot.name;
    core::modComManager.callInterface(slot.recorderName, RECORDER_IFACE_CMD_SET_NAME_TEMPLATE,
                                      (void*)templ.c_str(), NULL);
    core::modComManager.callInterface(slot.recorderName, RECORDER_IFACE_CMD_START, NULL, NULL);

    bool recording = false;
    core::modComManager.callInterface(slot.recorderName, RECORDER_IFACE_CMD_IS_RECORDING, NULL,
                                      &recording);
    slot.recording = recording;
    slot.recordingStartedMs = nowMs;
    if (recording) {
        sigpath::vfoManager.setStatusText(slot.name, "REC");
        flog::info("Automatic reception: {} recording {} Hz", slot.name, slot.tuneFrequency);
    }
    else {
        // Don't hammer the recorder every frame if it refuses to open a file.
        slot.recordingFailed = true;
        flog::error("Automatic reception: {} could not start recording", slot.name);
    }
}

void ReceiverAllocator::stopRecording(AutoReceiverSlot& slot) {
    if (!slot.recording) { return; }
    core::modComManager.callInterface(slot.recorderName, RECORDER_IFACE_CMD_STOP, NULL, NULL);
    slot.recording = false;
    slot.recordingStartedMs = 0;
    sigpath::vfoManager.setStatusText(slot.name, "");
}

void ReceiverAllocator::releaseSlot(AutoReceiverSlot& slot, uint64_t nowMs) {
    if (slot.state == AutoReceiverSlot::IDLE) { return; }
    stopRecording(slot);
    slot.state = AutoReceiverSlot::IDLE;
    slot.signalId = 0;
    slot.recordingFailed = false;
    slot.idleSinceMs = nowMs;
}

void ReceiverAllocator::pruneIdleSlots(uint64_t nowMs) {
    if (config.idleTimeoutMs == 0) { return; }

    for (auto it = slots.begin(); it != slots.end();) {
        if (it->state == AutoReceiverSlot::IDLE && (nowMs - it->idleSinceMs) >= config.idleTimeoutMs) {
            flog::info("Automatic reception: removing idle receiver {}", it->name);
            if (!it->recorderName.empty()) { core::moduleManager.deleteInstance(it->recorderName); }
            core::moduleManager.deleteInstance(it->name);
            it = slots.erase(it);
        }
        else {
            ++it;
        }
    }
}

void ReceiverAllocator::update(const std::vector<AutoReceiverManager::ClassifiedSignal>& signals,
                               uint64_t nowMs) {
    if (!config.allocateReceivers) {
        if (!slots.empty()) { destroyAll(); }
        return;
    }
    statusMessage.clear();

    // Only confirmed, receivable, non-ignored signals with a profile get a receiver. A candidate
    // has not proven itself yet, and without a profile there is no mode or bandwidth to use.
    std::vector<AllocationRequest> requests;
    for (const auto& cs : signals) {
        if (cs.ignored || !cs.hasProfile) { continue; }
        if (cs.tracked.state != dsp::detector::SignalState::ACTIVE &&
            cs.tracked.state != dsp::detector::SignalState::RELEASING) {
            continue;
        }
        AllocationRequest req;
        req.signalId = cs.tracked.signal.id;
        req.channelFrequency = cs.tracked.signal.channelFrequency;
        req.tuneFrequency = cs.tuneFrequency;
        req.bandwidth = cs.receiverBandwidth;
        req.demod = cs.demod;
        req.reference = referenceForDemod(cs.demod);
        requests.push_back(req);
    }

    double center = gui::waterfall.getCenterFrequency();
    double usable = gui::waterfall.getBandwidth() * gui::waterfall.getUsableSpectrumRatio();

    auto plan = planAllocation(slots, requests, config.maxReceivers, center, usable);

    for (size_t s = 0; s < slots.size() && s < plan.slotAssignments.size(); s++) {
        if (plan.slotAssignments[s].has_value()) {
            applyToSlot(slots[s], *plan.slotAssignments[s]);
            // Retried each frame while the receiver is active: the recorder cannot be bound until
            // the radio instance has published its audio stream, which is not immediate.
            if (config.recordAudio && !slots[s].recording && !slots[s].recordingFailed) {
                startRecording(slots[s], nowMs);
            }
        }
        else {
            // The release delay already elapsed inside the tracker before the signal disappeared
            // from the list, so a short recording here really is a false trigger.
            if (slots[s].recording && config.minRecordingMs > 0 &&
                (nowMs - slots[s].recordingStartedMs) < config.minRecordingMs) {
                continue;
            }
            releaseSlot(slots[s], nowMs);
        }
    }

    for (const auto& req : plan.newSlots) {
        if (!createSlot(req, nowMs)) { break; }
    }

    if (!plan.rejected.empty() && statusMessage.empty()) {
        statusMessage = std::to_string(plan.rejected.size()) +
                        " signal(s) not received (receiver limit or outside usable spectrum)";
    }

    pruneIdleSlots(nowMs);
}

void ReceiverAllocator::releaseAll() {
    for (auto& slot : slots) { releaseSlot(slot, 0); }
}

void ReceiverAllocator::destroyAll() {
    for (auto& slot : slots) {
        stopRecording(slot);
        if (!slot.recorderName.empty()) { core::moduleManager.deleteInstance(slot.recorderName); }
        core::moduleManager.deleteInstance(slot.name);
    }
    slots.clear();
}
