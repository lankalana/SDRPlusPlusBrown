#include <imgui.h>
#include <config.h>
#include <core.h>
#include <ctm.h>
#include <dsp/channel/rx_vfo.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <module.h>
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

#include "adsb_decoder.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "adsb_decoder",
    /* Description:     */ "ADS-B 1090ES Decoder for SDR++",
    /* Author:          */ "@lankalana",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

namespace {

constexpr double ADSB_FREQUENCY = 1090000000.0;
constexpr double CHANNEL_BANDWIDTH = 2400000.0;
constexpr int DEFAULT_RETENTION_SECONDS = 30;
constexpr int MAX_TRACKED_AIRCRAFT = 512;

std::string formatICAO(uint32_t icao) {
    char text[7];
    std::snprintf(text, sizeof(text), "%06X", icao);
    return text;
}

}

class ADSBDecoderModule : public ModuleManager::Instance {
public:
    explicit ADSBDecoderModule(std::string instanceName) :
        name(std::move(instanceName)),
        internalVFOName(name + " ADS-B internal"),
        tracker(MAX_TRACKED_AIRCRAFT),
        decoder([this](const adsb::DecodedFrame& frame) { onFrame(frame); }) {
        loadConfig();
        decoder.setMaxCorrections(errorCorrectionBits.load());

        afterWaterfallDrawListener.ctx = this;
        afterWaterfallDrawListener.handler = [](ImGui::WaterFall::WaterfallDrawArgs args, void* ctx) {
            ((ADSBDecoderModule*)ctx)->drawAircraft(args);
        };
        gui::waterfall.afterWaterfallDraw.bindHandler(&afterWaterfallDrawListener);

        onPlayStateChange.ctx = this;
        onPlayStateChange.handler = [](bool, void* ctx) {
            ADSBDecoderModule* module = (ADSBDecoderModule*)ctx;
            module->resetDecoder();
            module->clearAircraft();
        };
        gui::mainWindow.onPlayStateChange.bindHandler(&onPlayStateChange);

        gui::menu.registerEntry(name, menuHandler, this, this);
        enable();
    }

    ~ADSBDecoderModule() override {
        disable();
        gui::mainWindow.onPlayStateChange.unbindHandler(&onPlayStateChange);
        gui::waterfall.afterWaterfallDraw.unbindHandler(&afterWaterfallDrawListener);
        gui::menu.removeEntry(name);
    }

    void postInit() override {}

    void enable() override {
        if (enabled) { return; }

        double offset = ADSB_FREQUENCY - gui::waterfall.getCenterFrequency();
        vfo = sigpath::iqFrontEnd.addVFO(internalVFOName, adsb::DEMOD_SAMPLE_RATE, CHANNEL_BANDWIDTH, offset);
        if (!vfo) {
            flog::error("ADS-B Decoder failed to create its internal VFO");
            return;
        }

        enabled = true;
        workerRunning = true;
        refreshTuning();
        worker = std::thread(&ADSBDecoderModule::workerLoop, this);
        flog::info("ADS-B Decoder enabled");
    }

    void disable() override {
        if (!enabled) { return; }

        workerRunning = false;
        if (vfo) { vfo->out.stopReader(); }
        if (worker.joinable()) { worker.join(); }
        if (vfo) {
            sigpath::iqFrontEnd.removeVFO(internalVFOName);
            vfo = nullptr;
        }

        enabled = false;
        onFrequency = false;
        resetDecoder();
        clearAircraft();
        flog::info("ADS-B Decoder disabled");
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
        if (config.conf[name].contains("errorCorrectionBits")) {
            errorCorrectionSetting = config.conf[name]["errorCorrectionBits"].get<int>();
        }
        if (config.conf[name].contains("secondsToKeepResults")) {
            retentionSeconds = config.conf[name]["secondsToKeepResults"].get<int>();
        }
        errorCorrectionSetting = (std::max)(0, (std::min)(2, errorCorrectionSetting));
        retentionSeconds = (std::max)(5, (std::min)(300, retentionSeconds));
        processingEnabled = processingEnabledSetting;
        errorCorrectionBits = errorCorrectionSetting;
        config.conf[name]["processingEnabled"] = processingEnabledSetting;
        config.conf[name]["errorCorrectionBits"] = errorCorrectionSetting;
        config.conf[name]["secondsToKeepResults"] = retentionSeconds;
        config.release(true);
    }

