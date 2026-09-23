#include <imgui.h>
#include <config.h>
#include <core.h>
#include <ctm.h>
#include <dsp/channel/rx_vfo.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <module/module_api.h>
#include <signal_path/signal_path.h>
#include <utils/flog.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "aprs_decoder.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MODULE_INFO{
    /* Name:            */ "aprs_decoder",
    /* Description:     */ "APRS Bell 202 / AX.25 Decoder for SDR++",
    /* Author:          */ "SDR++ Brown contributors",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

namespace {

constexpr double DEFAULT_FREQUENCY = 144800000.0;
constexpr double CHANNEL_BANDWIDTH = 16000.0;
constexpr int DEFAULT_RETENTION_SECONDS = 300;
constexpr int MAX_TRACKED_STATIONS = 512;

}

class APRSDecoderModule : public ModuleInstance {
public:
    explicit APRSDecoderModule(std::string instanceName) :
        name(std::move(instanceName)),
        internalVFOName(name + " APRS internal"),
        tracker(MAX_TRACKED_STATIONS),
        decoder([this](const aprs::Packet& packet) { onPacket(packet); }) {
        loadConfig();

        afterWaterfallDrawListener.ctx = this;
        afterWaterfallDrawListener.handler = [](ImGui::WaterFall::WaterfallDrawArgs args, void* ctx) {
            ((APRSDecoderModule*)ctx)->drawStations(args);
        };
        gui::waterfall.afterWaterfallDraw.bindHandler(&afterWaterfallDrawListener);

        onPlayStateChange.ctx = this;
        onPlayStateChange.handler = [](bool, void* ctx) {
            APRSDecoderModule* module = (APRSDecoderModule*)ctx;
            module->resetDecoder();
            module->clearStations();
        };
        gui::mainWindow.onPlayStateChange.bindHandler(&onPlayStateChange);

        gui::menu.registerEntry(name, menuHandler, this, this);
        enable();
    }

    ~APRSDecoderModule() override {
        disable();
        closeLogFile();
        gui::mainWindow.onPlayStateChange.unbindHandler(&onPlayStateChange);
        gui::waterfall.afterWaterfallDraw.unbindHandler(&afterWaterfallDrawListener);
        gui::menu.removeEntry(name);
    }

    void postInit() override {}

    void enable() override {
        if (enabled) { return; }

        double automaticFrequency = findVisibleAPRSFrequency();
        targetFrequency = automaticFrequency;
        double offset = automaticFrequency > 0.0 ? automaticFrequency - gui::waterfall.getCenterFrequency() : 0.0;
        vfo = sigpath::iqFrontEnd.addVFO(internalVFOName, aprs::SAMPLE_RATE, CHANNEL_BANDWIDTH, offset);
        if (!vfo) {
            flog::error("APRS Decoder failed to create its internal VFO");
            return;
        }

        enabled = true;
        workerRunning = true;
        refreshTuning();
        worker = std::thread(&APRSDecoderModule::workerLoop, this);
        flog::info("APRS Decoder enabled");
    }

    void disable() override {
        if (!enabled) { return; }

        workerRunning = false;
        if (vfo) { vfo->out.stopReader(); }
        if (worker.joinable()) { worker.join(); }
        closeLogFile();
        if (vfo) {
            sigpath::iqFrontEnd.removeVFO(internalVFOName);
            vfo = nullptr;
        }

        enabled = false;
        onFrequency = false;
        resetDecoder();
        clearStations();
        flog::info("APRS Decoder disabled");
    }

    bool isEnabled() override {
        return enabled;
    }

private:
    void loadConfig() {
        config.acquire();
        if (config.conf[name].contains("processingEnabled")) {
            processingEnabledSetting = config.conf[name]["processingEnabled"].get<bool>();
        }
        if (config.conf[name].contains("secondsToKeepResults")) {
            retentionSeconds = config.conf[name]["secondsToKeepResults"].get<int>();
        }
        if (config.conf[name].contains("logEnabled")) {
            logEnabledSetting = config.conf[name]["logEnabled"].get<bool>();
        }
        if (config.conf[name].contains("logPath")) {
            std::string configuredPath = config.conf[name]["logPath"].get<std::string>();
            std::snprintf(logPath, sizeof(logPath), "%s", configuredPath.c_str());
        }
        retentionSeconds = (std::max)(10, (std::min)(3600, retentionSeconds));
        processingEnabled = processingEnabledSetting;
        logEnabled = logEnabledSetting;
        config.conf[name]["processingEnabled"] = processingEnabledSetting;
        config.conf[name]["secondsToKeepResults"] = retentionSeconds;
        config.conf[name]["logEnabled"] = logEnabledSetting;
        config.conf[name]["logPath"] = std::string(logPath);
        config.release(true);
    }

