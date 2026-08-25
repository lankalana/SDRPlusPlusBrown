#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class FolderSelect {
public:
    FolderSelect(std::string defaultPath);
    bool render(std::string id);
    void setPath(std::string path, bool markChanged = false);
    bool pathIsValid();

    std::string expandString(std::string input);

    std::string path = "";


private:
    void worker(std::stop_token stopToken, std::string startingPath);
    std::string root = "";

    bool pathValid = false;
    std::atomic_bool dialogOpen = false;
    std::array<char, 2048> strPath{};
    bool pathChanged = false;
    std::mutex resultMutex;
    std::optional<std::string> selectedPath;
    std::jthread workerThread;
};
