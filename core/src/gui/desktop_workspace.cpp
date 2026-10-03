#define IMGUI_DEFINE_MATH_OPERATORS
#include <gui/main_window.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/menus/display.h>
#include <gui/menus/source.h>
#include <gui/menus/receiver_manager.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <utils/hrfreq.h>
#include <imgui_internal.h>
#include <algorithm>
#include "../../decoder_modules/radio/src/radio_interface.h"

namespace {
    void tooltip(const char* text) {
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", text); }
    }

    void persist(const char* key, const json& value) {
        core::configManager.acquire();
        core::configManager.conf[key] = value;
        core::configManager.release(true);
    }

    float remainingLineWidth() {
        return ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x -
               ImGui::GetItemRectMax().x - ImGui::GetStyle().ItemSpacing.x;
    }
}

void MainWindow::drawDesktopHeader(ImGui::WaterfallVFO* vfo) {
    const float scale = style::uiScale;
    ImGui::BeginChild("Radio Header", ImVec2(0, 96 * scale), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (ImGui::Button("Panels##workspace_panels") || ImGui::IsKeyPressed(ImGuiKey_Menu, false)) {
        showMenu = !showMenu;
        persist("showMenu", showMenu);
    }
    tooltip("Show or hide the control panel");
    ImGui::SameLine();
    if (ImGui::Button("SDR++ BROWN##workspace_about")) { showCredits = true; }
    tooltip("About SDR++ Brown (Esc to close)");
    ImGui::SameLine();

    const bool canStart = !playButtonLocked && !sigpath::sourceManager.getSelectedName().empty();
    ImGui::BeginDisabled(!playing && !canStart);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(playing ? ImGuiCol_HeaderActive : ImGuiCol_Button));
    if (ImGui::Button(playing ? "Stop RX##workspace_play" : "Start RX##workspace_play", ImVec2(95 * scale, 0))) {
        setPlayState(!playing);
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    tooltip("Start / stop reception (End)");
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_End, false) && (playing || canStart)) {
        setPlayState(!playing);
    }
    if (autostart) {
        autostart = false;
        if (canStart) { setPlayState(true); }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(playing);
    const auto& selectedSource = sigpath::sourceManager.getSelectedName();
    ImGui::SetNextItemWidth((std::min)(210 * scale, (std::max)(100 * scale, ImGui::GetContentRegionAvail().x)));
    if (ImGui::BeginCombo("##workspace_source", selectedSource.empty() ? "Choose a source" : selectedSource.c_str())) {
        for (const auto& name : sigpath::sourceManager.getSourceNames()) {
            const bool selected = name == selectedSource;
            if (ImGui::Selectable(name.c_str(), selected)) {
                sourcemenu::selectSource(name);
                persist("source", name);
            }
            if (selected) { ImGui::SetItemDefaultFocus(); }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    tooltip("Input source; stop reception to change devices");
    if (remainingLineWidth() > 75 * scale) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::GetStyleColorVec4(playing ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled),
                           "%s", playing ? "LIVE" : "STANDBY");
    }

    const float frequencyY = ImGui::GetCursorPosY() + 4 * scale;
    ImGui::SetCursorPosY(frequencyY);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    gui::freqSelect.draw();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetCursorPosY(frequencyY + 4 * scale);
    receiver_manager_menu::drawSelector(100 * scale);
    tooltip("Active receiver; frequency and audio controls apply to this receiver");
    if (remainingLineWidth() > 180 * scale) {
        ImGui::SameLine();
        ImGui::SetCursorPosY(frequencyY + 4 * scale);
        sigpath::sinkManager.showVolumeSlider(gui::waterfall.selectedVFO, "##workspace_volume_", 165 * scale, 22 * scale, 0, true);
    }
    if (remainingLineWidth() > 100 * scale) {
        ImGui::SameLine();
        ImGui::SetCursorPosY(frequencyY + 7 * scale);
        ImGui::TextDisabled("SNR %.1f dB", vfo ? gui::waterfall.selectedVFOSNR : 0.0f);
    }
    ImGui::EndChild();
}

