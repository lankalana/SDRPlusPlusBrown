#include <gui/menus/automatic_reception.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <config.h>
#include <imgui.h>
#include <algorithm>
#include <string>
#include <utils/flog.h>

using dsp::detector::MeasurementState;
using dsp::detector::NoiseFloorMode;
using dsp::detector::SignalState;

namespace automatic_reception_menu {
    // Kept out of the main config so that the automatic subsystem's settings can be edited,
    // backed up and reset independently. A measured noise floor is never persisted: it depends on
    // centre frequency, sample rate, gain, device and antenna.
    ConfigManager config;
    bool initialized = false;

    // Mirrors of the manager's config, as the widget-friendly types ImGui wants.
    bool enabled = false;
    int floorModeIdx = 0; // 0 = manual, 1 = measured
    float manualFloorDb = -85.0f;
    float marginDb = 10.0f;
    float measurementSeconds = 1.0f;
    float spectralWindowKHz = 2000.0f;
    float spectralPercentile = 0.25f;
    int activationMs = 300;
    int releaseMs = 2000;
    int averagingFrames = 4;
    float minBandwidthKHz = 0.0f;
    float maxBandwidthKHz = 0.0f;
    float mergeGapKHz = 20.0f;
    bool restrictToProfiles = false;
    float ignorePaddingKHz = 5.0f;
    bool allocateReceivers = false;
    bool recordAudio = false;
    int maxReceivers = 4;
    std::string recordingPath = "%ROOT%/recordings/automatic";
    bool dateSubfolders = true;
    int idleTimeoutSec = 60;
    int minRecordingMs = 1000;

    static void applyAllocator(bool persist);
    static AutoReceiverManager::Config toManagerConfig() {
        AutoReceiverManager::Config c = sigpath::autoReceiverManager.getConfig();
        c.enabled = enabled;
        c.floorMode = (floorModeIdx == 1) ? NoiseFloorMode::MEASURED : NoiseFloorMode::MANUAL;
        c.manualFloorDb = manualFloorDb;
        c.marginDb = marginDb;
        c.measurementSeconds = measurementSeconds;
        c.spectralWindowHz = spectralWindowKHz * 1e3;
        c.spectralPercentile = spectralPercentile;
        c.activationMs = (uint64_t)std::max<int>(activationMs, 0);
        c.releaseMs = (uint64_t)std::max<int>(releaseMs, 0);
        c.detectionAveragingFrames = std::max<int>(averagingFrames, 1);
        c.minBandwidthHz = minBandwidthKHz * 1e3;
        c.maxBandwidthHz = maxBandwidthKHz * 1e3;
        c.mergeGapHz = mergeGapKHz * 1e3;
        c.restrictToProfiles = restrictToProfiles;
        return c;
    }

    static json profilesToJson(const ReceptionProfileSet& set) {
        json arr = json::array();
        for (const auto& p : set.profiles) {
            json j;
            j["name"] = p.name;
            j["enabled"] = p.enabled;
            j["minFrequency"] = p.minFrequency;
            j["maxFrequency"] = p.maxFrequency;
            j["demod"] = (int)p.demod;
            j["bandwidth"] = p.bandwidth;
            j["minDetectionBandwidth"] = p.minDetectionBandwidth;
            j["maxDetectionBandwidth"] = p.maxDetectionBandwidth;
            j["frequencyStep"] = p.frequencyStep;
            j["priority"] = p.priority;
            arr.push_back(j);
        }
        return arr;
    }

