#include <gui/menus/receiver_manager.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/dialogs/dialog_box.h>
#include <signal_path/signal_path.h>
#include <signal_path/receiver.h>
#include <core.h>
#include <imgui.h>
#include <algorithm>
#include "../../../../decoder_modules/radio/src/radio_interface.h"

namespace receiver_manager_menu {
    const char* RADIO_MODULE = "radio";

    std::string errorMessage;
    bool errorOpen = false;
    std::string toBeRemoved;
    bool confirmOpened = false;

    void init() {
        errorMessage = "";
        errorOpen = false;
        toBeRemoved = "";
        confirmOpened = false;
    }

    std::string modeName(const std::string& receiverName) {
        if (receiverName.empty()) { return ""; }
        if (!core::modComManager.interfaceExists(receiverName)) { return ""; }
        if (core::modComManager.getModuleName(receiverName) != RADIO_MODULE) { return ""; }
        int mode = -1;
        if (!core::modComManager.callInterface(receiverName, RADIO_IFACE_CMD_GET_MODE, NULL, &mode)) { return ""; }
        switch (mode) {
        case RADIO_IFACE_MODE_NFM: return "NFM";
        case RADIO_IFACE_MODE_WFM: return "WFM";
        case RADIO_IFACE_MODE_AM: return "AM";
        case RADIO_IFACE_MODE_DSB: return "DSB";
        case RADIO_IFACE_MODE_USB: return "USB";
        case RADIO_IFACE_MODE_CW: return "CW";
        case RADIO_IFACE_MODE_LSB: return "LSB";
        case RADIO_IFACE_MODE_RAW: return "RAW";
        default: return "";
        }
    }

    static std::string formatFrequency(double freq) {
        char buf[64];
        snprintf(buf, sizeof buf, "%.6f MHz", freq / 1e6);
        return buf;
    }

    static std::string formatBandwidth(double bw) {
        char buf[64];
        if (bw >= 1e6) { snprintf(buf, sizeof buf, "%.3f MHz", bw / 1e6); }
        else { snprintf(buf, sizeof buf, "%.2f kHz", bw / 1e3); }
        return buf;
    }

    // Receivers sorted by frequency so the list matches the left-to-right order on the waterfall.
    static std::vector<VFOManager::VFOState> sortedReceivers() {
        auto states = sigpath::vfoManager.getVFOStates();
        std::sort(states.begin(), states.end(), [](const VFOManager::VFOState& a, const VFOManager::VFOState& b) {
            return a.offset < b.offset;
        });
        return states;
    }

    std::string activeReceiverSummary() {
        const std::string& name = gui::waterfall.selectedVFO;
        if (name.empty()) { return ""; }
        auto* vfo = sigpath::vfoManager.getVFO(name);
        if (vfo == NULL) { return ""; }
        std::string mode = modeName(name);
        std::string summary = mode.empty() ? "" : (mode + "  ");
        summary += formatBandwidth(vfo->getBandwidth());
        return summary;
    }