    void saveSetting(const char* key, const json& value) {
        config.acquire();
        config.conf[name][key] = value;
        config.release(true);
    }

    bool hasChannelCoverage() const {
        double sourceBandwidth = (std::min)(gui::waterfall.getBandwidth(), sigpath::iqFrontEnd.getEffectiveSamplerate());
        if (sourceBandwidth + 1.0 < CHANNEL_BANDWIDTH) { return false; }
        double center = gui::waterfall.getCenterFrequency();
        double halfSource = sourceBandwidth / 2.0;
        double halfChannel = CHANNEL_BANDWIDTH / 2.0;
        return ADSB_FREQUENCY - halfChannel >= center - halfSource &&
               ADSB_FREQUENCY + halfChannel <= center + halfSource;
    }

    void refreshTuning() {
        if (!vfo) { return; }
        double offset = ADSB_FREQUENCY - gui::waterfall.getCenterFrequency();
        if (std::fabs(offset - currentOffset) >= 1.0) {
            currentOffset = offset;
            vfo->setOffset(offset);
        }

        bool covered = hasChannelCoverage();
        bool wasCovered = onFrequency.exchange(covered);
        if (covered != wasCovered) {
            resetDecoder();
            clearAircraft();
            flog::info("ADS-B Decoder on frequency: {}", covered);
        }
    }

    void workerLoop() {
        std::vector<float> power;
        int64_t lastRateUpdate = currentTimeMillis();
        uint64_t lastValidCount = 0;

        while (workerRunning.load()) {
            int count = vfo->out.read();
            if (count < 0) { break; }

            refreshTuning();
            bool shouldDecode = processingEnabled.load() && onFrequency.load();
            if (shouldDecode) {
                power.resize((std::size_t)count);
                for (int i = 0; i < count; i++) {
                    const dsp::complex_t& sample = vfo->out.readBuf[i];
                    power[(std::size_t)i] = sample.re * sample.re + sample.im * sample.im;
                }
                {
                    std::lock_guard<std::mutex> lock(decoderMutex);
                    decoder.setMaxCorrections(errorCorrectionBits.load());
                    decoder.process(power.data(), power.size());
                    const adsb::DemodStats& decoderStats = decoder.stats();
                    validFrames = decoderStats.valid;
                    correctedFrames = decoderStats.corrected;
                }
            } else {
                resetDecoder();
            }
            vfo->out.flush();

            int64_t now = currentTimeMillis();
            if (now - lastRateUpdate >= 1000) {
                uint64_t valid = validFrames.load();
                double seconds = (double)(now - lastRateUpdate) / 1000.0;
                frameRate = (float)((valid - lastValidCount) / seconds);
                lastValidCount = valid;
                lastRateUpdate = now;
            }
        }
    }

    void onFrame(const adsb::DecodedFrame& frame) {
        int64_t timestamp = sigpath::iqFrontEnd.getCurrentStreamTime();
        std::lock_guard<std::mutex> lock(trackerMutex);
        tracker.update(frame, timestamp);
        trackedAircraft = tracker.size();
    }

    void resetDecoder() {
        std::lock_guard<std::mutex> lock(decoderMutex);
        decoder.reset();
    }

    void clearAircraft() {
        std::lock_guard<std::mutex> lock(trackerMutex);
        tracker.clear();
        trackedAircraft = 0;
    }

    std::vector<adsb::AircraftState> aircraftSnapshot(int64_t now) {
        std::lock_guard<std::mutex> lock(trackerMutex);
        tracker.expire(now, retentionSeconds);
        trackedAircraft = tracker.size();
        return tracker.snapshot();
    }

