#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <module.h>
#include <signal_path/signal_path.h>
#include <dsp/channel/rx_vfo.h>
#include <dsp/protocol/lora/lora.h>
#include <protocol/meshtastic/meshtastic.h>
#include <utils/flog.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

SDRPP_MOD_INFO{
    /* Name:            */ "meshtastic_decoder",
    /* Description:     */ "Receive-only Meshtastic EdgeFastLow decoder",
    /* Author:          */ "SDR++ Brown",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

ConfigManager config;

namespace {

constexpr std::size_t MAX_PENDING_PACKETS = 64;
constexpr std::size_t MAX_PACKET_HISTORY = 100;
constexpr std::size_t MAX_RECENT_PACKETS = 256;
namespace meshtastic = protocol::meshtastic;

struct ConfiguredProfile {
    std::string id;
    meshtastic::RadioProfile radio;
    std::string channelName;
    std::string psk;
    int pskEncoding = 0;
};

json profileJson(const char* id, const meshtastic::RadioProfile& profile, const char* channelName) {
    return {
        { "id", id },
        { "radio", {
            { "name", profile.name },
            { "region", "EU_868" },
            { "bandwidthHz", profile.bandwidth },
            { "spreadingFactor", profile.spreadingFactor },
            { "codingRateDenominator", profile.codingRate + 4 },
            { "frequencySlot", profile.frequencySlot },
            { "preambleLength", profile.preambleLength },
            { "syncWord", profile.syncWord }
        } },
        { "channel", {
            { "name", channelName },
            { "psk", "AQ==" },
            { "pskEncoding", "base64" }
        } }
    };
}

json defaultConfig() {
    return {
        { "profiles", json::array({
            profileJson("EdgeFastLow", meshtastic::edgeFastLowProfile(), "EdgeFastLow"),
            profileJson("LongFast", meshtastic::longFastProfile(), "LongFast")
        }) },
        { "instances", json::object() }
    };
}

bool parseConfiguredProfile(const json& value, ConfiguredProfile& configured, std::string& error) {
    try {
        configured.id = value.at("id").get<std::string>();
        const json& radio = value.at("radio");
        if (radio.value("region", "EU_868") != "EU_868") { throw std::invalid_argument("unsupported region"); }
        configured.radio.name = radio.value("name", configured.id);
        configured.radio.mode = meshtastic::RadioMode::Custom;
        configured.radio.bandwidth = radio.at("bandwidthHz").get<double>();
        configured.radio.spreadingFactor = radio.at("spreadingFactor").get<int>();
        const int codingRateDenominator = radio.at("codingRateDenominator").get<int>();
        configured.radio.codingRate = codingRateDenominator - 4;
        configured.radio.frequencySlot = radio.at("frequencySlot").get<int>();
        configured.radio.preambleLength = radio.value("preambleLength", 16);
        configured.radio.syncWord = radio.value("syncWord", 0x2B);
        if (radio.contains("frequencyHz") && !radio["frequencyHz"].is_null()) {
            configured.radio.frequencyOverride = radio["frequencyHz"].get<double>();
        }
        const json& channel = value.at("channel");
        configured.channelName = channel.at("name").get<std::string>();
        configured.psk = channel.value("psk", "AQ==");
        const std::string encoding = channel.value("pskEncoding", "base64");
        if (encoding != "base64" && encoding != "hex") { throw std::invalid_argument("unsupported PSK encoding"); }
        configured.pskEncoding = encoding == "hex" ? 1 : 0;
        if (configured.id.empty() || configured.channelName.empty()) { throw std::invalid_argument("empty id or channel name"); }
        if (configured.radio.bandwidth <= 0.0 || configured.radio.spreadingFactor < 5 ||
            configured.radio.spreadingFactor > 12 || codingRateDenominator < 5 || codingRateDenominator > 8 ||
            configured.radio.frequencySlot < 1 || configured.radio.preambleLength < 8) {
            throw std::invalid_argument("invalid radio parameters");
        }
        meshtastic::calculateFrequency(configured.radio);
        std::vector<uint8_t> encodedPsk;
        const bool validPsk = configured.pskEncoding == 0
            ? meshtastic::decodeBase64Psk(configured.psk, encodedPsk)
            : meshtastic::decodeHexPsk(configured.psk, encodedPsk);
        if (!validPsk || (meshtastic::expandPsk(encodedPsk.data(), encodedPsk.size()).empty() && !encodedPsk.empty())) {
            throw std::invalid_argument("invalid PSK");
        }
        return true;
    }
    catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

std::string bytesToHex(const uint8_t* data, std::size_t size) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; i++) {
        if (i) { stream << ' '; }
        stream << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return stream.str();
}

std::string nodeName(uint32_t node) {
    if (node == meshtastic::BROADCAST_NODE) { return "broadcast"; }
    char text[16];
    std::snprintf(text, sizeof(text), "!%08x", node);
    return text;
}

std::string currentTime() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%H:%M:%S", &local);
    return text;
}

struct DisplayPacket {
    std::string time;
    meshtastic::DecodedPacket decoded;
    std::string rawPayload;
    std::string decryptedPayload;
    unsigned receptions = 1;
};

struct ObservedNode {
    std::string longName;
    std::string shortName;
    meshtastic::Position position;
    bool hasPosition = false;
    float lastSnr = 0.0f;
};

}

