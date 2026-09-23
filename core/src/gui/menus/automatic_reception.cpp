#include <gui/menus/automatic_reception.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <config.h>
#include <imgui.h>
#include <algorithm>
#include <string>

using dsp::detector::CalibrationState;
using dsp::detector::SignalState;

namespace automatic_reception_menu {
    // Kept out of the main config so that the automatic subsystem's settings can be edited,
    // backed up and reset independently. The calibrated noise floor is deliberately NOT stored:
    // it depends on centre frequency, sample rate, gain, device and antenna, so it is always
    // rebuilt when automatic reception starts.
    ConfigManager config;
    bool initialized = false;

    // Mirrors of the manager's config, as the widget-friendly types ImGui wants.
    bool enabled = false;
    float calibrationSeconds = 15.0f;
    float minSnrDb = 10.0f;
    float deviationMultiplier = 3.0f;
    int activationMs = 300;
    int releaseMs = 2000;
    int minDetectionBins = 2;

    static AutoReceiverManager::Config toManagerConfig() {
        AutoReceiverManager::Config c = sigpath::autoReceiverManager.getConfig();
        c.enabled = enabled;
        c.calibrationSeconds = calibrationSeconds;
        c.minSnrDb = minSnrDb;
        c.deviationMultiplier = deviationMultiplier;
        c.activationMs = (uint64_t)std::max<int>(activationMs, 0);
        c.releaseMs = (uint64_t)std::max<int>(releaseMs, 0);
        c.minDetectionBins = std::max<int>(minDetectionBins, 1);
        return c;
    }

    static void save() {
        config.acquire();
        config.conf["enabled"] = enabled;
        config.conf["calibrationSeconds"] = calibrationSeconds;
        config.conf["minSnrDb"] = minSnrDb;
        config.conf["deviationMultiplier"] = deviationMultiplier;
        config.conf["activationMs"] = activationMs;
        config.conf["releaseMs"] = releaseMs;
        config.conf["minDetectionBins"] = minDetectionBins;
        config.release(true);
    }

    static void apply(bool persist) {
        sigpath::autoReceiverManager.setConfig(toManagerConfig());
        if (!enabled) { gui::waterfall.clearDetectionMarkers(); }
        if (persist) { save(); }
    }

    void init() {
        if (initialized) { return; }
        initialized = true;

        json def;
        def["enabled"] = false;
        def["calibrationSeconds"] = 15.0;
        def["minSnrDb"] = 10.0;
        def["deviationMultiplier"] = 3.0;
        def["activationMs"] = 300;
        def["releaseMs"] = 2000;
        def["minDetectionBins"] = 2;
        // Present from the start so the file matches the documented schema; populated in later
        // phases.
        def["recordingPath"] = "recordings/automatic";
        def["profiles"] = json::array();
        def["ignoreRules"] = json::array();

        config.setPath(std::string(core::getRoot()) + "/auto_receiver_config.json");
        config.load(def);
        config.enableAutoSave();

        config.acquire();
        enabled = config.conf["enabled"];
        calibrationSeconds = config.conf["calibrationSeconds"];
        minSnrDb = config.conf["minSnrDb"];
        deviationMultiplier = config.conf["deviationMultiplier"];
        activationMs = config.conf["activationMs"];
        releaseMs = config.conf["releaseMs"];
        minDetectionBins = config.conf["minDetectionBins"];
        config.release();

        apply(false);
    }

    static void drawCalibrationState() {
        auto state = sigpath::autoReceiverManager.getCalibrationState();

        ImGui::TextUnformatted("State");
        ImGui::SameLine();
        if (!enabled) {
            ImGui::TextUnformatted("Disabled");
            return;
        }

        switch (state) {
        case CalibrationState::READY:
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Ready");
            break;
        case CalibrationState::CALIBRATING: {
            float progress = sigpath::autoReceiverManager.getCalibrationProgress();
            ImGui::Text("Calibrating... %d%%", (int)(progress * 100.0f));
            ImGui::ProgressBar(progress, ImVec2(ImGui::GetContentRegionAvail().x, 0));
            break;
        }
        case CalibrationState::UNCALIBRATED:
        default:
            ImGui::TextUnformatted("Waiting for spectrum");
            break;
        }

        auto reason = sigpath::autoReceiverManager.getLastInvalidationReason();
        if (reason != CalibrationInvalidationReason::NONE && state != CalibrationState::READY) {
            ImGui::TextDisabled("%s", toString(reason));
        }
    }

    void draw(void* ctx) {
        float width = ImGui::GetContentRegionAvail().x;

        if (ImGui::Checkbox("Enabled##auto_rx_enabled", &enabled)) { apply(true); }
        ImGui::TextDisabled("Never changes the SDR center frequency.");

        ImGui::Separator();
        ImGui::TextUnformatted("Calibration");

        drawCalibrationState();

        if (!enabled) { style::beginDisabled(); }
        if (ImGui::Button("Recalibrate##auto_rx_recal", ImVec2(width, 0))) {
            sigpath::autoReceiverManager.invalidateCalibration(CalibrationInvalidationReason::MANUAL);
        }

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderFloat("Duration (s)##auto_rx_caldur", &calibrationSeconds, 2.0f, 60.0f, "%.0f")) {
            apply(true);
        }

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderFloat("Min SNR (dB)##auto_rx_minsnr", &minSnrDb, 3.0f, 40.0f, "%.0f")) {
            apply(true);
        }

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderFloat("Deviations##auto_rx_devmul", &deviationMultiplier, 1.0f, 10.0f, "%.1f")) {
            apply(true);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Detection");

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderInt("Activation (ms)##auto_rx_act", &activationMs, 0, 3000)) { apply(true); }

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderInt("Release (ms)##auto_rx_rel", &releaseMs, 0, 10000)) { apply(true); }

        ImGui::SetNextItemWidth(width / 2);
        if (ImGui::SliderInt("Min bins##auto_rx_bins", &minDetectionBins, 1, 32)) { apply(true); }

        ImGui::Checkbox("Show on waterfall##auto_rx_overlay", &gui::waterfall.showDetections);

        if (!enabled) { style::endDisabled(); }

        ImGui::Separator();
        auto tracked = sigpath::autoReceiverManager.getTrackedSignals();
        ImGui::Text("Detected signals (%d)", (int)tracked.size());

        if (ImGui::BeginTable("Detected Signals Table", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 140.0f * style::uiScale))) {
            ImGui::TableSetupColumn("Frequency");
            ImGui::TableSetupColumn("BW");
            ImGui::TableSetupColumn("SNR");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupScrollFreeze(4, 1);
            ImGui::TableHeadersRow();

            // Sorted by frequency so rows don't jump around as tracks are created and pruned.
            std::sort(tracked.begin(), tracked.end(),
                      [](const dsp::detector::TrackedSignal& a, const dsp::detector::TrackedSignal& b) {
                          return a.signal.centerFrequency < b.signal.centerFrequency;
                      });

            for (const auto& track : tracked) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%.6f MHz", track.signal.centerFrequency / 1e6);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.1f kHz", track.signal.bandwidth / 1e3);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.1f dB", track.signal.snrDb);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(dsp::detector::toString(track.state));
            }
            ImGui::EndTable();
        }
    }
}