void MainWindow::drawDesktopReceiverControls() {
    const float scale = style::uiScale;
    const float scrollHeight = ImGui::GetContentRegionAvail().x < 700 * scale ? ImGui::GetStyle().ScrollbarSize : 0;
    ImGui::BeginChild("Receiver Controls", ImVec2(0, ImGui::GetFrameHeight() + 24 * scale + scrollHeight), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const std::string receiver = gui::waterfall.selectedVFO;
    const bool radio = !receiver.empty() && core::modComManager.interfaceExists(receiver) &&
                       core::modComManager.getModuleName(receiver) == "radio";
    int mode = -1;
    if (radio) { core::modComManager.callInterface(receiver, RADIO_IFACE_CMD_GET_MODE, nullptr, &mode); }
    ImGui::BeginDisabled(!radio);
    const std::pair<const char*, int> modes[] = {
        { "AM", RADIO_IFACE_MODE_AM }, { "NFM", RADIO_IFACE_MODE_NFM },
        { "WFM", RADIO_IFACE_MODE_WFM }, { "USB", RADIO_IFACE_MODE_USB },
        { "LSB", RADIO_IFACE_MODE_LSB }, { "CW", RADIO_IFACE_MODE_CW },
        { "DSB", RADIO_IFACE_MODE_DSB }, { "RAW", RADIO_IFACE_MODE_RAW }
    };
    for (const auto& [label, value] : modes) {
        if (value == mode) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive)); }
        const std::string id = std::string(label) + "##workspace_mode";
        if (ImGui::Button(id.c_str(), ImVec2(47 * scale, 0))) {
            int requestedMode = value;
            core::modComManager.callInterface(receiver, RADIO_IFACE_CMD_SET_MODE, &requestedMode, nullptr);
        }
        if (value == mode) { ImGui::PopStyleColor(); }
        ImGui::SameLine();
    }
    float bandwidth = 0;
    if (radio) { core::modComManager.callInterface(receiver, RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &bandwidth); }
    ImGui::SetNextItemWidth(110 * scale);
    if (ImGui::DragFloat("##workspace_bandwidth", &bandwidth, 100.0f, 50.0f, 300000.0f, "%.0f Hz")) {
        core::modComManager.callInterface(receiver, RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, nullptr);
    }
    tooltip("Receiver bandwidth; drag or Ctrl-click to enter a value");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool center = tuningMode == tuner::TUNER_MODE_CENTER;
    if (ImGui::Button(center ? "Center tuning##workspace_tuning" : "Free tuning##workspace_tuning")) {
        tuningMode = center ? tuner::TUNER_MODE_NORMAL : tuner::TUNER_MODE_CENTER;
        gui::waterfall.VFOMoveSingleClick = !center;
        if (!center) { tuner::tune(tuningMode, receiver, gui::freqSelect.frequency); }
        persist("centerTuning", !center);
    }
    tooltip("Choose whether tuning moves the receiver within the spectrum or recenters the input");
    ImGui::EndChild();
}