class MeshtasticDecoderModule : public ModuleManager::Instance {
public:
    explicit MeshtasticDecoderModule(std::string instanceName)
        : name(std::move(instanceName)), iqInputName(name + ".efl.iq"), iqInput(iqInputName.c_str()) {
        loadConfig();
        const ConfiguredProfile& configured = configuredProfiles[profileSelection];
        profile = configured.radio;
        std::vector<uint8_t> encodedPsk;
        if (configured.pskEncoding == 0) { meshtastic::decodeBase64Psk(configured.psk, encodedPsk); }
        else { meshtastic::decodeHexPsk(configured.psk, encodedPsk); }
        channels.push_back(meshtastic::makeChannel(configured.channelName, encodedPsk));
        std::snprintf(channelNameInput.data(), channelNameInput.size(), "%s", configured.channelName.c_str());
        std::snprintf(channelPskInput.data(), channelPskInput.size(), "%s", configured.psk.c_str());
        pskEncoding = configured.pskEncoding;
        syncRadioInputs();
        protocolDecoder.setChannels(channels);
        channelHash = channels.front().hash;
        receiveFrequency = meshtastic::calculateFrequency(profile);
        const double decoderRate = profile.bandwidth * 4.0;
        const double inputSampleRate = sigpath::iqFrontEnd.getEffectiveSamplerate();
        const double offset = receiveFrequency - gui::waterfall.getCenterFrequency();
        fixedVfo.init(&iqInput, inputSampleRate, decoderRate, profile.bandwidth * 1.5, offset);
        loraDecoder.init(&fixedVfo.out, decoderRate, meshtastic::makeLoRaConfig(profile), frameHandler, this);
        currentInputSampleRate.store(inputSampleRate);
        currentOffset.store(offset);

        sampleRateListener.ctx = this;
        sampleRateListener.handler = [](double sampleRate, void* context) {
            auto* self = static_cast<MeshtasticDecoderModule*>(context);
            self->currentInputSampleRate.store(sampleRate, std::memory_order_relaxed);
            self->fixedVfo.setInSamplerate(sampleRate);
        };
        sigpath::iqFrontEnd.onEffectiveSampleRateChange.bindHandler(&sampleRateListener);

        tuneListener.ctx = this;
        tuneListener.handler = [](double centerFrequency, void* context) {
            auto* self = static_cast<MeshtasticDecoderModule*>(context);
            const double newOffset = self->receiveFrequency - centerFrequency;
            self->currentOffset.store(newOffset, std::memory_order_relaxed);
            self->fixedVfo.setOffset(newOffset);
            self->loraDecoder.reset();
        };
        sigpath::sourceManager.onTuneChanged.bindHandler(&tuneListener);

        startReceiver();
        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~MeshtasticDecoderModule() override {
        gui::menu.removeEntry(name);
        sigpath::sourceManager.onTuneChanged.unbindHandler(&tuneListener);
        sigpath::iqFrontEnd.onEffectiveSampleRateChange.unbindHandler(&sampleRateListener);
        if (enabled) {
            loraDecoder.stop();
            fixedVfo.stop();
            sigpath::iqFrontEnd.unbindIQStream(&iqInput);
        }
    }

    void postInit() override {}

    void enable() override {
        if (enabled) { return; }
        startReceiver();
    }

    void disable() override {
        if (!enabled) { return; }
        loraDecoder.stop();
        fixedVfo.stop();
        sigpath::iqFrontEnd.unbindIQStream(&iqInput);
        enabled = false;
    }

    bool isEnabled() override { return enabled; }

private:
    void loadConfig() {
        std::string selectedId = "EdgeFastLow";
        bool modified = false;
        config.acquire();
        try {
            json& instance = config.conf["instances"][name];
            if (!instance.is_object()) {
                instance = json::object();
                modified = true;
            }
            selectedId = instance.value("selectedProfile", selectedId);
            if (!instance.contains("selectedProfile")) {
                instance["selectedProfile"] = selectedId;
                modified = true;
            }
            showRawPayload.store(instance.value("showRawPayload", false));
            showDecryptedPayload.store(instance.value("showDecryptedPayload", false));
            showNonText.store(instance.value("showNonText", true));
            showUnsupportedChannels.store(instance.value("showUnsupportedChannels", false));
            showDuplicates.store(instance.value("showDuplicates", false));
            for (const auto& value : config.conf["profiles"]) {
                ConfiguredProfile configured;
                std::string error;
                if (parseConfiguredProfile(value, configured, error)) { configuredProfiles.push_back(std::move(configured)); }
                else { flog::error("Meshtastic: invalid JSON profile: {}", error); }
            }
        }
        catch (const std::exception& exception) {
            flog::error("Meshtastic: could not read configuration: {}", exception.what());
        }
        config.release(modified);

        if (configuredProfiles.empty()) {
            ConfiguredProfile efl;
            ConfiguredProfile longFast;
            std::string error;
            parseConfiguredProfile(profileJson("EdgeFastLow", meshtastic::edgeFastLowProfile(), "EdgeFastLow"), efl, error);
            parseConfiguredProfile(profileJson("LongFast", meshtastic::longFastProfile(), "LongFast"), longFast, error);
            configuredProfiles = { efl, longFast };
            configurationError = "No valid JSON profiles; using built-in defaults";
        }
        const auto selected = std::find_if(configuredProfiles.begin(), configuredProfiles.end(), [&](const ConfiguredProfile& configured) {
            return configured.id == selectedId;
        });
        profileSelection = selected == configuredProfiles.end() ? 0 : static_cast<int>(selected - configuredProfiles.begin());
    }

    void saveProfiles() {
        json profiles = json::array();
        for (const ConfiguredProfile& configured : configuredProfiles) {
            json value = profileJson(configured.id.c_str(), configured.radio, configured.channelName.c_str());
            value["channel"]["psk"] = configured.psk;
            value["channel"]["pskEncoding"] = configured.pskEncoding == 0 ? "base64" : "hex";
            if (configured.radio.frequencyOverride) { value["radio"]["frequencyHz"] = *configured.radio.frequencyOverride; }
            profiles.push_back(std::move(value));
        }
        config.acquire();
        config.conf["profiles"] = std::move(profiles);
        config.release(true);
    }

    void saveInstanceSetting(const char* key, const json& value) {
        config.acquire();
        config.conf["instances"][name][key] = value;
        config.release(true);
    }

    void syncRadioInputs() {
        customBandwidthKhz = static_cast<float>(profile.bandwidth / 1000.0);
        customSf = profile.spreadingFactor;
        customCrDenominator = profile.codingRate + 4;
        customSlot = profile.frequencySlot;
    }

    void applyConfiguredProfile(int selection) {
        if (selection < 0 || selection >= static_cast<int>(configuredProfiles.size())) { return; }
        profileSelection = selection;
        const ConfiguredProfile& configured = configuredProfiles[profileSelection];
        setProfile(configured.radio);
        std::vector<uint8_t> encodedPsk;
        if (configured.pskEncoding == 0) { meshtastic::decodeBase64Psk(configured.psk, encodedPsk); }
        else { meshtastic::decodeHexPsk(configured.psk, encodedPsk); }
        setChannel(configured.channelName, encodedPsk);
        std::snprintf(channelNameInput.data(), channelNameInput.size(), "%s", configured.channelName.c_str());
        std::snprintf(channelPskInput.data(), channelPskInput.size(), "%s", configured.psk.c_str());
        pskEncoding = configured.pskEncoding;
        syncRadioInputs();
        saveInstanceSetting("selectedProfile", configured.id);
    }

    void startReceiver() {
        const double offset = receiveFrequency - gui::waterfall.getCenterFrequency();
        currentOffset.store(offset, std::memory_order_relaxed);
        fixedVfo.setInSamplerate(sigpath::iqFrontEnd.getEffectiveSamplerate());
        fixedVfo.setOffset(offset);
        fixedVfo.reset();
        loraDecoder.reset();
        sigpath::iqFrontEnd.bindIQStream(&iqInput);
        fixedVfo.start();
        loraDecoder.start();
        enabled = true;
    }

    void setProfile(const meshtastic::RadioProfile& newProfile) {
        const bool restartDecoder = enabled;
        if (restartDecoder) { loraDecoder.stop(); }
        profile = newProfile;
        receiveFrequency = meshtastic::calculateFrequency(profile);
        const double decoderRate = profile.bandwidth * 4.0;
        fixedVfo.setOutSamplerate(decoderRate, profile.bandwidth * 1.5);
        fixedVfo.setOffset(receiveFrequency - gui::waterfall.getCenterFrequency());
        loraDecoder.setInputSampleRate(decoderRate);
        loraDecoder.setConfig(meshtastic::makeLoRaConfig(profile));
        fixedVfo.reset();
        loraDecoder.reset();
        currentOffset.store(receiveFrequency - gui::waterfall.getCenterFrequency(), std::memory_order_relaxed);
        if (restartDecoder) { loraDecoder.start(); }
    }

    void setChannel(const std::string& channelName, const std::vector<uint8_t>& encodedPsk) {
        const bool restartDecoder = enabled;
        if (restartDecoder) { loraDecoder.stop(); }
        channels = { meshtastic::makeChannel(channelName, encodedPsk) };
        protocolDecoder.setChannels(channels);
        channelHash = channels.front().hash;
        if (restartDecoder) { loraDecoder.start(); }
    }

    static void frameHandler(const dsp::protocol::lora::Frame& frame, void* context) {
        static_cast<MeshtasticDecoderModule*>(context)->handleFrame(frame);
    }

    void handleFrame(const dsp::protocol::lora::Frame& frame) {
        loraFrames.fetch_add(1, std::memory_order_relaxed);
        const meshtastic::RxMetadata metadata{ frame.snrDb, frame.rssiDb, frame.frequencyErrorHz };
        const auto result = protocolDecoder.decode(frame.payload.data(), frame.payload.size(), metadata,
                                                   frame.payloadCrcPresent && frame.payloadCrcValid);
        if (result.status != meshtastic::DecodeStatus::TooShort &&
            result.status != meshtastic::DecodeStatus::InvalidHeader &&
            result.status != meshtastic::DecodeStatus::LoRaCrcInvalid) {
            meshtasticHeaders.fetch_add(1, std::memory_order_relaxed);
        }
        if (result.matchingChannels) { matchingChannel.fetch_add(1, std::memory_order_relaxed); }
        if (result.status != meshtastic::DecodeStatus::Ok) {
            decodeErrors.fetch_add(1, std::memory_order_relaxed);
            if (showUnsupportedChannels.load(std::memory_order_relaxed) &&
                result.status != meshtastic::DecodeStatus::LoRaCrcInvalid) {
                flog::debug("Meshtastic: {}", meshtastic::decodeStatusText(result.status));
            }
            return;
        }
        const auto& data = result.decoded.data;
        protobufDecoded.fetch_add(1, std::memory_order_relaxed);
        if (data.portNum == meshtastic::TEXT_MESSAGE_APP) { textMessages.fetch_add(1, std::memory_order_relaxed); }
        else if (data.portNum == meshtastic::POSITION_APP) { positionMessages.fetch_add(1, std::memory_order_relaxed); }
        else if (data.portNum == meshtastic::NODEINFO_APP) { nodeInfoMessages.fetch_add(1, std::memory_order_relaxed); }
        else if (data.portNum == meshtastic::TELEMETRY_APP) { telemetryMessages.fetch_add(1, std::memory_order_relaxed); }
        else if (data.portNum == meshtastic::ROUTING_APP) { routingMessages.fetch_add(1, std::memory_order_relaxed); }
        else if (!showNonText.load(std::memory_order_relaxed)) { return; }

        DisplayPacket display;
        display.time = currentTime();
        display.decoded = result.decoded;
        if (showRawPayload.load(std::memory_order_relaxed)) {
            display.rawPayload = bytesToHex(frame.payload.data(), frame.payload.size());
        }
        if (showDecryptedPayload.load(std::memory_order_relaxed)) {
            display.decryptedPayload = bytesToHex(result.decoded.plaintext.data(), result.decoded.plaintext.size());
        }

        std::lock_guard<std::mutex> lock(queueMutex);
        if (pendingPackets.size() == MAX_PENDING_PACKETS) { pendingPackets.pop_front(); }
        pendingPackets.push_back(std::move(display));
    }

    void drainPackets() {
        std::deque<DisplayPacket> incoming;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            incoming.swap(pendingPackets);
        }
        while (!incoming.empty()) {
            DisplayPacket& next = incoming.front();
            const auto& packet = next.decoded.packet;
            const auto& data = next.decoded.data;
            ObservedNode& node = observedNodes[packet.header.from];
            node.lastSnr = packet.metadata.snrDb;
            if (data.portNum == meshtastic::NODEINFO_APP) {
                node.longName = data.nodeInfo.longName;
                node.shortName = data.nodeInfo.shortName;
            }
            else if (data.portNum == meshtastic::POSITION_APP) {
                node.position = data.position;
                node.hasPosition = true;
            }
            const uint64_t identity = (static_cast<uint64_t>(packet.header.from) << 32u) | packet.header.id;
            const bool duplicate = recentPackets.find(identity) != recentPackets.end();
            recentPackets[identity]++;
            recentOrder.push_back(identity);
            while (recentOrder.size() > MAX_RECENT_PACKETS) {
                const uint64_t expired = recentOrder.front();
                recentOrder.pop_front();
                auto found = recentPackets.find(expired);
                if (found != recentPackets.end() && --found->second == 0) { recentPackets.erase(found); }
            }
            if (duplicate && !showDuplicates.load(std::memory_order_relaxed)) {
                for (auto it = packetHistory.rbegin(); it != packetHistory.rend(); ++it) {
                    if (it->decoded.packet.header.from == packet.header.from &&
                        it->decoded.packet.header.id == packet.header.id) {
                        it->receptions++;
                        it->decoded.packet.metadata = packet.metadata;
                        break;
                    }
                }
                incoming.pop_front();
                continue;
            }
            if (packetHistory.size() == MAX_PACKET_HISTORY) { packetHistory.pop_front(); }
            packetHistory.push_back(std::move(incoming.front()));
            incoming.pop_front();
        }
    }