    void drawAircraft(const ImGui::WaterFall::WaterfallDrawArgs& args) {
        if (enabled && !hasChannelCoverage()) {
            bool wasCovered = onFrequency.exchange(false);
            if (wasCovered) {
                resetDecoder();
                clearAircraft();
            }
        }
        if (!enabled || !processingEnabled.load() || !onFrequency.load()) { return; }
        int64_t now = sigpath::iqFrontEnd.getCurrentStreamTime();
        std::vector<adsb::AircraftState> aircraft = aircraftSnapshot(now);
        if (aircraft.empty()) { return; }

        ImDrawList* drawList = args.window->DrawList;
        drawList->PushClipRect(args.wfMin, args.wfMax, true);
        ImGui::PushFont(style::baseFont);
        float lineHeight = ImGui::GetTextLineHeight();
        float x = args.wfMin.x + 3.0f;
        float y = args.wfMax.y - lineHeight - 7.0f;

        for (const adsb::AircraftState& state : aircraft) {
            std::string identity = state.callsign.empty() ? formatICAO(state.icao) : state.callsign;
            std::string label = identity;
            if (state.hasAltitude) { label += "  " + std::to_string(state.altitudeFeet) + " ft"; }

            ImVec2 badgeSize = ImGui::CalcTextSize("ADS-B");
            ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
            float width = badgeSize.x + labelSize.x + 15.0f;
            float height = lineHeight + 4.0f;
            if (x + width > args.wfMax.x - 3.0f) {
                x = args.wfMin.x + 3.0f;
                y -= height + 3.0f;
            }
            if (y < args.wfMin.y) { break; }

            float age = (float)(now - state.lastSeenMillis) / 1000.0f;
            float life = (float)retentionSeconds;
            float fade = age > life * 0.67f ? (std::max)(0.15f, (life - age) / (life * 0.33f)) : 1.0f;
            int alpha = (int)(220.0f * fade);
            ImVec2 cardMin(x, y);
            ImVec2 cardMax(x + width, y + height);
            ImVec2 badgeMax(x + badgeSize.x + 7.0f, y + height);

            drawList->AddRectFilled(cardMin, cardMax, IM_COL32(18, 28, 38, alpha), 2.0f);
            drawList->AddRectFilled(cardMin, badgeMax, IM_COL32(0, 125, 210, alpha), 2.0f);
            drawList->AddText(ImVec2(x + 3.0f, y + 2.0f), IM_COL32(255, 255, 255, (int)(255.0f * fade)), "ADS-B");
            drawList->AddText(ImVec2(badgeMax.x + 4.0f, y + 2.0f), IM_COL32(255, 255, 255, (int)(255.0f * fade)), label.c_str());

            if (ImGui::IsMouseHoveringRect(cardMin, cardMax) && !gui::mainWindow.showMenu) {
                ImGui::BeginTooltip();
                ImGui::Text("ICAO: %s", formatICAO(state.icao).c_str());
                ImGui::Text("Callsign: %s", state.callsign.empty() ? "Unavailable" : state.callsign.c_str());
                ImGui::Text("Category: %s", state.category.empty() ? "Unavailable" : state.category.c_str());
                if (state.hasAltitude) { ImGui::Text("Pressure altitude: %d ft", state.altitudeFeet); }
                else { ImGui::Text("Pressure altitude: Unavailable"); }
                ImGui::Text("Signal: %.1f dBFS", state.rssiDbfs);
                ImGui::Text("Last seen: %.1f sec ago", (std::max)(0.0f, age));
                ImGui::Text("Messages: %llu", (unsigned long long)state.messageCount);
                ImGui::Text("Last correction: %d bit(s)", state.lastCorrectedBits);
                ImGui::Text("Corrected messages: %llu", (unsigned long long)state.correctedMessages);
                ImGui::Separator();
                ImGui::Text("Raw: %s", state.lastRawFrame.c_str());
                ImGui::EndTooltip();
            }
            x += width + 3.0f;
        }

        ImGui::PopFont();
        drawList->PopClipRect();
    }

