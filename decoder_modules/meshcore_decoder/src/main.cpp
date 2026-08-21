#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <module.h>
#include <signal_path/signal_path.h>
#include <dsp/channel/rx_vfo.h>
#include <dsp/protocol/lora/lora.h>
#include <protocol/meshcore/meshcore.h>
#include <utils/flog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

SDRPP_MOD_INFO{
    /* Name:            */ "meshcore_decoder",
    /* Description:     */ "Receive-only MeshCore decoder",
    /* Author:          */ "SDR++ Brown",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

ConfigManager config;

namespace {

namespace meshcore = protocol::meshcore;
constexpr std::size_t MAX_PENDING_PACKETS = 64;
constexpr std::size_t MAX_PACKET_HISTORY = 100;
constexpr std::size_t MAX_RECENT_PACKETS = 256;

struct ConfiguredProfile {
    std::string id;
    meshcore::RadioProfile radio;
};

struct ConfiguredChannel {
    meshcore::GroupChannel channel;
    std::string secretText;
    int encoding = 1;
};

struct DisplayPacket {
    std::string time;
    meshcore::DecodedPacket decoded;
    std::string raw;
    uint64_t identity = 0;
    unsigned receptions = 1;
};

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

std::string bytesToHex(const uint8_t* data, std::size_t size, std::size_t limit = 255) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    const std::size_t count = (std::min)(size, limit);
    for (std::size_t i = 0; i < count; i++) {
        if (i) { stream << ' '; }
        stream << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    if (count < size) { stream << " ..."; }
    return stream.str();
}

uint64_t packetIdentity(const meshcore::Packet& packet) {
    uint64_t hash = 1469598103934665603ull;
    hash = (hash ^ static_cast<uint8_t>(packet.payloadType)) * 1099511628211ull;
    for (uint8_t value : packet.payload) { hash = (hash ^ value) * 1099511628211ull; }
    return hash;
}

json profileJson(const char* id, const meshcore::RadioProfile& profile) {
    return {
        { "id", id },
        { "frequencyHz", profile.frequency },
        { "bandwidthHz", profile.bandwidth },
        { "spreadingFactor", profile.spreadingFactor },
        { "codingRateDenominator", profile.codingRate + 4 },
        { "preambleLength", profile.preambleLength },
        { "syncWord", profile.syncWord }
    };
}

json defaultConfig() {
    return {
        { "profiles", json::array({
            profileJson("EU/UK Narrow", meshcore::euNarrowProfile()),
            profileJson("EU/UK Long Range", meshcore::euLongRangeProfile())
        }) },
        { "channels", json::array({
            {
                { "name", "Public" },
                { "secret", "8b3387e9c5cdea6ac9e5edbaa115cd72" },
                { "encoding", "hex" }
            }
        }) },
        { "instances", json::object() }
    };
}

bool parseProfile(const json& value, ConfiguredProfile& configured, std::string& error) {
    try {
        configured.id = value.at("id").get<std::string>();
        configured.radio.name = configured.id;
        configured.radio.frequency = value.at("frequencyHz").get<double>();
        configured.radio.bandwidth = value.at("bandwidthHz").get<double>();
        configured.radio.spreadingFactor = value.at("spreadingFactor").get<int>();
        configured.radio.codingRate = value.at("codingRateDenominator").get<int>() - 4;
        configured.radio.preambleLength = value.value("preambleLength", 8);
        configured.radio.syncWord = value.value("syncWord", 0x1424);
        if (configured.id.empty()) { throw std::invalid_argument("empty profile id"); }
        meshcore::makeLoRaConfig(configured.radio);
        return true;
    }
    catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool parseChannel(const json& value, ConfiguredChannel& configured, std::string& error) {
    try {
        const std::string name = value.at("name").get<std::string>();
        configured.secretText = value.at("secret").get<std::string>();
        const std::string encoding = value.value("encoding", "hex");
        if (encoding != "base64" && encoding != "hex") { throw std::invalid_argument("unsupported encoding"); }
        configured.encoding = encoding == "base64" ? 0 : 1;
        std::vector<uint8_t> secret;
        const bool valid = configured.encoding == 0
            ? meshcore::decodeBase64Secret(configured.secretText, secret)
            : meshcore::decodeHexSecret(configured.secretText, secret);
        if (!valid || !meshcore::makeGroupChannel(name, std::move(secret), configured.channel)) {
            throw std::invalid_argument("invalid channel name or secret");
        }
        return true;
    }
    catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

}

class MeshCoreDecoderModule : public ModuleManager::Instance {
public:
    explicit MeshCoreDecoderModule(std::string instanceName)
        : name(std::move(instanceName)), iqInputName(name + ".meshcore.iq"), iqInput(iqInputName.c_str()) {
        loadConfig();
        profile = profiles[profileSelection].radio;
        applyChannelInputs(0);
        protocolDecoder.setChannels(channelSnapshot());
        receiveFrequency = profile.frequency;
        const double decoderRate = profile.bandwidth * 4.0;
        const double inputSampleRate = sigpath::iqFrontEnd.getEffectiveSamplerate();
        const double offset = receiveFrequency - gui::waterfall.getCenterFrequency();
        fixedVfo.init(&iqInput, inputSampleRate, decoderRate, profile.bandwidth * 1.5, offset);
        loraDecoder.init(&fixedVfo.out, decoderRate, meshcore::makeLoRaConfig(profile), frameHandler, this);
        currentInputSampleRate.store(inputSampleRate);
        currentOffset.store(offset);

        sampleRateListener.ctx = this;
        sampleRateListener.handler = [](double sampleRate, void* context) {
            auto* self = static_cast<MeshCoreDecoderModule*>(context);
            self->currentInputSampleRate.store(sampleRate, std::memory_order_relaxed);
            self->fixedVfo.setInSamplerate(sampleRate);
        };
        sigpath::iqFrontEnd.onEffectiveSampleRateChange.bindHandler(&sampleRateListener);

        tuneListener.ctx = this;
        tuneListener.handler = [](double centerFrequency, void* context) {
            auto* self = static_cast<MeshCoreDecoderModule*>(context);
            const double offset = self->receiveFrequency - centerFrequency;
            self->currentOffset.store(offset, std::memory_order_relaxed);
            self->fixedVfo.setOffset(offset);
            self->loraDecoder.reset();
        };
        sigpath::sourceManager.onTuneChanged.bindHandler(&tuneListener);

        startReceiver();
        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~MeshCoreDecoderModule() override {
        gui::menu.removeEntry(name);
        sigpath::sourceManager.onTuneChanged.unbindHandler(&tuneListener);
        sigpath::iqFrontEnd.onEffectiveSampleRateChange.unbindHandler(&sampleRateListener);
        disable();
    }

    void postInit() override {}
    void enable() override { if (!enabled) { startReceiver(); } }
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
        std::string selectedId = "EU/UK Narrow";
        bool modified = false;
        config.acquire();
        try {
            json& instance = config.conf["instances"][name];
            if (!instance.is_object()) { instance = json::object(); modified = true; }
            selectedId = instance.value("selectedProfile", selectedId);
            if (!instance.contains("selectedProfile")) { instance["selectedProfile"] = selectedId; modified = true; }
            showRaw.store(instance.value("showRaw", false));
            showDuplicates.store(instance.value("showDuplicates", false));
            for (const auto& value : config.conf["profiles"]) {
                ConfiguredProfile configured;
                std::string error;
                if (parseProfile(value, configured, error)) { profiles.push_back(std::move(configured)); }
                else { flog::error("MeshCore: invalid JSON profile: {}", error); }
            }
            for (const auto& value : config.conf["channels"]) {
                ConfiguredChannel configured;
                std::string error;
                if (parseChannel(value, configured, error)) { channels.push_back(std::move(configured)); }
                else { flog::error("MeshCore: invalid JSON channel: {}", error); }
            }
        }
        catch (const std::exception& exception) { flog::error("MeshCore: could not read configuration: {}", exception.what()); }
        config.release(modified);

        if (profiles.empty()) {
            ConfiguredProfile narrow;
            ConfiguredProfile longRange;
            std::string error;
            parseProfile(profileJson("EU/UK Narrow", meshcore::euNarrowProfile()), narrow, error);
            parseProfile(profileJson("EU/UK Long Range", meshcore::euLongRangeProfile()), longRange, error);
            profiles = { narrow, longRange };
            configurationError = "No valid JSON profiles; using defaults";
        }
        if (channels.empty()) {
            ConfiguredChannel publicChannel;
            std::string error;
            parseChannel(defaultConfig()["channels"][0], publicChannel, error);
            channels = { publicChannel };
            configurationError = "No valid JSON channels; using Public";
        }
        const auto selected = std::find_if(profiles.begin(), profiles.end(), [&](const ConfiguredProfile& configured) {
            return configured.id == selectedId;
        });
        profileSelection = selected == profiles.end() ? 0 : static_cast<int>(selected - profiles.begin());
    }

    std::vector<meshcore::GroupChannel> channelSnapshot() const {
        std::vector<meshcore::GroupChannel> result;
        for (const ConfiguredChannel& configured : channels) { result.push_back(configured.channel); }
        return result;
    }

    void saveInstanceSetting(const char* key, const json& value) {
        config.acquire();
        config.conf["instances"][name][key] = value;
        config.release(true);
    }

    void saveChannels() {
        json values = json::array();
        for (const ConfiguredChannel& configured : channels) {
            values.push_back({
                { "name", configured.channel.name },
                { "secret", configured.secretText },
                { "encoding", configured.encoding == 0 ? "base64" : "hex" }
            });
        }
        config.acquire();
        config.conf["channels"] = std::move(values);
        config.release(true);
    }

    void applyChannelInputs(int selection) {
        channelSelection = selection;
        const ConfiguredChannel& configured = channels[channelSelection];
        std::snprintf(channelNameInput.data(), channelNameInput.size(), "%s", configured.channel.name.c_str());
        std::snprintf(channelSecretInput.data(), channelSecretInput.size(), "%s", configured.secretText.c_str());
        secretEncoding = configured.encoding;
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

    void applyProfile(int selection) {
        if (selection < 0 || selection >= static_cast<int>(profiles.size())) { return; }
        const bool restart = enabled;
        if (restart) { loraDecoder.stop(); }
        profileSelection = selection;
        profile = profiles[selection].radio;
        receiveFrequency = profile.frequency;
        const double decoderRate = profile.bandwidth * 4.0;
        fixedVfo.setOutSamplerate(decoderRate, profile.bandwidth * 1.5);
        fixedVfo.setOffset(receiveFrequency - gui::waterfall.getCenterFrequency());
        loraDecoder.setInputSampleRate(decoderRate);
        loraDecoder.setConfig(meshcore::makeLoRaConfig(profile));
        fixedVfo.reset();
        loraDecoder.reset();
        currentOffset.store(receiveFrequency - gui::waterfall.getCenterFrequency(), std::memory_order_relaxed);
        if (restart) { loraDecoder.start(); }
        saveInstanceSetting("selectedProfile", profiles[selection].id);
    }

    void applyDecoderChannels() {
        const bool restart = enabled;
        if (restart) { loraDecoder.stop(); }
        protocolDecoder.setChannels(channelSnapshot());
        if (restart) { loraDecoder.start(); }
    }

    static void frameHandler(const dsp::protocol::lora::Frame& frame, void* context) {
        static_cast<MeshCoreDecoderModule*>(context)->handleFrame(frame);
    }

    void handleFrame(const dsp::protocol::lora::Frame& frame) {
        loraFrames.fetch_add(1, std::memory_order_relaxed);
        const meshcore::RxMetadata metadata{ frame.snrDb, frame.rssiDb, frame.frequencyErrorHz };
        const auto result = protocolDecoder.decode(frame.payload.data(), frame.payload.size(), metadata,
                                                   frame.payloadCrcPresent && frame.payloadCrcValid);
        if (result.status != meshcore::DecodeStatus::Ok) {
            decodeErrors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        decodedPackets.fetch_add(1, std::memory_order_relaxed);
        if (result.decoded.hasAdvertisement) { advertisements.fetch_add(1, std::memory_order_relaxed); }
        if (result.decoded.hasGroupMessage) { groupMessages.fetch_add(1, std::memory_order_relaxed); }

        DisplayPacket display;
        display.time = currentTime();
        display.decoded = result.decoded;
        display.identity = packetIdentity(result.decoded.packet);
        if (showRaw.load(std::memory_order_relaxed)) {
            display.raw = bytesToHex(frame.payload.data(), frame.payload.size());
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
            const bool duplicate = recentPackets.find(next.identity) != recentPackets.end();
            recentPackets[next.identity]++;
            recentOrder.push_back(next.identity);
            while (recentOrder.size() > MAX_RECENT_PACKETS) {
                const uint64_t expired = recentOrder.front();
                recentOrder.pop_front();
                auto found = recentPackets.find(expired);
                if (found != recentPackets.end() && --found->second == 0) { recentPackets.erase(found); }
            }
            if (duplicate && !showDuplicates.load(std::memory_order_relaxed)) {
                for (auto it = history.rbegin(); it != history.rend(); ++it) {
                    if (it->identity == next.identity) { it->receptions++; break; }
                }
            }
            else {
                if (history.size() == MAX_PACKET_HISTORY) { history.pop_front(); }
                history.push_back(std::move(next));
            }
            incoming.pop_front();
        }
    }

    static void drawPacket(const DisplayPacket& display) {
        const meshcore::Packet& packet = display.decoded.packet;
        ImGui::Separator();
        ImGui::Text("%s  %s / %s  hops %u x %u", display.time.c_str(), meshcore::payloadTypeText(packet.payloadType),
                    meshcore::routeTypeText(packet.routeType), packet.pathHashCount, display.receptions);
        if (display.decoded.hasGroupMessage) {
            const auto& message = display.decoded.groupMessage;
            ImGui::Text("GROUP [%s] hash=%02x", message.channelName.c_str(), message.channelHash);
            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted(message.text.c_str());
            ImGui::PopTextWrapPos();
        }
        else if (display.decoded.hasAdvertisement) {
            const auto& advert = display.decoded.advertisement;
            ImGui::Text("ADVERT: %s  type=%u  key=%s", advert.name.empty() ? "unnamed" : advert.name.c_str(),
                        advert.nodeType, bytesToHex(advert.publicKey.data(), 6).c_str());
            if (advert.hasLocation) { ImGui::Text("Position: %.6f, %.6f", advert.latitude, advert.longitude); }
            ImGui::TextDisabled("Signature captured (verification not implemented)");
        }
        else if (display.decoded.hasAck) { ImGui::Text("ACK checksum: %08x", display.decoded.ackChecksum); }
        else if (display.decoded.hasPeerHashes) {
            ImGui::Text("Encrypted peer traffic: %02x -> %02x", display.decoded.sourceHash,
                        display.decoded.destinationHash);
        }
        else if (!display.decoded.channelName.empty()) {
            ImGui::Text("Authenticated group data [%s], %zu plaintext bytes",
                        display.decoded.channelName.c_str(), display.decoded.plaintext.size());
        }
        ImGui::Text("SNR %.1f dB  RSSI %.1f dB  CFO %+.0f Hz", packet.metadata.snrDb, packet.metadata.rssiDb,
                    packet.metadata.frequencyErrorHz);
        if (!packet.path.empty()) { ImGui::TextWrapped("Path: %s", bytesToHex(packet.path.data(), packet.path.size()).c_str()); }
        if (!display.raw.empty()) { ImGui::TextWrapped("LoRa: %s", display.raw.c_str()); }
    }

    static void menuHandler(void* context) {
        auto* self = static_cast<MeshCoreDecoderModule*>(context);
        self->drainPackets();
        if (!self->enabled) { style::beginDisabled(); }

        if (ImGui::BeginCombo(("Radio profile##" + self->name).c_str(), self->profiles[self->profileSelection].id.c_str())) {
            for (int index = 0; index < static_cast<int>(self->profiles.size()); index++) {
                const bool selected = index == self->profileSelection;
                if (ImGui::Selectable(self->profiles[index].id.c_str(), selected)) {
                    try { self->applyProfile(index); self->configurationError.clear(); }
                    catch (const std::exception& exception) { self->configurationError = exception.what(); }
                }
                if (selected) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Frequency: %.6f MHz", self->profile.frequency / 1.0e6);
        ImGui::Text("BW / SF / CR: %.1f kHz / SF%d / 4/%d", self->profile.bandwidth / 1000.0,
                    self->profile.spreadingFactor, self->profile.codingRate + 4);
        ImGui::Text("Sync: 0x%04x", self->profile.syncWord);

        if (ImGui::CollapsingHeader("Group channels")) {
            if (ImGui::BeginCombo(("Channel##" + self->name).c_str(), self->channels[self->channelSelection].channel.name.c_str())) {
                for (int index = 0; index < static_cast<int>(self->channels.size()); index++) {
                    const bool selected = index == self->channelSelection;
                    if (ImGui::Selectable(self->channels[index].channel.name.c_str(), selected)) {
                        self->applyChannelInputs(index);
                    }
                    if (selected) { ImGui::SetItemDefaultFocus(); }
                }
                ImGui::EndCombo();
            }
            ImGui::InputText(("Name##" + self->name).c_str(), self->channelNameInput.data(), self->channelNameInput.size());
            ImGui::InputText(("Secret##" + self->name).c_str(), self->channelSecretInput.data(), self->channelSecretInput.size(),
                             ImGuiInputTextFlags_Password);
            ImGui::RadioButton(("Base64##" + self->name).c_str(), &self->secretEncoding, 0); ImGui::SameLine();
            ImGui::RadioButton(("Hex##" + self->name).c_str(), &self->secretEncoding, 1);
            if (ImGui::Button(("Apply channel##" + self->name).c_str())) {
                std::vector<uint8_t> secret;
                const bool valid = self->secretEncoding == 0
                    ? meshcore::decodeBase64Secret(self->channelSecretInput.data(), secret)
                    : meshcore::decodeHexSecret(self->channelSecretInput.data(), secret);
                meshcore::GroupChannel channel;
                if (valid && meshcore::makeGroupChannel(self->channelNameInput.data(), std::move(secret), channel)) {
                    ConfiguredChannel& configured = self->channels[self->channelSelection];
                    configured.channel = std::move(channel);
                    configured.secretText = self->channelSecretInput.data();
                    configured.encoding = self->secretEncoding;
                    self->applyDecoderChannels();
                    self->saveChannels();
                    self->configurationError.clear();
                }
                else { self->configurationError = "Secret must decode to 16 or 32 bytes"; }
            }
        }

        if (!self->configurationError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", self->configurationError.c_str());
        }
        ImGui::TextUnformatted("Configuration: meshcore_decoder_config.json");
        const double offset = self->currentOffset.load(std::memory_order_relaxed);
        const double inputRate = self->currentInputSampleRate.load(std::memory_order_relaxed);
        const bool inPassband = std::abs(offset) + self->profile.bandwidth * 0.75 <= inputRate * 0.5;
        if (!self->enabled) { ImGui::TextUnformatted("Status: Disabled"); }
        else if (inPassband) { ImGui::Text("Status: Listening (offset %+.0f Hz)", offset); }
        else { ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Status: profile outside SDR passband"); }

        bool toggle = self->showRaw.load(std::memory_order_relaxed);
        if (ImGui::Checkbox(("Show raw payload##" + self->name).c_str(), &toggle)) {
            self->showRaw.store(toggle); self->saveInstanceSetting("showRaw", toggle);
        }
        toggle = self->showDuplicates.load(std::memory_order_relaxed);
        if (ImGui::Checkbox(("Show duplicate receptions##" + self->name).c_str(), &toggle)) {
            self->showDuplicates.store(toggle); self->saveInstanceSetting("showDuplicates", toggle);
        }

        ImGui::Separator();
        ImGui::Text("LoRa / decoded / errors: %llu / %llu / %llu",
                    static_cast<unsigned long long>(self->loraFrames.load()),
                    static_cast<unsigned long long>(self->decodedPackets.load()),
                    static_cast<unsigned long long>(self->decodeErrors.load()));
        ImGui::Text("Advertisements / group messages: %llu / %llu",
                    static_cast<unsigned long long>(self->advertisements.load()),
                    static_cast<unsigned long long>(self->groupMessages.load()));
        ImGui::Separator();
        ImGui::TextUnformatted("Packets");
        if (self->history.empty()) { ImGui::TextDisabled("No decoded MeshCore packets yet"); }
        for (auto it = self->history.rbegin(); it != self->history.rend(); ++it) { drawPacket(*it); }
        if (!self->enabled) { style::endDisabled(); }
    }

    std::string name;
    bool enabled = false;
    std::string iqInputName;
    dsp::stream<dsp::complex_t> iqInput;
    dsp::channel::RxVFO fixedVfo;
    dsp::protocol::lora::LoRaDecoder loraDecoder;
    meshcore::Decoder protocolDecoder;
    std::vector<ConfiguredProfile> profiles;
    std::vector<ConfiguredChannel> channels;
    meshcore::RadioProfile profile;
    int profileSelection = 0;
    int channelSelection = 0;
    std::array<char, 64> channelNameInput{};
    std::array<char, 128> channelSecretInput{};
    int secretEncoding = 1;
    double receiveFrequency = 0.0;
    std::string configurationError;
    EventHandler<double> sampleRateListener;
    EventHandler<double> tuneListener;
    std::atomic<double> currentInputSampleRate{0.0};
    std::atomic<double> currentOffset{0.0};
    std::atomic_bool showRaw{false};
    std::atomic_bool showDuplicates{false};
    std::atomic<uint64_t> loraFrames{0};
    std::atomic<uint64_t> decodedPackets{0};
    std::atomic<uint64_t> decodeErrors{0};
    std::atomic<uint64_t> advertisements{0};
    std::atomic<uint64_t> groupMessages{0};
    std::mutex queueMutex;
    std::deque<DisplayPacket> pendingPackets;
    std::deque<DisplayPacket> history;
    std::unordered_map<uint64_t, unsigned> recentPackets;
    std::deque<uint64_t> recentOrder;
};

MOD_EXPORT void _INIT_() {
    json defaults = defaultConfig();
    config.setPath(std::string(core::getRoot()) + "/meshcore_decoder_config.json");
    config.load(defaults);
    config.acquire();
    bool modified = false;
    for (const char* key : { "profiles", "channels" }) {
        if (!config.conf.contains(key) || !config.conf[key].is_array() || config.conf[key].empty()) {
            config.conf[key] = defaults[key];
            modified = true;
        }
    }
    if (!config.conf.contains("instances") || !config.conf["instances"].is_object()) {
        config.conf["instances"] = json::object();
        modified = true;
    }
    config.release(modified);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new MeshCoreDecoderModule(std::move(name));
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete static_cast<MeshCoreDecoderModule*>(instance);
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
