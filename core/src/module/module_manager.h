#pragma once

#include <map>
#include <string>
#include <vector>
#include <utils/event.h>
#include "module_api.h"
#include "sdrpp_export.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

class ModuleManager {
public:
    struct Module_t {
#ifdef _WIN32
        HMODULE handle;
#else
        void* handle;
#endif
        const SdrppModuleAPI* api;
        const ModuleInfo* info;

        friend bool operator==(const Module_t& a, const Module_t& b) {
            return a.handle == b.handle && a.api == b.api;
        }
    };

    struct Instance_t {
        Module_t module;
        ModuleInstance* instance;
    };

    Module_t loadModule(std::string path);
    int createInstance(std::string name, std::string module);
    int deleteInstance(std::string name);
    int deleteInstance(ModuleInstance* instance);
    int enableInstance(std::string name);
    int disableInstance(std::string name);
    bool instanceEnabled(std::string name);
    void postInit(std::string name);
    std::string getInstanceModuleName(std::string name);
    int countModuleInstances(std::string module);

    template <typename T>
    std::vector<T*> getAllInterfaces(const std::string& interfaceName) {
        std::vector<T*> retval;
        for (auto x : instances) {
            if (x.second.instance == nullptr) { continue; }
            auto rv = x.second.instance->getInterface(interfaceName.c_str());
            if (rv != nullptr) { retval.emplace_back((T*)rv); }
        }
        return retval;
    }

    void* getInterface(const std::string& name, const std::string& interfaceName) {
        if (name != "") {
            auto it = instances.find(name);
            if (it == instances.end() || it->second.instance == nullptr) { return nullptr; }
            return it->second.instance->getInterface(interfaceName.c_str());
        }
        for (auto x : instances) {
            if (x.second.instance == nullptr) { continue; }
            auto rv = x.second.instance->getInterface(interfaceName.c_str());
            if (rv != nullptr) { return rv; }
        }
        return nullptr;
    }

    void doPostInitAll();

    Event<std::string> onInstanceCreated;
    Event<std::string> onInstanceDelete;
    Event<std::string> onInstanceDeleted;
    std::map<std::string, Module_t> modules;
    std::map<std::string, Instance_t> instances;

#ifdef BUILD_TESTS
    std::vector<std::string> pluginWhitelist;
    bool useWhitelist = false;
#endif
};
