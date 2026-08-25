#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class FileSelect {
public:
    FileSelect(std::string defaultPath, std::vector<std::string> filter = { "All Files", "*" });
    bool render(std::string id);
    void setPath(std::string path, bool markChanged = false);
    bool pathIsValid();

    std::string expandString(std::string input);

    std::string path = "";
    std::atomic_bool dialogOpen = false;

private:
    void worker(std::stop_token stopToken, std::string startingPath);
    std::vector<std::string> _filter;
    std::string root = "";

    bool pathValid = false;
    std::array<char, 2048> strPath{};
    bool pathChanged = false;
    std::mutex resultMutex;
    std::optional<std::string> selectedPath;
    std::jthread workerThread;
};