    void saveSetting(const char* key, const json& value) {
        config.acquire();
        config.conf[name][key] = value;
        config.release(true);
    }

    bool hasChannelCoverage() const {
        if (targetFrequency.load() <= 0.0) { return false; }
        double sourceBandwidth = (std::min)(gui::waterfall.getBandwidth(), sigpath::iqFrontEnd.getEffectiveSamplerate());
        if (sourceBandwidth + 1.0 < CHANNEL_BANDWIDTH) { return false; }
        double center = gui::waterfall.getCenterFrequency();
        double frequency = targetFrequency.load();
        double halfSource = sourceBandwidth / 2.0;
        double halfChannel = CHANNEL_BANDWIDTH / 2.0;
        return frequency - halfChannel >= center - halfSource &&
               frequency + halfChannel <= center + halfSource;
    }

    double findVisibleAPRSFrequency() const {
        double sourceBandwidth = (std::min)(gui::waterfall.getBandwidth(), sigpath::iqFrontEnd.getEffectiveSamplerate());
        return aprs::selectAPRSFrequency(gui::waterfall.getCenterFrequency(), sourceBandwidth, CHANNEL_BANDWIDTH);
    }

    void refreshTuning() {
        if (!vfo) { return; }
        double frequency = findVisibleAPRSFrequency();
        double previousFrequency = targetFrequency.exchange(frequency);
        if (frequency > 0.0) {
            double offset = frequency - gui::waterfall.getCenterFrequency();
            if (std::fabs(offset - currentOffset) >= 1.0) {
                currentOffset = offset;
                vfo->setOffset(offset);
            }
        }
        if (frequency != previousFrequency) {
            resetDecoder();
            clearStations();
            flog::info("APRS Decoder selected frequency: {}", frequency);
        }

        bool covered = hasChannelCoverage();
        bool wasCovered = onFrequency.exchange(covered);
        if (covered != wasCovered) {
            resetDecoder();
            clearStations();
            flog::info("APRS Decoder on frequency: {}", covered);
        }
    }

    void workerLoop() {
        std::vector<aprs::IQSample> samples;
        int64_t lastRateUpdate = currentTimeMillis();
        uint64_t lastFrameCount = 0;

        while (workerRunning.load()) {
            int count = vfo->out.read();
            if (count < 0) { break; }

            refreshTuning();
            if (processingEnabled.load() && onFrequency.load()) {
                samples.resize((std::size_t)count);
                for (int i = 0; i < count; i++) {
                    samples[(std::size_t)i] = { vfo->out.readBuf[i].re, vfo->out.readBuf[i].im };
                }
                {
                    std::lock_guard<std::mutex> lock(decoderMutex);
                    decoder.process(samples.data(), samples.size());
                    const aprs::DecoderStats& stats = decoder.stats();
                    validFrames = stats.frames;
                    crcErrors = stats.crcErrors;
                }
            }
            else { resetDecoder(); }
            vfo->out.flush();

            int64_t now = currentTimeMillis();
            if (now - lastRateUpdate >= 1000) {
                uint64_t frames = validFrames.load();
                double seconds = (double)(now - lastRateUpdate) / 1000.0;
                frameRate = (float)((frames - lastFrameCount) / seconds);
                lastFrameCount = frames;
                lastRateUpdate = now;
            }
        }
    }

    void onPacket(const aprs::Packet& packet) {
        int64_t timestamp = sigpath::iqFrontEnd.getCurrentStreamTime();
        aprs::Packet received = packet;
        received.rfFrequency = targetFrequency.load();
        logPacket(received, timestamp);
        std::lock_guard<std::mutex> lock(trackerMutex);
        tracker.update(received, timestamp);
        trackedStations = tracker.size();
    }

    void logPacket(const aprs::Packet& packet, int64_t timestamp) {
        if (!logEnabled.load()) { return; }
        std::lock_guard<std::mutex> lock(logMutex);
        if (!logPath[0]) {
            logPathError = "filename is empty";
            return;
        }
        if (!logFile) {
            logFile = std::fopen(logPath, "at");
            if (!logFile) {
                logPathError = "can't write file";
                return;
            }
        }

        std::string line = aprs::formatLogLine(packet, timestamp);
        if (std::fprintf(logFile, "%s\n", line.c_str()) < 0 || std::fflush(logFile) != 0) {
            logPathError = "can't write file";
            std::fclose(logFile);
            logFile = nullptr;
            return;
        }
        logPathError.clear();
    }

    void closeLogFile() {
        std::lock_guard<std::mutex> lock(logMutex);
        closeLogFileLocked();
    }

    void closeLogFileLocked() {
        if (!logFile) { return; }
        std::fclose(logFile);
        logFile = nullptr;
    }

    void resetDecoder() {
        std::lock_guard<std::mutex> lock(decoderMutex);
        decoder.reset();
    }