    static void drawPacket(const DisplayPacket& display) {
        const auto& packet = display.decoded.packet;
        const auto& header = packet.header;
        const auto& data = display.decoded.data;
        ImGui::Separator();
        ImGui::Text("%s  %s -> %s", display.time.c_str(), nodeName(header.from).c_str(), nodeName(header.to).c_str());
        if (data.portNum == meshtastic::TEXT_MESSAGE_APP) {
            ImGui::TextUnformatted("TEXT:");
            ImGui::SameLine();
            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted(data.text.data(), data.text.data() + data.text.size());
            ImGui::PopTextWrapPos();
        }
        else if (data.portNum == meshtastic::POSITION_APP) {
            const auto& position = data.position;
            if (position.hasLatitude && position.hasLongitude) {
                ImGui::Text("POSITION: %.7f, %.7f", position.latitude, position.longitude);
            }
            else { ImGui::TextUnformatted("POSITION: partial update"); }
            if (position.hasAltitude) { ImGui::SameLine(); ImGui::Text("alt %d m", position.altitude); }
            if (position.hasTimestamp) { ImGui::Text("Position time: %u", position.timestamp); }
        }
        else if (data.portNum == meshtastic::NODEINFO_APP) {
            const auto& info = data.nodeInfo;
            ImGui::TextWrapped("NODEINFO: %s [%s] %s", info.longName.c_str(), info.shortName.c_str(), info.id.c_str());
        }
        else if (data.portNum == meshtastic::TELEMETRY_APP) {
            const auto& telemetry = data.telemetry;
            ImGui::TextUnformatted(telemetry.kind == meshtastic::TelemetryKind::Device ? "TELEMETRY: device" : "TELEMETRY: environment");
            if (telemetry.hasBatteryLevel) { ImGui::Text("Battery: %u%%", telemetry.batteryLevel); }
            if (telemetry.hasVoltage) { ImGui::SameLine(); ImGui::Text("%.2f V", telemetry.voltage); }
            if (telemetry.hasTemperature) { ImGui::Text("Temperature: %.1f C", telemetry.temperature); }
            if (telemetry.hasHumidity) { ImGui::SameLine(); ImGui::Text("Humidity: %.1f%%", telemetry.humidity); }
            if (telemetry.hasPressure) { ImGui::Text("Pressure: %.1f hPa", telemetry.pressure); }
        }
        else if (data.portNum == meshtastic::ROUTING_APP) {
            ImGui::Text("ROUTING: variant %u, error %u", data.routing.variant, data.routing.errorReason);
        }
        else {
            ImGui::Text("Port %u, %zu bytes", data.portNum, data.payload.size());
        }
        ImGui::Text("id=%08x  hop %u/%u  ch=0x%02x  next=%02x relay=%02x", header.id, header.hopLimit,
                    header.hopStart, header.channelHash, header.nextHop, header.relayNode);
        ImGui::Text("SNR %.1f dB  RSSI %.1f dB  CFO %+.0f Hz%s%s", packet.metadata.snrDb, packet.metadata.rssiDb,
                    packet.metadata.frequencyErrorHz, header.wantAck ? "  ACK" : "", header.viaMqtt ? "  MQTT" : "");
        if (display.receptions > 1) { ImGui::SameLine(); ImGui::Text("x%u", display.receptions); }
        if (!display.rawPayload.empty()) { ImGui::TextWrapped("LoRa: %s", display.rawPayload.c_str()); }
        if (!display.decryptedPayload.empty()) { ImGui::TextWrapped("Data: %s", display.decryptedPayload.c_str()); }
    }

