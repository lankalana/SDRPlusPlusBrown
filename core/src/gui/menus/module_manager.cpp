#include <gui/menus/module_manager.h>
#include <imgui.h>
#include <core.h>
#include <gui/style.h>
#include <gui/dialogs/dialog_box.h>
#include <algorithm>

namespace module_manager_menu {
    namespace {
        char modName[1024] = {};
        std::vector<std::string> modTypes;
        std::string toBeRemoved;
        std::string errorMessage;
        int modTypeId = 0;
        bool windowOpen = false;
        bool focusRequested = false;
        bool confirmOpened = false;
        bool errorOpen = false;
        ImGuiTextFilter instanceFilter;

        void persistInstances() {
            core::configManager.acquire();
            json instances = json::object();
            for (const auto& [name, inst] : core::moduleManager.instances) {
                instances[name]["module"] = inst.module.info->name;
                instances[name]["enabled"] = inst.module.api->isEnabled(inst.instance);
            }
            core::configManager.conf["moduleInstances"] = instances;
            core::configManager.release(true);
        }

        void drawContents() {
            const float scale = style::uiScale;
            bool modified = false;
            ImGui::TextDisabled("%zu instances  |  %zu available module types", core::moduleManager.instances.size(), modTypes.size());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##module_manager_search", "Find an instance or module type...",
                                       instanceFilter.InputBuf, sizeof(instanceFilter.InputBuf))) {
                instanceFilter.Build();
            }

            const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + 2 * ImGui::GetFrameHeightWithSpacing() + 16 * scale;
            const float tableHeight = (std::max)(80 * scale, ImGui::GetContentRegionAvail().y - footerHeight);
            if (ImGui::BeginTable("Module instances", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, tableHeight))) {
                ImGui::TableSetupColumn("Instance", ImGuiTableColumnFlags_WidthStretch, 1);
                ImGui::TableSetupColumn("Module type", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 70 * scale);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80 * scale);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                int visibleCount = 0;
                for (const auto& [name, inst] : core::moduleManager.instances) {
                    const std::string searchable = name + " " + inst.module.info->name;
                    if (!instanceFilter.PassFilter(searchable.c_str())) { continue; }
                    ++visibleCount;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(inst.module.info->name);
                    ImGui::TableSetColumnIndex(2);
                    bool enabled = inst.module.api->isEnabled(inst.instance);
                    if (ImGui::Checkbox(("##module_manager_enabled_" + name).c_str(), &enabled)) {
                        if (!enabled) {
                            // Save disabled state before a module's disable callback can save its own config.
                            core::configManager.acquire();
                            core::configManager.conf["moduleInstances"][name]["enabled"] = false;
                            core::configManager.release(true);
                            core::configManager.save(true);
                        }
                        enabled ? core::moduleManager.enableInstance(name) : core::moduleManager.disableInstance(name);
                        modified = true;
                    }
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::Button(("Remove##module_manager_remove_" + name).c_str())) {
                        toBeRemoved = name;
                        confirmOpened = true;
                    }
                }
                if (!visibleCount) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("No matching instances");
                }
                ImGui::EndTable();
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Add an instance");
            if (ImGui::BeginTable("New module instance", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110 * scale);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##module_mod_name", "Instance name", modName, sizeof(modName));
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1);
                const bool hasType = modTypeId >= 0 && modTypeId < (int)modTypes.size();
                if (ImGui::BeginCombo("##module_mgr_type", hasType ? modTypes[modTypeId].c_str() : "No modules available")) {
                    for (int i = 0; i < (int)modTypes.size(); ++i) {
                        if (ImGui::Selectable(modTypes[i].c_str(), modTypeId == i)) { modTypeId = i; }
                        if (modTypeId == i) { ImGui::SetItemDefaultFocus(); }
                    }
                    ImGui::EndCombo();
                }
                ImGui::TableSetColumnIndex(2);
                const bool duplicate = core::moduleManager.instances.contains(modName);
                ImGui::BeginDisabled(!hasType || !modName[0] || duplicate);
                if (ImGui::Button("Add instance##module_mgr_add_btn")) {
                    if (!core::moduleManager.createInstance(modName, modTypes[modTypeId])) {
                        core::moduleManager.postInit(modName);
                        modName[0] = 0;
                        modified = true;
                    }
                    else {
                        errorMessage = "Could not create an instance of " + modTypes[modTypeId] + ". Check the module's instance limit.";
                        errorOpen = true;
                    }
                }
                ImGui::EndDisabled();
                if (duplicate && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("An instance with this name already exists");
                }
                ImGui::EndTable();
            }

            if (ImGui::GenericDialog("module_mgr_confirm_", confirmOpened, GENERIC_DIALOG_BUTTONS_YES_NO, []() {
                    ImGui::Text("Remove instance \"%s\"?", toBeRemoved.c_str());
                }) == GENERIC_DIALOG_BUTTON_YES) {
                modified |= core::moduleManager.deleteInstance(toBeRemoved) == 0;
            }
            ImGui::GenericDialog("module_mgr_error_", errorOpen, GENERIC_DIALOG_BUTTONS_OK, []() {
                ImGui::TextWrapped("%s", errorMessage.c_str());
            });
            if (modified) { persistInstances(); }
        }
    }

    void init() {
        modName[0] = 0;
        modTypes.clear();
        for (const auto& [name, mod] : core::moduleManager.modules) { modTypes.push_back(name); }
        modTypeId = 0;
        instanceFilter.Clear();
    }

    void open() {
        windowOpen = true;
        focusRequested = true;
    }

    void drawWindow() {
        if (!windowOpen) { return; }
        const float scale = style::uiScale;
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const ImVec2 maximum((std::max)(1.0f, display.x - 24 * scale), (std::max)(1.0f, display.y - 24 * scale));
        ImGui::SetNextWindowSize(ImVec2((std::min)(820 * scale, maximum.x), (std::min)(540 * scale, maximum.y)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2((std::min)(520 * scale, maximum.x), (std::min)(320 * scale, maximum.y)), maximum);
        if (focusRequested) {
            ImGui::SetNextWindowFocus();
            focusRequested = false;
        }
        if (ImGui::Begin("Module manager", &windowOpen, ImGuiWindowFlags_NoCollapse)) {
            drawContents();
            if (ImGui::Button("Close##module_manager_close")) { windowOpen = false; }
            if (!confirmOpened && !errorOpen && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                ImGui::IsKeyPressed(ImGuiKey_Escape)) { windowOpen = false; }
        }
        ImGui::End();
        if (!windowOpen) {
            confirmOpened = false;
            errorOpen = false;
        }
    }
}