    void clearStations() {
        std::lock_guard<std::mutex> lock(trackerMutex);
        tracker.clear();
        trackedStations = 0;
    }

    std::vector<aprs::StationState> stationSnapshot(int64_t now) {
        std::lock_guard<std::mutex> lock(trackerMutex);
        tracker.expire(now, retentionSeconds);
        trackedStations = tracker.size();
        return tracker.snapshot();
    }

    void drawStations(const ImGui::WaterFall::WaterfallDrawArgs& args) {
        if (enabled && !hasChannelCoverage()) {
            bool wasCovered = onFrequency.exchange(false);
            if (wasCovered) {
                resetDecoder();
                clearStations();
            }
        }
        if (!enabled || !processingEnabled.load() || !onFrequency.load()) { return; }
        int64_t now = sigpath::iqFrontEnd.getCurrentStreamTime();
        std::vector<aprs::StationState> stations = stationSnapshot(now);
        if (stations.empty()) { return; }

        ImDrawList* drawList = args.window->DrawList;
        drawList->PushClipRect(args.wfMin, args.wfMax, true);
        ImGui::PushFont(style::baseFont);
        float lineHeight = ImGui::GetTextLineHeight();
        float x = args.wfMin.x + 3.0f;
        float y = args.wfMax.y - lineHeight - 7.0f;

        for (const aprs::StationState& station : stations) {
            std::string label = station.callsign;
            if (station.hasPosition) {
                char position[64];
                std::snprintf(position, sizeof(position), "  %.4f, %.4f", station.latitude, station.longitude);
                label += position;
            }

            ImVec2 badgeSize = ImGui::CalcTextSize("APRS");
            ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
            float width = badgeSize.x + labelSize.x + 15.0f;
            float height = lineHeight + 4.0f;
            if (x + width > args.wfMax.x - 3.0f) {
                x = args.wfMin.x + 3.0f;
                y -= height + 3.0f;
            }
            if (y < args.wfMin.y) { break; }

            float age = (float)(now - station.lastSeenMillis) / 1000.0f;
            float life = (float)retentionSeconds;
            float fade = age > life * 0.67f ? (std::max)(0.15f, (life - age) / (life * 0.33f)) : 1.0f;
            int alpha = (int)(220.0f * fade);
            ImVec2 cardMin(x, y);
            ImVec2 cardMax(x + width, y + height);
            ImVec2 badgeMax(x + badgeSize.x + 7.0f, y + height);

            drawList->AddRectFilled(cardMin, cardMax, IM_COL32(24, 34, 28, alpha), 2.0f);
            drawList->AddRectFilled(cardMin, badgeMax, IM_COL32(25, 150, 70, alpha), 2.0f);
            drawList->AddText(ImVec2(x + 3.0f, y + 2.0f), IM_COL32(255, 255, 255, (int)(255.0f * fade)), "APRS");
            drawList->AddText(ImVec2(badgeMax.x + 4.0f, y + 2.0f), IM_COL32(255, 255, 255, (int)(255.0f * fade)), label.c_str());

            if (ImGui::IsMouseHoveringRect(cardMin, cardMax) && !gui::mainWindow.showMenu) {
                ImGui::BeginTooltip();
                ImGui::Text("Source: %s", station.callsign.c_str());
                ImGui::Text("Destination: %s", station.destination.c_str());
                ImGui::Text("Path: %s", station.path.empty() ? "Direct" : station.path.c_str());
                ImGui::Text("Type: %s", station.type.c_str());
                ImGui::Text("Frequency: %.6f MHz", station.rfFrequency / 1000000.0);
                if (station.hasPosition) {
                    ImGui::Text("Position: %.5f, %.5f", station.latitude, station.longitude);
                    ImGui::Text("Symbol: %c%c", station.symbolTable, station.symbolCode);
                }
                ImGui::Text("Last seen: %.1f sec ago", (std::max)(0.0f, age));
                ImGui::Text("Packets: %llu", (unsigned long long)station.packetCount);
                ImGui::Separator();
                ImGui::TextWrapped("%s", station.information.c_str());
                ImGui::EndTooltip();
            }
            x += width + 3.0f;
        }

        ImGui::PopFont();
        drawList->PopClipRect();
    }