    static ReceptionProfileSet profilesFromJson(const json& arr) {
        ReceptionProfileSet set;
        if (!arr.is_array()) { return set; }
        for (const auto& j : arr) {
            ReceptionProfile p;
            p.name = j.value("name", "Profile");
            p.enabled = j.value("enabled", true);
            p.minFrequency = j.value("minFrequency", 0.0);
            p.maxFrequency = j.value("maxFrequency", 0.0);
            int demod = j.value("demod", (int)ProfileDemod::NFM);
            p.demod = (ProfileDemod)std::clamp(demod, 0, (int)ProfileDemod::_COUNT - 1);
            p.bandwidth = j.value("bandwidth", 12500.0);
            p.minDetectionBandwidth = j.value("minDetectionBandwidth", 0.0);
            p.maxDetectionBandwidth = j.value("maxDetectionBandwidth", 0.0);
            p.frequencyStep = j.value("frequencyStep", 0.0);
            p.priority = j.value("priority", 0);
            set.profiles.push_back(p);
        }
        return set;
    }

    static json ignoreRulesToJson(const IgnoreRuleSet& set) {
        json arr = json::array();
        for (const auto& r : set.rules) {
            json j;
            j["lowerFrequency"] = r.lowerFrequency;
            j["upperFrequency"] = r.upperFrequency;
            j["reason"] = r.reason;
            j["enabled"] = r.enabled;
            arr.push_back(j);
        }
        return arr;
    }

    static IgnoreRuleSet ignoreRulesFromJson(const json& arr) {
        IgnoreRuleSet set;
        if (!arr.is_array()) { return set; }
        for (const auto& j : arr) {
            IgnoreRule r;
            r.lowerFrequency = j.value("lowerFrequency", 0.0);
            r.upperFrequency = j.value("upperFrequency", 0.0);
            r.reason = j.value("reason", "");
            r.enabled = j.value("enabled", true);
            set.rules.push_back(r);
        }
        return set;
    }

    static void saveProfiles() {
        config.acquire();
        config.conf["profiles"] = profilesToJson(sigpath::autoReceiverManager.getProfiles());
        config.release(true);
    }

    static void saveIgnoreRules() {
        config.acquire();
        config.conf["ignoreRules"] = ignoreRulesToJson(sigpath::autoReceiverManager.getIgnoreRules());
        config.release(true);
    }

    static void save() {
        config.acquire();
        config.conf["floorMode"] = (floorModeIdx == 1) ? "measured" : "manual";
        config.conf["manualFloorDb"] = manualFloorDb;
        config.conf["marginDb"] = marginDb;
        config.conf["measurementSeconds"] = measurementSeconds;
        config.conf["spectralWindowKHz"] = spectralWindowKHz;
        config.conf["spectralPercentile"] = spectralPercentile;
        config.conf["activationMs"] = activationMs;
        config.conf["releaseMs"] = releaseMs;
        config.conf["averagingFrames"] = averagingFrames;
        config.conf["minBandwidthKHz"] = minBandwidthKHz;
        config.conf["maxBandwidthKHz"] = maxBandwidthKHz;
        config.conf["mergeGapKHz"] = mergeGapKHz;
        config.conf["restrictToProfiles"] = restrictToProfiles;
        config.conf["ignorePaddingKHz"] = ignorePaddingKHz;
        config.release(true);
    }

    static void apply(bool persist) {
        sigpath::autoReceiverManager.setConfig(toManagerConfig());
        if (!enabled) {
            gui::waterfall.clearDetectionMarkers();
            gui::waterfall.clearNoiseFloorOverlay();
        }
        if (persist) { save(); }
    }