    bool drawSelector(float width) {
        auto states = sortedReceivers();
        if (states.empty()) {
            style::beginDisabled();
            ImGui::SetNextItemWidth(width);
            if (ImGui::BeginCombo("##receiver_selector", "No RX")) { ImGui::EndCombo(); }
            style::endDisabled();
            return false;
        }

        bool changed = false;
        std::string preview = gui::waterfall.selectedVFO.empty() ? "---" : gui::waterfall.selectedVFO;
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##receiver_selector", preview.c_str())) {
            for (auto& state : states) {
                bool selected = (state.name == gui::waterfall.selectedVFO);
                if (ImGui::Selectable(state.name.c_str(), selected) && !selected) {
                    gui::waterfall.selectVFO(state.name);
                    changed = true;
                }
                if (selected) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // Mirrors what the Module Manager does, so enabled/disabled state survives a restart.
    static void persistModuleInstances() {
        core::configManager.acquire();
        json instances;
        for (auto& [_name, inst] : core::moduleManager.instances) {
            instances[_name]["module"] = inst.module.info->name;
            instances[_name]["enabled"] = inst.module.api->isEnabled(inst.instance);
        }
        core::configManager.conf["moduleInstances"] = instances;
        core::configManager.release(true);
    }

    static bool radioModuleAvailable() {
        return core::moduleManager.modules.find(RADIO_MODULE) != core::moduleManager.modules.end();
    }

    void draw(void* ctx) {
        double centerFreq = gui::waterfall.getCenterFrequency();
        auto states = sortedReceivers();

        if (ImGui::BeginTable("Receivers Table", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 150.0f * style::uiScale))) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Frequency");
            ImGui::TableSetupColumn("Mode");
            ImGui::TableSetupColumn("Owner");
            ImGui::TableSetupScrollFreeze(4, 1);
            ImGui::TableHeadersRow();

            for (auto& state : states) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                bool selected = (state.name == gui::waterfall.selectedVFO);
                if (ImGui::Selectable((state.name + "##receiver_row_" + state.name).c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns) &&
                    !selected) {
                    gui::waterfall.selectVFO(state.name);
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(formatFrequency(centerFreq + state.offset).c_str());

                ImGui::TableSetColumnIndex(2);
                std::string mode = modeName(state.name);
                ImGui::TextUnformatted(mode.empty() ? "-" : mode.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(state.owner == ReceiverOwner::AUTOMATIC ? "Auto" : "Manual");
            }
            ImGui::EndTable();
        }

        // Summary of the active receiver
        const std::string& active = gui::waterfall.selectedVFO;
        if (active.empty()) {
            ImGui::TextUnformatted("Active receiver: none");
        }
        else {
            auto* vfo = sigpath::vfoManager.getVFO(active);
            std::string mode = modeName(active);
            ImGui::Text("Active: %s  %s  %s  %s  SNR %.0f dB",
                        active.c_str(),
                        (vfo != NULL) ? formatFrequency(centerFreq + vfo->getOffset()).c_str() : "-",
                        mode.empty() ? "-" : mode.c_str(),
                        (vfo != NULL) ? formatBandwidth(vfo->getBandwidth()).c_str() : "-",
                        gui::waterfall.selectedVFOSNR);
        }

        bool canAdd = radioModuleAvailable();
        if (!canAdd) { style::beginDisabled(); }
        if (ImGui::Button("Add manual receiver##receiver_add", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            std::string name = receivers::nextFreeName(ReceiverOwner::MANUAL);
            if (!core::moduleManager.createInstance(name, RADIO_MODULE)) {
                core::moduleManager.postInit(name);
                persistModuleInstances();
                gui::waterfall.selectVFO(name);
            }
            else {
                errorMessage = "Could not create receiver " + name;
                errorOpen = true;
            }
        }
        if (!canAdd) {
            style::endDisabled();
            ImGui::TextUnformatted("The radio module is not loaded.");
        }

        // Only manual receivers may be removed here; automatic ones are owned by the
        // automatic reception subsystem.
        bool canRemove = !active.empty() &&
                         receivers::ownerFromName(active) == ReceiverOwner::MANUAL &&
                         core::moduleManager.instances.find(active) != core::moduleManager.instances.end();
        if (!canRemove) { style::beginDisabled(); }
        if (ImGui::Button("Remove selected receiver##receiver_remove", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            toBeRemoved = active;
            confirmOpened = true;
        }
        if (!canRemove) { style::endDisabled(); }

        if (ImGui::GenericDialog("receiver_mgr_confirm_", confirmOpened, GENERIC_DIALOG_BUTTONS_YES_NO, []() {
                ImGui::Text("Removing receiver \"%s\". Are you sure?", toBeRemoved.c_str());
            }) == GENERIC_DIALOG_BUTTON_YES) {
            core::moduleManager.deleteInstance(toBeRemoved);
            persistModuleInstances();
            toBeRemoved = "";
        }

        ImGui::GenericDialog("receiver_mgr_error_", errorOpen, GENERIC_DIALOG_BUTTONS_OK, []() {
            ImGui::TextUnformatted(errorMessage.c_str());
        });
    }
}