    static void menuHandler(void* ctx) {
        APRSDecoderModule* module = (APRSDecoderModule*)ctx;

        ImGui::LeftLabel("Decode APRS");
        if (ImGui::Checkbox(CONCAT("##_aprs_processing_", module->name), &module->processingEnabledSetting)) {
            module->processingEnabled = module->processingEnabledSetting;
            module->saveSetting("processingEnabled", module->processingEnabledSetting);
            if (!module->processingEnabledSetting) {
                module->resetDecoder();
                module->clearStations();
                module->closeLogFile();
            }
        }

        ImGui::LeftLabel("Frequency");
        if (module->targetFrequency.load() > 0.0) {
            ImGui::Text("Auto: %.6f MHz", module->targetFrequency.load() / 1000000.0);
        }
        else { ImGui::Text("Auto: no standard APRS channel visible"); }

        ImGui::LeftLabel("Keep results (sec)");
        ImGui::FillWidth();
        if (ImGui::SliderInt(CONCAT("##_aprs_retention_", module->name), &module->retentionSeconds, 10, 3600)) {
            module->saveSetting("secondsToKeepResults", module->retentionSeconds);
        }

        ImGui::LeftLabel("TNC2 log");
        if (ImGui::Checkbox(CONCAT("##_aprs_log_enabled_", module->name), &module->logEnabledSetting)) {
            module->logEnabled = module->logEnabledSetting;
            module->saveSetting("logEnabled", module->logEnabledSetting);
            if (!module->logEnabledSetting) { module->closeLogFile(); }
        }
        ImGui::SameLine();
        ImGui::Text("filename:");
        ImGui::SameLine();
        ImGui::FillWidth();
        bool logPathChanged = false;
        std::string updatedLogPath;
        std::string logError;
        {
            std::lock_guard<std::mutex> lock(module->logMutex);
            if (ImGui::InputText(CONCAT("##_aprs_log_path_", module->name), module->logPath, sizeof(module->logPath))) {
                module->closeLogFileLocked();
                module->logPathError.clear();
                updatedLogPath = module->logPath;
                logPathChanged = true;
            }
            logError = module->logPathError;
        }
        if (logPathChanged) { module->saveSetting("logPath", updatedLogPath); }
        if (!logError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
            ImGui::Text("Error: %s", logError.c_str());
            ImGui::PopStyleColor();
        }

        if (ImGui::Button(CONCAT("Clear results##_aprs_", module->name))) { module->clearStations(); }

        ImGui::Separator();
        if (!module->processingEnabled.load()) { ImGui::Text("Status: decoding disabled"); }
        else if (module->onFrequency.load()) { ImGui::Text("Status: auto receiving %.6f MHz", module->targetFrequency.load() / 1000000.0); }
        else if (sigpath::iqFrontEnd.getEffectiveSamplerate() < CHANNEL_BANDWIDTH) {
            ImGui::Text("Status: insufficient source bandwidth");
        } else { ImGui::Text("Status: no standard APRS channel is visible"); }
        ImGui::Text("Rate: %.2f packets/sec", module->frameRate.load());
        ImGui::Text("Stations: %llu", (unsigned long long)module->trackedStations.load());
        ImGui::Text("Valid: %llu  FCS errors: %llu", (unsigned long long)module->validFrames.load(),
                    (unsigned long long)module->crcErrors.load());
    }

    std::string name;
    std::string internalVFOName;
    bool enabled = false;
    bool processingEnabledSetting = true;
    bool logEnabledSetting = false;
    int retentionSeconds = DEFAULT_RETENTION_SECONDS;

    std::atomic_bool workerRunning{false};
    std::atomic_bool processingEnabled{true};
    std::atomic_bool logEnabled{false};
    std::atomic_bool onFrequency{false};
    std::atomic<double> targetFrequency{DEFAULT_FREQUENCY};
    std::atomic<uint64_t> validFrames{0};
    std::atomic<uint64_t> crcErrors{0};
    std::atomic<uint64_t> trackedStations{0};
    std::atomic<float> frameRate{0.0f};

    double currentOffset = 1.0e30;
    dsp::channel::RxVFO* vfo = nullptr;
    std::thread worker;
    aprs::Decoder decoder;
    std::mutex decoderMutex;
    aprs::StationTracker tracker;
    std::mutex trackerMutex;
    char logPath[1024]{};
    std::string logPathError;
    FILE* logFile = nullptr;
    std::mutex logMutex;

    EventHandler<ImGui::WaterFall::WaterfallDrawArgs> afterWaterfallDrawListener;
    EventHandler<bool> onPlayStateChange;
};

MOD_EXPORT void sdrppModuleInit() {
    json defaults = json({});
    config.setPath(std::string(core::getRoot()) + "/aprs_decoder_config.json");
    config.load(defaults);
    config.enableAutoSave();
}

MOD_EXPORT void* sdrppModuleCreateInstance(const char* instanceName, size_t instanceNameLen) {
    std::string name(instanceName, instanceNameLen);
    return new APRSDecoderModule(std::move(name));
}

MOD_EXPORT void sdrppModuleDestroyInstance(void* instance) {
    delete (APRSDecoderModule*)instance;
}

MOD_EXPORT void sdrppModuleEnd() {
    config.disableAutoSave();
    config.save();
}

SDRPP_MODULE_EXPORT_API;