    void init() {
        if (initialized) { return; }
        initialized = true;

        // Bumped when a default changes in a way that a file written by an older build would
        // silently defeat. Detection tuning is reset on a bump; the user's own floor, profiles
        // and ignore rules are never touched.
        const int CONFIG_VERSION = 2;

        json def;
        def["configVersion"] = CONFIG_VERSION;
        def["floorMode"] = "manual";
        def["manualFloorDb"] = -85.0;
        def["marginDb"] = 10.0;
        def["measurementSeconds"] = 1.0;
        def["spectralWindowKHz"] = 2000.0;
        def["spectralPercentile"] = 0.25;
        def["activationMs"] = 300;
        def["releaseMs"] = 2000;
        def["averagingFrames"] = 4;
        def["minBandwidthKHz"] = 0.0;
        def["maxBandwidthKHz"] = 0.0;
        def["mergeGapKHz"] = 20.0;
        def["restrictToProfiles"] = false;
        def["ignorePaddingKHz"] = 5.0;
        def["allocateReceivers"] = false;
        def["recordAudio"] = false;
        def["maxReceivers"] = 4;
        def["recordingPath"] = "%ROOT%/recordings/automatic";
        def["dateSubfolders"] = true;
        def["idleTimeoutSec"] = 60;
        def["minRecordingMs"] = 1000;
        def["profiles"] = profilesToJson(ReceptionProfileSet::defaults());
        def["ignoreRules"] = json::array();

        config.setPath(std::string(core::getRoot()) + "/auto_receiver_config.json");
        config.load(def);
        config.enableAutoSave();

        config.acquire();
        // ConfigManager::load() does not merge defaults into an existing file, so every read has
        // to tolerate a key written by an older version not being there.
        bool repaired = false;
        for (auto& [key, value] : def.items()) {
            if (!config.conf.contains(key)) {
                config.conf[key] = value;
                repaired = true;
            }
        }

        // A file from before the channel raster existed carries detection settings that make the
        // raster do nothing (no merge gap, profiles used as an all-or-nothing gate). Reset just
        // those to the current defaults rather than leaving the feature quietly disabled.
        if (config.conf.value("configVersion", 1) < CONFIG_VERSION) {
            flog::info("Automatic reception: migrating config to version {}", CONFIG_VERSION);
            for (const char* key : { "mergeGapKHz", "averagingFrames", "minBandwidthKHz",
                                     "maxBandwidthKHz", "restrictToProfiles" }) {
                config.conf[key] = def[key];
            }
            // Profiles written before the raster field existed default it to zero, which reads as
            // "no rounding". Adopt the shipped rasters for any profile that has none.
            auto existing = profilesFromJson(config.conf["profiles"]);
            auto shipped = ReceptionProfileSet::defaults();
            for (auto& p : existing.profiles) {
                if (p.frequencyStep > 0.0) { continue; }
                for (const auto& s : shipped.profiles) {
                    if (s.name == p.name) {
                        p.frequencyStep = s.frequencyStep;
                        break;
                    }
                }
            }
            config.conf["profiles"] = profilesToJson(existing);
            config.conf["configVersion"] = CONFIG_VERSION;
            repaired = true;
        }

        floorModeIdx = (config.conf.value("floorMode", std::string("manual")) == "measured") ? 1 : 0;
        manualFloorDb = config.conf.value("manualFloorDb", -85.0f);
        marginDb = config.conf.value("marginDb", 10.0f);
        measurementSeconds = config.conf.value("measurementSeconds", 1.0f);
        spectralWindowKHz = config.conf.value("spectralWindowKHz", 2000.0f);
        spectralPercentile = config.conf.value("spectralPercentile", 0.25f);
        activationMs = config.conf.value("activationMs", 300);
        releaseMs = config.conf.value("releaseMs", 2000);
        averagingFrames = config.conf.value("averagingFrames", 4);
        minBandwidthKHz = config.conf.value("minBandwidthKHz", 0.0f);
        maxBandwidthKHz = config.conf.value("maxBandwidthKHz", 0.0f);
        mergeGapKHz = config.conf.value("mergeGapKHz", 20.0f);
        restrictToProfiles = config.conf.value("restrictToProfiles", false);
        ignorePaddingKHz = config.conf.value("ignorePaddingKHz", 5.0f);
        allocateReceivers = config.conf.value("allocateReceivers", false);
        recordAudio = config.conf.value("recordAudio", false);
        maxReceivers = config.conf.value("maxReceivers", 4);
        recordingPath = config.conf.value("recordingPath", std::string("%ROOT%/recordings/automatic"));
        dateSubfolders = config.conf.value("dateSubfolders", true);
        idleTimeoutSec = config.conf.value("idleTimeoutSec", 60);
        minRecordingMs = config.conf.value("minRecordingMs", 1000);

        auto loadedProfiles = profilesFromJson(config.conf["profiles"]);
        // An existing file from before profiles existed has an empty list; seed it rather than
        // presenting an empty table.
        if (loadedProfiles.profiles.empty()) {
            loadedProfiles = ReceptionProfileSet::defaults();
            config.conf["profiles"] = profilesToJson(loadedProfiles);
            repaired = true;
        }
        auto loadedIgnores = ignoreRulesFromJson(config.conf["ignoreRules"]);
        config.release(repaired);

        sigpath::autoReceiverManager.setProfiles(loadedProfiles);
        sigpath::autoReceiverManager.setIgnoreRules(loadedIgnores);
        apply(false);
        applyAllocator(false);
    }