void MainWindow::drawDesktopWorkspace(ImGui::WaterfallVFO* vfo) {
    const float scale = style::uiScale;
    lockWaterfallControls |= showCredits;
    displaymenu::checkKeybinds();
    displayVariousWindows();
    drawDesktopReceiverControls();

    if (!workspaceInitialized) {
        core::configManager.acquire();
        workspaceSection = (std::clamp)(core::configManager.conf.value("workspaceSection", 0), 0, 2);
        core::configManager.release();
        workspaceInitialized = true;
    }
    const ImVec2 origin = ImGui::GetCursorPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float footerHeight = ImGui::GetFrameHeight() + 6 * scale;
    const float contentHeight = (std::max)(64 * scale, available.y - footerHeight);
    const float panelWidth = showMenu ? (std::clamp)((float)menuWidth, 280 * scale, (std::max)(280 * scale, available.x * 0.42f)) : 0;
    const float dividerWidth = showMenu ? 8 * scale : 0;

    if (showMenu) {
        ImGui::BeginChild("Control Panel", ImVec2(panelWidth, contentHeight), true);
        const char* sections[] = { "Radio", "Modules", "Settings" };
        const float tabWidth = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        for (int i = 0; i < 3; ++i) {
            if (i) { ImGui::SameLine(); }
            if (workspaceSection == i) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive)); }
            const bool chosen = ImGui::Button((std::string(sections[i]) + "##workspace_section").c_str(), ImVec2(tabWidth, 0));
            if (workspaceSection == i) { ImGui::PopStyleColor(); }
            if (chosen) {
                workspaceSection = i;
                firstMenuRender = true;
                persist("workspaceSection", i);
            }
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##workspace_search", "Find a panel...", workspaceSearch, sizeof(workspaceSearch));
        ImGui::Separator();
        ImGui::BeginChild("Panel Contents", ImVec2(0, 0), false);
        if (gui::menu.draw(firstMenuRender, workspaceSection + 1, workspaceSearch)) {
            core::configManager.acquire();
            json elements = json::array();
            for (const auto& option : gui::menu.order) {
                elements.push_back({ { "name", option.name }, { "open", option.open } });
            }
            core::configManager.conf["menuElements"] = elements;
            for (const auto& [name, instance] : core::moduleManager.instances) {
                if (core::configManager.conf["moduleInstances"].contains(name)) {
                    core::configManager.conf["moduleInstances"][name]["enabled"] = instance.module.api->isEnabled(instance.instance);
                }
            }
            core::configManager.release(true);
        }
        firstMenuRender = false;
        startedWithMenuClosed = false;
        if (workspaceSection == 2) { drawDebugMenu(); }
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::SetCursorPos(ImVec2(origin.x + panelWidth, origin.y));
        ImGui::InvisibleButton("##workspace_divider", ImVec2(dividerWidth, contentHeight));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); }
        if (ImGui::IsItemActive()) {
            menuWidth = (int)(std::clamp)(panelWidth + ImGui::GetIO().MouseDelta.x, 280 * scale, (std::max)(280 * scale, available.x * 0.42f));
        }
        if (ImGui::IsItemDeactivated()) { persist("menuWidth", menuWidth); }
    }

    ImGui::SetCursorPos(ImVec2(origin.x + panelWidth + dividerWidth, origin.y));
    const float spectrumWidth = (std::max)(80 * scale, available.x - panelWidth - dividerWidth);
    ImGui::BeginChild("Spectrum Workspace", ImVec2(spectrumWidth, contentHeight), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawDesktopSpectrumToolbar(spectrumWidth);
    ImGui::Separator();
    const float outputMinimum = 100 * scale + 2 * ImGui::GetStyle().WindowPadding.y + ImGui::GetFrameHeightWithSpacing();
    const float bottomHeight = bottomWindows.empty() ? 0 : (std::min)((std::max)(outputMinimum, ImGui::GetContentRegionAvail().y * 0.3f), ImGui::GetContentRegionAvail().y * 0.5f);
    ImGui::BeginChild("Waterfall", ImVec2(0, (std::max)(40 * scale, ImGui::GetContentRegionAvail().y - bottomHeight)), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    gui::waterfall.draw();
    onWaterfallDrawn.emit(GImGui);
    ImGui::EndChild();
    if (!bottomWindows.empty()) {
        ImGui::BeginChild("Output Dock", ImVec2(0, bottomHeight), true);
        if (ImGui::BeginTabBar("##workspace_outputs")) {
            for (auto& output : bottomWindows) {
                const char* title = output.name == "audio_waterfall" ? "Audio spectrum" : output.name.c_str();
                if (ImGui::BeginTabItem(title)) {
                    ImGui::BeginChild("Output Contents", ImVec2(0, 0), false,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    output.drawFunc();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    handleWaterfallInput(vfo);

    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + contentHeight + 4 * scale));
    ImGui::TextDisabled("%s", receiver_manager_menu::activeReceiverSummary().c_str());
    if (available.x > 700 * scale) {
        ImGui::SameLine();
        const auto& source = sigpath::sourceManager.getState();
        ImGui::TextDisabled("  INPUT %s   |   SPAN %s", hrfreq::toString(source.sampleRate).c_str(),
                            hrfreq::toString(gui::waterfall.getViewBandwidth()).c_str());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { showCredits = false; }
}

void MainWindow::drawDesktopSpectrumToolbar(float width) {
    const float scale = style::uiScale;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("SPECTRUM / WATERFALL");
    ImGui::SameLine();
    ImGui::SetNextItemWidth((std::min)(120 * scale, width / 5));
    if (ImGui::SliderFloat("##workspace_zoom", &bw, 0.0f, 1.0f, "Zoom")) {
        persist("zoomBw", bw);
        updateWaterfallZoomBandwidth(bw);
    }
    tooltip("Spectrum zoom; scroll over the spectrum for fine control");
    ImGui::SameLine();
    if (ImGui::Button("Fit##workspace_fit")) {
        const auto range = gui::waterfall.autoRange();
        if (range.first != 0 || range.second != 0) {
            fftMin = range.first;
            fftMax = (std::max)((float)range.second, fftMin + 10.0f);
            gui::waterfall.setFFTMin(fftMin);
            gui::waterfall.setWaterfallMin(fftMin);
            gui::waterfall.setFFTMax(fftMax);
            gui::waterfall.setWaterfallMax(fftMax);
            persist("min", fftMin);
            persist("max", fftMax);
        }
    }
    tooltip("Fit display levels to the visible signals");
    const auto drawLevels = [&](bool inlineControls) {
        if (inlineControls) { ImGui::SameLine(); }
        ImGui::SetNextItemWidth(140 * scale);
        if (ImGui::SliderFloat("Floor##workspace_floor", &fftMin, -200.0f, fftMax - 10, "%.0f dB", ImGuiSliderFlags_AlwaysClamp)) {
            gui::waterfall.setFFTMin(fftMin);
            gui::waterfall.setWaterfallMin(fftMin);
            persist("min", fftMin);
        }
        if (inlineControls) { ImGui::SameLine(); }
        tooltip("Display floor; drag the slider or Ctrl-click to enter dB");
        ImGui::SetNextItemWidth(140 * scale);
        if (ImGui::SliderFloat("Ceiling##workspace_ceiling", &fftMax, fftMin + 10, 0.0f, "%.0f dB", ImGuiSliderFlags_AlwaysClamp)) {
            gui::waterfall.setFFTMax(fftMax);
            gui::waterfall.setWaterfallMax(fftMax);
            persist("max", fftMax);
        }
        tooltip("Display ceiling; stays at least 10 dB above the floor");
    };
    if (width > 900 * scale) {
        drawLevels(true);
    }
    else {
        ImGui::SameLine();
        if (ImGui::Button("Levels##workspace_levels")) { ImGui::OpenPopup("Display levels"); }
        if (ImGui::BeginPopup("Display levels")) {
            drawLevels(false);
            ImGui::EndPopup();
        }
    }
}