    static void menuHandler(void* ctx) {
        ADSBDecoderModule* module = (ADSBDecoderModule*)ctx;

        ImGui::LeftLabel("Decode ADS-B");
        if (ImGui::Checkbox(CONCAT("##_adsb_processing_", module->name), &module->processingEnabledSetting)) {
            module->processingEnabled = module->processingEnabledSetting;
            module->saveSetting("processingEnabled", module->processingEnabledSetting);
            if (!module->processingEnabledSetting) {
                module->resetDecoder();
                module->clearAircraft();
            }
        }

        ImGui::LeftLabel("Error correction");
        ImGui::FillWidth();
        if (ImGui::Combo(CONCAT("##_adsb_correction_", module->name), &module->errorCorrectionSetting,
                         "0 bits\0" "1 bit\0" "Up to 2 bits\0")) {
            module->errorCorrectionSetting = (std::max)(0, (std::min)(2, module->errorCorrectionSetting));
            module->errorCorrectionBits = module->errorCorrectionSetting;
            module->saveSetting("errorCorrectionBits", module->errorCorrectionSetting);
        }

        ImGui::LeftLabel("Keep results (sec)");
        ImGui::FillWidth();
        if (ImGui::SliderInt(CONCAT("##_adsb_retention_", module->name), &module->retentionSeconds, 5, 300)) {
            module->saveSetting("secondsToKeepResults", module->retentionSeconds);
        }

        if (ImGui::Button(CONCAT("Clear results##_adsb_", module->name))) { module->clearAircraft(); }

        ImGui::Separator();
        if (!module->processingEnabled.load()) { ImGui::Text("Status: decoding disabled"); }
        else if (module->onFrequency.load()) { ImGui::Text("Status: receiving 1090 MHz"); }
        else if (sigpath::iqFrontEnd.getEffectiveSamplerate() < CHANNEL_BANDWIDTH) {
            ImGui::Text("Status: insufficient source bandwidth");
        } else { ImGui::Text("Status: tune until 1090 MHz is fully visible"); }
        ImGui::Text("Rate: %.1f frames/sec", module->frameRate.load());
        ImGui::Text("Aircraft: %llu", (unsigned long long)module->trackedAircraft.load());
        ImGui::Text("Valid: %llu  Corrected: %llu", (unsigned long long)module->validFrames.load(),
                    (unsigned long long)module->correctedFrames.load());
    }

    std::string name;
    std::string internalVFOName;
    bool enabled = false;
    bool processingEnabledSetting = true;
    int errorCorrectionSetting = 1;
    int retentionSeconds = DEFAULT_RETENTION_SECONDS;

    std::atomic_bool workerRunning{false};
    std::atomic_bool processingEnabled{true};
    std::atomic_bool onFrequency{false};
    std::atomic_int errorCorrectionBits{1};
    std::atomic<uint64_t> candidates{0};
    std::atomic<uint64_t> validFrames{0};
    std::atomic<uint64_t> correctedFrames{0};
    std::atomic<uint64_t> trackedAircraft{0};
    std::atomic<float> frameRate{0.0f};

    double currentOffset = 1.0e30;
    dsp::channel::RxVFO* vfo = nullptr;
    std::thread worker;
    adsb::StreamDecoder decoder;
    std::mutex decoderMutex;
    adsb::AircraftTracker tracker;
    std::mutex trackerMutex;

    EventHandler<ImGui::WaterFall::WaterfallDrawArgs> afterWaterfallDrawListener;
    EventHandler<bool> onPlayStateChange;
};

MOD_EXPORT void _INIT_() {
    json defaults = json({});
    config.setPath(std::string(core::getRoot()) + "/adsb_decoder_config.json");
    config.load(defaults);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new ADSBDecoderModule(std::move(name));
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (ADSBDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