    static void drawFloorSection(float width) {
        ImGui::TextUnformatted("Noise floor");

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::Combo("Source##auto_rx_floormode", &floorModeIdx, "Manual (flat)\0Measured\0")) {
            apply(true);
        }

        if (floorModeIdx == 0) {
            ImGui::SetNextItemWidth(width / 2);
            if (ImGui::SliderFloat("Floor (dB)##auto_rx_floor", &manualFloorDb, -140.0f, 0.0f, "%.0f")) {
                apply(true);
            }
            if (ImGui::Button("Set from current spectrum##auto_rx_floorauto", ImVec2(width, 0))) {
                if (sigpath::autoReceiverManager.setManualFloorFromSpectrum()) {
                    manualFloorDb = sigpath::autoReceiverManager.getConfig().manualFloorDb;
                    save();
                }
            }
            ImGui::TextDisabled("A flat level survives retuning, so detection keeps working.");
        }
        else {
            auto state = sigpath::autoReceiverManager.getMeasurementState();
            switch (state) {
            case MeasurementState::READY:
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Measured");
                break;
            case MeasurementState::MEASURING:
                ImGui::ProgressBar(sigpath::autoReceiverManager.getMeasurementProgress(),
                                   ImVec2(width, 0), "Measuring...");
                break;
            case MeasurementState::IDLE:
            default:
                ImGui::TextUnformatted("Not measured yet");
                break;
            }

            if (ImGui::Button("Measure now##auto_rx_measure", ImVec2(width, 0))) {
                sigpath::autoReceiverManager.startMeasurement();
            }
            if (state == MeasurementState::MEASURING) {
                if (ImGui::Button("Cancel##auto_rx_measure_cancel", ImVec2(width, 0))) {
                    sigpath::autoReceiverManager.cancelMeasurement();
                }
            }

            ImGui::SetNextItemWidth(width / 2);
            if (ImGui::SliderFloat("Duration (s)##auto_rx_measdur", &measurementSeconds, 0.2f, 10.0f,
                                   "%.1f")) {
                apply(true);
            }

            ImGui::SetNextItemWidth(width / 2);
            if (ImGui::SliderFloat("Window (kHz)##auto_rx_specwin", &spectralWindowKHz, 100.0f,
                                   10000.0f, "%.0f")) {
                apply(true);
            }
            ImGui::TextDisabled("Must be wider than the widest signal to detect.");

            ImGui::SetNextItemWidth(width / 2);
            if (ImGui::SliderFloat("Percentile##auto_rx_specpct", &spectralPercentile, 0.02f, 0.5f,
                                   "%.2f")) {
                apply(true);
            }
            ImGui::TextDisabled("Discarded on retune; measure again.");
        }

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderFloat("Margin (dB)##auto_rx_margin", &marginDb, 0.0f, 40.0f, "%.0f")) {
            apply(true);
        }
        ImGui::TextDisabled("Detection threshold = floor + margin.");

        ImGui::Checkbox("Show floor on waterfall##auto_rx_floorshow", &gui::waterfall.showNoiseFloor);
    }

    static void applyAllocator(bool persist) {
        ReceiverAllocator::Config c = sigpath::receiverAllocator.getConfig();
        c.allocateReceivers = allocateReceivers;
        c.recordAudio = recordAudio;
        c.maxReceivers = maxReceivers;
        c.recordingPath = recordingPath;
        c.dateSubfolders = dateSubfolders;
        c.idleTimeoutMs = (uint64_t)std::max<int>(idleTimeoutSec, 0) * 1000;
        c.minRecordingMs = (uint64_t)std::max<int>(minRecordingMs, 0);
        sigpath::receiverAllocator.setConfig(c);

        if (persist) {
            config.acquire();
            config.conf["allocateReceivers"] = allocateReceivers;
            config.conf["recordAudio"] = recordAudio;
            config.conf["maxReceivers"] = maxReceivers;
            config.conf["recordingPath"] = recordingPath;
            config.conf["dateSubfolders"] = dateSubfolders;
            config.conf["idleTimeoutSec"] = idleTimeoutSec;
            config.conf["minRecordingMs"] = minRecordingMs;
            config.release(true);
        }
    }

    static void drawReceivers(float width) {
        ImGui::TextUnformatted("Automatic receivers");

        if (ImGui::Checkbox("Allocate receivers##auto_rx_alloc", &allocateReceivers)) {
            applyAllocator(true);
        }
        ImGui::TextDisabled("Creates AUTO1, AUTO2... for confirmed signals.");

        if (!allocateReceivers) { style::beginDisabled(); }

        if (ImGui::Checkbox("Record audio##auto_rx_rec", &recordAudio)) { applyAllocator(true); }

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderInt("Max receivers##auto_rx_max", &maxReceivers, 1, 16)) {
            applyAllocator(true);
        }

        char pathBuf[512];
        snprintf(pathBuf, sizeof pathBuf, "%s", recordingPath.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##auto_rx_path", pathBuf, sizeof pathBuf)) {
            recordingPath = pathBuf;
            applyAllocator(true);
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Recording folder. %%ROOT%% is expanded."); }

        if (ImGui::Checkbox("Date subfolders##auto_rx_datedir", &dateSubfolders)) {
            applyAllocator(true);
        }

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderInt("Idle timeout (s)##auto_rx_idle", &idleTimeoutSec, 0, 600)) {
            applyAllocator(true);
        }
        ImGui::TextDisabled("0 keeps idle receivers forever.");

        if (!allocateReceivers) { style::endDisabled(); }

        const auto& status = sigpath::receiverAllocator.getStatusMessage();
        if (!status.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", status.c_str());
        }

        const auto& slots = sigpath::receiverAllocator.getSlots();
        if (ImGui::BeginTable("Auto Receivers Table", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0, 110.0f * style::uiScale))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 70.0f * style::uiScale);
            ImGui::TableSetupColumn("Frequency");
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 50.0f * style::uiScale);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 70.0f * style::uiScale);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (const auto& slot : slots) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable((slot.name + "##autoslot_" + slot.name).c_str(),
                                      slot.name == gui::waterfall.selectedVFO,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    gui::waterfall.selectVFO(slot.name);
                }

                ImGui::TableSetColumnIndex(1);
                if (slot.state == AutoReceiverSlot::ACTIVE) {
                    ImGui::Text("%.4f MHz", slot.tuneFrequency / 1e6);
                }
                else {
                    ImGui::TextDisabled("---");
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(slot.state == AutoReceiverSlot::ACTIVE ? toString(slot.demod)
                                                                              : "-");

                ImGui::TableSetColumnIndex(3);
                if (slot.recording) {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "REC");
                }
                else if (slot.state == AutoReceiverSlot::ACTIVE) {
                    ImGui::TextUnformatted("Active");
                }
                else {
                    ImGui::TextDisabled("Idle");
                }
            }
            ImGui::EndTable();
        }
    }

    static void drawProfiles(float width) {
        ImGui::TextUnformatted("Reception profiles");
        if (ImGui::Checkbox("Ignore signals outside every profile##auto_rx_restrict", &restrictToProfiles)) {
            apply(true);
        }
        ImGui::TextDisabled("A matching profile always supplies its raster, bandwidth\n"
                            "limits and mode; this only affects uncovered frequencies.");

        auto set = sigpath::autoReceiverManager.getProfiles();
        bool modified = false;
        int toRemove = -1;

        if (ImGui::BeginTable("Reception Profiles Table", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX,
                              ImVec2(0, 170.0f * style::uiScale))) {
            ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 26.0f * style::uiScale);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 100.0f * style::uiScale);
            ImGui::TableSetupColumn("Range (MHz)", ImGuiTableColumnFlags_WidthFixed,
                                    150.0f * style::uiScale);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 70.0f * style::uiScale);
            ImGui::TableSetupColumn("RX BW / Detect min / max (kHz)", ImGuiTableColumnFlags_WidthFixed,
                                    220.0f * style::uiScale);
            ImGui::TableSetupColumn("Step (kHz)", ImGuiTableColumnFlags_WidthFixed,
                                    80.0f * style::uiScale);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f * style::uiScale);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < set.profiles.size(); i++) {
                auto& p = set.profiles[i];
                std::string id = "##prof_" + std::to_string(i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                if (ImGui::Checkbox(("##en" + id).c_str(), &p.enabled)) { modified = true; }

                ImGui::TableSetColumnIndex(1);
                char nameBuf[64];
                snprintf(nameBuf, sizeof nameBuf, "%s", p.name.c_str());
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText(("##name" + id).c_str(), nameBuf, sizeof nameBuf)) {
                    p.name = nameBuf;
                    modified = true;
                }

                ImGui::TableSetColumnIndex(2);
                double loMHz = p.minFrequency / 1e6;
                double hiMHz = p.maxFrequency / 1e6;
                ImGui::SetNextItemWidth(70.0f * style::uiScale);
                if (ImGui::InputDouble(("##lo" + id).c_str(), &loMHz, 0.0, 0.0, "%.3f")) {
                    p.minFrequency = loMHz * 1e6;
                    modified = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f * style::uiScale);
                if (ImGui::InputDouble(("##hi" + id).c_str(), &hiMHz, 0.0, 0.0, "%.3f")) {
                    p.maxFrequency = hiMHz * 1e6;
                    modified = true;
                }

                ImGui::TableSetColumnIndex(3);
                int demodIdx = (int)p.demod;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo(("##mode" + id).c_str(), &demodIdx, profileDemodComboItems())) {
                    p.demod = (ProfileDemod)demodIdx;
                    modified = true;
                }

                ImGui::TableSetColumnIndex(4);
                double bwK = p.bandwidth / 1e3;
                double minK = p.minDetectionBandwidth / 1e3;
                double maxK = p.maxDetectionBandwidth / 1e3;
                ImGui::SetNextItemWidth(64.0f * style::uiScale);
                if (ImGui::InputDouble(("##bw" + id).c_str(), &bwK, 0.0, 0.0, "%.1f")) {
                    p.bandwidth = bwK * 1e3;
                    modified = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(64.0f * style::uiScale);
                if (ImGui::InputDouble(("##dmin" + id).c_str(), &minK, 0.0, 0.0, "%.1f")) {
                    p.minDetectionBandwidth = minK * 1e3;
                    modified = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(64.0f * style::uiScale);
                if (ImGui::InputDouble(("##dmax" + id).c_str(), &maxK, 0.0, 0.0, "%.1f")) {
                    p.maxDetectionBandwidth = maxK * 1e3;
                    modified = true;
                }

                ImGui::TableSetColumnIndex(5);
                double stepK = p.frequencyStep / 1e3;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputDouble(("##step" + id).c_str(), &stepK, 0.0, 0.0, "%.3f")) {
                    p.frequencyStep = std::max<double>(stepK, 0.0) * 1e3;
                    modified = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Channel raster. Rounds the receiver onto channel and merges\n"
                                      "fragments of one transmission. 0 disables rounding.");
                }

                ImGui::TableSetColumnIndex(6);
                if (ImGui::SmallButton(("x" + id).c_str())) { toRemove = (int)i; }
            }
            ImGui::EndTable();
        }

        if (toRemove >= 0) {
            set.profiles.erase(set.profiles.begin() + toRemove);
            modified = true;
        }

        if (ImGui::Button("Add profile##auto_rx_addprof", ImVec2(width / 2, 0))) {
            ReceptionProfile p;
            p.name = "New profile";
            p.minFrequency = gui::waterfall.getCenterFrequency() - (gui::waterfall.getBandwidth() / 2.0);
            p.maxFrequency = gui::waterfall.getCenterFrequency() + (gui::waterfall.getBandwidth() / 2.0);
            set.profiles.push_back(p);
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Restore defaults##auto_rx_defprof", ImVec2(width / 2, 0))) {
            set = ReceptionProfileSet::defaults();
            modified = true;
        }

        if (modified) {
            sigpath::autoReceiverManager.setProfiles(set);
            saveProfiles();
        }
    }

    static void drawIgnoreRules(float width) {
        ImGui::TextUnformatted("Ignore rules");

        auto set = sigpath::autoReceiverManager.getIgnoreRules();
        bool modified = false;
        int toRemove = -1;

        if (ImGui::BeginTable("Ignore Rules Table", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0, 120.0f * style::uiScale))) {
            ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 26.0f * style::uiScale);
            ImGui::TableSetupColumn("Range (MHz)", ImGuiTableColumnFlags_WidthFixed,
                                    150.0f * style::uiScale);
            ImGui::TableSetupColumn("Reason");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f * style::uiScale);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < set.rules.size(); i++) {
                auto& r = set.rules[i];
                std::string id = "##ign_" + std::to_string(i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                if (ImGui::Checkbox(("##en" + id).c_str(), &r.enabled)) { modified = true; }

                ImGui::TableSetColumnIndex(1);
                double loMHz = r.lowerFrequency / 1e6;
                double hiMHz = r.upperFrequency / 1e6;
                ImGui::SetNextItemWidth(70.0f * style::uiScale);
                if (ImGui::InputDouble(("##lo" + id).c_str(), &loMHz, 0.0, 0.0, "%.4f")) {
                    r.lowerFrequency = loMHz * 1e6;
                    modified = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f * style::uiScale);
                if (ImGui::InputDouble(("##hi" + id).c_str(), &hiMHz, 0.0, 0.0, "%.4f")) {
                    r.upperFrequency = hiMHz * 1e6;
                    modified = true;
                }

                ImGui::TableSetColumnIndex(2);
                char reasonBuf[96];
                snprintf(reasonBuf, sizeof reasonBuf, "%s", r.reason.c_str());
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText(("##reason" + id).c_str(), reasonBuf, sizeof reasonBuf)) {
                    r.reason = reasonBuf;
                    modified = true;
                }

                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton(("x" + id).c_str())) { toRemove = (int)i; }
            }
            ImGui::EndTable();
        }

        if (toRemove >= 0) {
            set.rules.erase(set.rules.begin() + toRemove);
            modified = true;
        }

        if (ImGui::Button("Ignore current VFO range##auto_rx_ignvfo", ImVec2(width, 0))) {
            auto* vfo = sigpath::vfoManager.getVFO(gui::waterfall.selectedVFO);
            if (vfo != NULL) {
                double center = gui::waterfall.getCenterFrequency() + vfo->getOffset();
                double half = vfo->getBandwidth() / 2.0;
                set.addForSignal(center - half, center + half, "Ignored from " +
                                                                   gui::waterfall.selectedVFO,
                                 ignorePaddingKHz * 1e3);
                modified = true;
            }
        }

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderFloat("Ignore padding (kHz)##auto_rx_ignpad", &ignorePaddingKHz, 0.0f,
                               200.0f, "%.1f")) {
            save();
        }

        if (modified) {
            sigpath::autoReceiverManager.setIgnoreRules(set);
            saveIgnoreRules();
        }
    }

    void draw(void* ctx) {
        float width = ImGui::GetContentRegionAvail().x;

        if (ImGui::Checkbox("Enabled##auto_rx_enabled", &enabled)) { apply(true); }
        ImGui::TextDisabled("Never changes the SDR center frequency.");

        ImGui::Separator();

        drawFloorSection(width);

        ImGui::Separator();
        ImGui::TextUnformatted("Detection");

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderInt("Averaging##auto_rx_avg", &averagingFrames, 1, 32, "%d frames")) {
            apply(true);
        }
        ImGui::TextDisabled("Raise for wideband modes; a single frame of FM is notched.");

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderFloat("Min BW (kHz)##auto_rx_minbw", &minBandwidthKHz, 0.0f, 500.0f, "%.1f")) {
            apply(true);
        }
        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderFloat("Max BW (kHz)##auto_rx_maxbw", &maxBandwidthKHz, 0.0f, 1000.0f,
                               "%.1f")) {
            apply(true);
        }
        ImGui::TextDisabled("Max 0 means no upper limit.");

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderFloat("Merge gap (kHz)##auto_rx_gap", &mergeGapKHz, 0.0f, 200.0f, "%.1f")) {
            apply(true);
        }
        ImGui::TextDisabled("Bridges dropouts inside one transmission.");

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderInt("Activation (ms)##auto_rx_act", &activationMs, 0, 3000)) { apply(true); }

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderInt("Release (ms)##auto_rx_rel", &releaseMs, 0, 10000)) { apply(true); }

        ImGui::Checkbox("Show detections##auto_rx_overlay", &gui::waterfall.showDetections);

        ImGui::Separator();
        drawReceivers(width);

        ImGui::Separator();
        drawProfiles(width);

        ImGui::Separator();
        drawIgnoreRules(width);

        ImGui::Separator();
        auto signals = sigpath::autoReceiverManager.getClassifiedSignals();
        ImGui::Text("Detected signals (%d)", (int)signals.size());

        // Sorted by frequency so rows don't jump around as tracks are created and pruned.
        std::sort(signals.begin(), signals.end(),
                  [](const AutoReceiverManager::ClassifiedSignal& a,
                     const AutoReceiverManager::ClassifiedSignal& b) {
                      return a.tracked.signal.centerFrequency < b.tracked.signal.centerFrequency;
                  });

        if (ImGui::BeginTable("Detected Signals Table", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 160.0f * style::uiScale))) {
            ImGui::TableSetupColumn("Frequency");
            ImGui::TableSetupColumn("BW");
            ImGui::TableSetupColumn("SNR");
            ImGui::TableSetupColumn("Profile");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 50.0f * style::uiScale);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            uint64_t toIgnore = 0;
            for (const auto& cs : signals) {
                const auto& sig = cs.tracked.signal;
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%.4f MHz", cs.tuneFrequency / 1e6);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.1f kHz", sig.bandwidth / 1e3);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.1f dB", sig.snrDb);

                ImGui::TableSetColumnIndex(3);
                if (cs.hasProfile) {
                    ImGui::Text("%s (%s)", cs.profileName.c_str(), toString(cs.demod));
                }
                else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableSetColumnIndex(4);
                if (cs.ignored) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Ignored");
                }
                else {
                    ImGui::TextUnformatted(dsp::detector::toString(cs.tracked.state));
                }

                ImGui::TableSetColumnIndex(5);
                if (!cs.ignored) {
                    if (ImGui::SmallButton(("Ignore##sig_" + std::to_string(sig.id)).c_str())) {
                        toIgnore = sig.id;
                    }
                }
            }
            ImGui::EndTable();

            if (toIgnore != 0) {
                sigpath::autoReceiverManager.ignoreSignal(toIgnore, "Ignored from detections",
                                                          ignorePaddingKHz * 1e3);
                saveIgnoreRules();
            }
        }
    }
}