    static void menuHandler(void* context) {
        auto* self = static_cast<MeshtasticDecoderModule*>(context);
        self->drainPackets();

        if (!self->enabled) { style::beginDisabled(); }
        const char* selectedProfile = self->configuredProfiles[self->profileSelection].id.c_str();
        if (ImGui::BeginCombo(("Radio profile##" + self->name).c_str(), selectedProfile)) {
            for (int index = 0; index < static_cast<int>(self->configuredProfiles.size()); index++) {
                const bool selected = index == self->profileSelection;
                if (ImGui::Selectable(self->configuredProfiles[index].id.c_str(), selected)) {
                    self->applyConfiguredProfile(index);
                    self->configurationError.clear();
                }
                if (selected) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }
        if (ImGui::CollapsingHeader("Radio configuration")) {
            ImGui::InputFloat(("Bandwidth (kHz)##" + self->name).c_str(), &self->customBandwidthKhz, 0.0f, 0.0f, "%.3f");
            ImGui::InputInt(("Spreading factor##" + self->name).c_str(), &self->customSf);
            ImGui::InputInt(("Coding denominator##" + self->name).c_str(), &self->customCrDenominator);
            ImGui::InputInt(("Frequency slot##" + self->name).c_str(), &self->customSlot);
            if (ImGui::Button(("Apply radio##" + self->name).c_str())) {
                if (self->customBandwidthKhz > 0.0f && self->customSf >= 5 && self->customSf <= 12 &&
                    self->customCrDenominator >= 5 && self->customCrDenominator <= 8 && self->customSlot >= 1) {
                    meshtastic::RadioProfile custom = self->profile;
                    custom.bandwidth = self->customBandwidthKhz * 1000.0;
                    custom.spreadingFactor = self->customSf;
                    custom.codingRate = self->customCrDenominator - 4;
                    custom.frequencySlot = self->customSlot;
                    custom.frequencyOverride.reset();
                    try {
                        self->setProfile(custom);
                        self->configuredProfiles[self->profileSelection].radio = custom;
                        self->saveProfiles();
                        self->configurationError.clear();
                    }
                    catch (const std::exception& error) { self->configurationError = error.what(); }
                }
                else { self->configurationError = "Invalid BW/SF/CR/slot"; }
            }
        }
        ImGui::Text("Mode: %s", self->profile.mode == meshtastic::RadioMode::Custom ? "Custom" : "Preset");
        ImGui::Text("Frequency: %.5f MHz", self->receiveFrequency / 1.0e6);
        ImGui::Text("BW / SF / CR: %.1f kHz / SF%d / 4/%d", self->profile.bandwidth / 1000.0,
                    self->profile.spreadingFactor, self->profile.codingRate + 4);
        if (ImGui::CollapsingHeader("Channel configuration")) {
            ImGui::InputText(("Name##" + self->name).c_str(), self->channelNameInput.data(), self->channelNameInput.size());
            ImGui::InputText(("PSK##" + self->name).c_str(), self->channelPskInput.data(), self->channelPskInput.size(),
                             ImGuiInputTextFlags_Password);
            ImGui::RadioButton(("Base64##" + self->name).c_str(), &self->pskEncoding, 0); ImGui::SameLine();
            ImGui::RadioButton(("Hex##" + self->name).c_str(), &self->pskEncoding, 1);
            if (ImGui::Button(("Apply channel##" + self->name).c_str())) {
                std::vector<uint8_t> encodedPsk;
                const bool valid = self->pskEncoding == 0
                    ? meshtastic::decodeBase64Psk(self->channelPskInput.data(), encodedPsk)
                    : meshtastic::decodeHexPsk(self->channelPskInput.data(), encodedPsk);
                const std::vector<uint8_t> expanded = meshtastic::expandPsk(encodedPsk.data(), encodedPsk.size());
                if (valid && (!expanded.empty() || encodedPsk.empty()) && self->channelNameInput[0]) {
                    self->setChannel(self->channelNameInput.data(), encodedPsk);
                    ConfiguredProfile& configured = self->configuredProfiles[self->profileSelection];
                    configured.channelName = self->channelNameInput.data();
                    configured.psk = self->channelPskInput.data();
                    configured.pskEncoding = self->pskEncoding;
                    self->saveProfiles();
                    self->configurationError.clear();
                }
                else { self->configurationError = "Invalid channel name or PSK"; }
            }
        }
        ImGui::Text("Channel: %s (hash 0x%02x)", self->channels.front().name.c_str(), self->channelHash);
        if (!self->configurationError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", self->configurationError.c_str());
        }
        ImGui::Text("Configuration: meshtastic_decoder_config.json [%s]",
                    self->configuredProfiles[self->profileSelection].id.c_str());
        const double offset = self->currentOffset.load(std::memory_order_relaxed);
        const double inputRate = self->currentInputSampleRate.load(std::memory_order_relaxed);
        const bool inPassband = std::abs(offset) + self->profile.bandwidth * 0.75 <= inputRate * 0.5;
        if (!self->enabled) { ImGui::TextUnformatted("Status: Disabled"); }
        else if (inPassband) { ImGui::Text("Status: Listening (offset %+.0f Hz)", offset); }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Status: EFL outside SDR passband (%+.0f Hz)", offset);
            ImGui::Text("Tune the SDR center so %.5f MHz is inside its passband.", self->receiveFrequency / 1.0e6);
        }

        bool toggle = self->showRawPayload.load(std::memory_order_relaxed);
        if (ImGui::Checkbox(("Show raw LoRa payload##" + self->name).c_str(), &toggle)) {
            self->showRawPayload.store(toggle); self->saveInstanceSetting("showRawPayload", toggle);
        }
        toggle = self->showDecryptedPayload.load(std::memory_order_relaxed);
        if (ImGui::Checkbox(("Show decrypted payload##" + self->name).c_str(), &toggle)) {
            self->showDecryptedPayload.store(toggle); self->saveInstanceSetting("showDecryptedPayload", toggle);
        }
        toggle = self->showNonText.load(std::memory_order_relaxed);
        if (ImGui::Checkbox(("Show non-text packets##" + self->name).c_str(), &toggle)) {
            self->showNonText.store(toggle); self->saveInstanceSetting("showNonText", toggle);
        }
        toggle = self->showUnsupportedChannels.load(std::memory_order_relaxed);
        if (ImGui::Checkbox(("Log unsupported channel packets##" + self->name).c_str(), &toggle)) {
            self->showUnsupportedChannels.store(toggle); self->saveInstanceSetting("showUnsupportedChannels", toggle);
        }
        toggle = self->showDuplicates.load(std::memory_order_relaxed);
        if (ImGui::Checkbox(("Show duplicate receptions##" + self->name).c_str(), &toggle)) {
            self->showDuplicates.store(toggle); self->saveInstanceSetting("showDuplicates", toggle);
        }

        ImGui::Separator();
        ImGui::Text("LoRa frames: %llu", static_cast<unsigned long long>(self->loraFrames.load()));
        ImGui::Text("Meshtastic headers: %llu", static_cast<unsigned long long>(self->meshtasticHeaders.load()));
        ImGui::Text("Matching EFL channel: %llu", static_cast<unsigned long long>(self->matchingChannel.load()));
        ImGui::Text("Data protobufs: %llu", static_cast<unsigned long long>(self->protobufDecoded.load()));
        ImGui::Text("Decode errors: %llu", static_cast<unsigned long long>(self->decodeErrors.load()));
        ImGui::Text("Text / Position / NodeInfo: %llu / %llu / %llu",
                    static_cast<unsigned long long>(self->textMessages.load()),
                    static_cast<unsigned long long>(self->positionMessages.load()),
                    static_cast<unsigned long long>(self->nodeInfoMessages.load()));
        ImGui::Text("Telemetry / Routing: %llu / %llu",
                    static_cast<unsigned long long>(self->telemetryMessages.load()),
                    static_cast<unsigned long long>(self->routingMessages.load()));

        if (!self->observedNodes.empty() && ImGui::CollapsingHeader("Observed nodes")) {
            for (const auto& entry : self->observedNodes) {
                const auto& node = entry.second;
                ImGui::Text("%s  %s [%s]  SNR %.1f", nodeName(entry.first).c_str(), node.longName.c_str(),
                            node.shortName.c_str(), node.lastSnr);
                if (node.hasPosition && node.position.hasLatitude && node.position.hasLongitude) {
                    ImGui::Text("  %.7f, %.7f", node.position.latitude, node.position.longitude);
                }
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Packets");
        if (self->packetHistory.empty()) { ImGui::TextDisabled("No decoded EFL packets yet"); }
        for (auto it = self->packetHistory.rbegin(); it != self->packetHistory.rend(); ++it) { drawPacket(*it); }
        if (!self->enabled) { style::endDisabled(); }
    }

    std::string name;
    bool enabled = false;
    std::string iqInputName;
    dsp::stream<dsp::complex_t> iqInput;
    dsp::channel::RxVFO fixedVfo;
    dsp::protocol::lora::LoRaDecoder loraDecoder;
    meshtastic::RadioProfile profile = meshtastic::edgeFastLowProfile();
    std::vector<ConfiguredProfile> configuredProfiles;
    std::vector<meshtastic::Channel> channels;
    meshtastic::Decoder protocolDecoder;
    double receiveFrequency = 0.0;
    int profileSelection = 0;
    float customBandwidthKhz = 62.5f;
    int customSf = 8;
    int customCrDenominator = 8;
    int customSlot = 1;
    std::array<char, 64> channelNameInput{};
    std::array<char, 128> channelPskInput{};
    int pskEncoding = 0;
    std::string configurationError;
    uint8_t channelHash = 0;
    EventHandler<double> sampleRateListener;
    EventHandler<double> tuneListener;
    std::atomic<double> currentInputSampleRate{0.0};
    std::atomic<double> currentOffset{0.0};

    std::atomic_bool showRawPayload{false};
    std::atomic_bool showDecryptedPayload{false};
    std::atomic_bool showNonText{true};
    std::atomic_bool showUnsupportedChannels{false};
    std::atomic_bool showDuplicates{false};
    std::atomic<uint64_t> loraFrames{0};
    std::atomic<uint64_t> meshtasticHeaders{0};
    std::atomic<uint64_t> matchingChannel{0};
    std::atomic<uint64_t> protobufDecoded{0};
    std::atomic<uint64_t> textMessages{0};
    std::atomic<uint64_t> positionMessages{0};
    std::atomic<uint64_t> nodeInfoMessages{0};
    std::atomic<uint64_t> telemetryMessages{0};
    std::atomic<uint64_t> routingMessages{0};
    std::atomic<uint64_t> decodeErrors{0};
    std::mutex queueMutex;
    std::deque<DisplayPacket> pendingPackets;
    std::deque<DisplayPacket> packetHistory;
    std::unordered_map<uint64_t, unsigned> recentPackets;
    std::deque<uint64_t> recentOrder;
    std::unordered_map<uint32_t, ObservedNode> observedNodes;
};

MOD_EXPORT void _INIT_() {
    json defaults = defaultConfig();
    config.setPath(std::string(core::getRoot()) + "/meshtastic_decoder_config.json");
    config.load(defaults);
    config.acquire();
    bool modified = false;
    if (!config.conf.contains("profiles") || !config.conf["profiles"].is_array() || config.conf["profiles"].empty()) {
        config.conf["profiles"] = defaults["profiles"];
        modified = true;
    }
    if (!config.conf.contains("instances") || !config.conf["instances"].is_object()) {
        config.conf["instances"] = json::object();
        modified = true;
    }
    config.release(modified);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new MeshtasticDecoderModule(std::move(name));
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete static_cast<MeshtasticDecoderModule*>(instance);
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
