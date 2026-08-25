#include <gui/widgets/folder_select.h>
#include <cstdio>
#include <regex>
#include <filesystem>
#include <gui/file_dialogs.h>
#include <core.h>
#include "utils/wstr.h"

FolderSelect::FolderSelect(std::string defaultPath) {
    root = (std::string)core::args["root"];
    setPath(defaultPath);
}

bool FolderSelect::render(std::string id) {
    std::optional<std::string> selection;
    {
        std::lock_guard lock(resultMutex);
        selection.swap(selectedPath);
    }
    if (selection) { setPath(std::move(*selection), true); }

    bool _pathChanged = false;
    float menuColumnWidth = ImGui::GetContentRegionAvail().x;

    float buttonWidth = ImGui::CalcTextSize("...").x + 20.0f;
    bool lastPathValid = pathValid;
    if (!lastPathValid) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    }
    ImGui::SetNextItemWidth(menuColumnWidth - buttonWidth);
    if (ImGui::InputText(id.c_str(), strPath.data(), strPath.size())) {
        path = strPath.data();
        std::string expandedPath = expandString(strPath.data());
        if (!std::filesystem::is_directory(wstr::str2wstr(expandedPath))) {
            pathValid = false;
        }
        else {
            pathValid = true;
            _pathChanged = true;
        }
    }
    if (!lastPathValid) {
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button(("..." + id + "_winselect").c_str(), ImVec2(buttonWidth - 8.0f, 0)) && !dialogOpen.exchange(true)) {
        auto startingPath = pathValid ? std::filesystem::path(expandString(path)).parent_path().string() : "";
        workerThread = std::jthread([this, startingPath = std::move(startingPath)](std::stop_token stopToken) mutable {
            worker(stopToken, std::move(startingPath));
        });
    }

    _pathChanged |= pathChanged;
    pathChanged = false;
    return _pathChanged;
}

void FolderSelect::setPath(std::string path, bool markChanged) {
    this->path = path;
    std::string expandedPath = expandString(path);
    pathValid = std::filesystem::is_directory(wstr::str2wstr(expandedPath));
    if (markChanged) { pathChanged = true; }
    std::snprintf(strPath.data(), strPath.size(), "%s", path.c_str());
}

std::string FolderSelect::expandString(std::string input) {
    input = std::regex_replace(input, std::regex("%ROOT%"), root);
    return std::regex_replace(input, std::regex("//"), "/");
}

bool FolderSelect::pathIsValid() {
    return pathValid;
}

void FolderSelect::worker(std::stop_token stopToken, std::string startingPath) {
    auto fold = pfd::select_folder("Select Folder", startingPath);
    std::string res = fold.result();

    if (!stopToken.stop_requested() && !res.empty()) {
        std::lock_guard lock(resultMutex);
        selectedPath = std::move(res);
    }

    dialogOpen = false;
}
