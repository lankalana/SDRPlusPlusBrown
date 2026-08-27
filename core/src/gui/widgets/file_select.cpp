#include <gui/widgets/file_select.h>
#include <cstdio>
#include <regex>
#include <filesystem>
#include <gui/file_dialogs.h>
#include <core.h>
#include <utils/flog.h>
#include <thread>
#include <chrono>

FileSelect::FileSelect(std::string defaultPath, std::vector<std::string> filter) {
    _filter = filter;
    root = (std::string)core::args["root"];
    setPath(defaultPath);
}

bool FileSelect::render(std::string id) {
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
        if (!std::filesystem::is_regular_file(expandedPath)) {
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
#if __APPLE__
        // On macOS, run dialog synchronously on main thread to avoid WindowServer/threading issues
        worker({}, std::move(startingPath));
#else
        // On other platforms, use background thread
        workerThread = std::jthread([this, startingPath = std::move(startingPath)](std::stop_token stopToken) mutable {
            worker(stopToken, std::move(startingPath));
        });
#endif
    }

    _pathChanged |= pathChanged;
    pathChanged = false;
    return _pathChanged;
}

void FileSelect::setPath(std::string path, bool markChanged) {
    this->path = path;
    std::string expandedPath = expandString(path);
    try {
        pathValid = std::filesystem::is_regular_file(expandedPath);
    } catch (const std::exception& e) {
        pathValid = false;
    }
    if (markChanged) { pathChanged = true; }
    std::snprintf(strPath.data(), strPath.size(), "%s", path.c_str());
}

std::string FileSelect::expandString(std::string input) {
    input = std::regex_replace(input, std::regex("%ROOT%"), root);
    return std::regex_replace(input, std::regex("//"), "/");
}

bool FileSelect::pathIsValid() {
    return pathValid;
}

void FileSelect::worker(std::stop_token stopToken, std::string startingPath) {
    auto file = pfd::open_file("Open File", startingPath, _filter);

    // Wait for dialog to complete
    while (!stopToken.stop_requested() && !file.ready()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (stopToken.stop_requested()) {
        dialogOpen = false;
        return;
    }

    std::vector<std::string> res = file.result();

    if (!res.empty()) {
        std::lock_guard lock(resultMutex);
        selectedPath = std::move(res.front());
        flog::info("FileSelect: Selected file: {0}", *selectedPath);
    }

    dialogOpen = false;
}
