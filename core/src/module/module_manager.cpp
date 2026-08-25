#include "module_manager.h"

#include <cstddef>
#include <filesystem>
#include <utils/flog.h>
#include <utils/wstr.h>

namespace {
void closeModuleHandle(ModuleManager::Module_t& mod) {
    if (mod.handle == nullptr) { return; }
#ifdef _WIN32
    FreeLibrary(mod.handle);
#else
    dlclose(mod.handle);
#endif
    mod = {};
}
}

ModuleManager::Module_t ModuleManager::loadModule(std::string path) {
    Module_t mod{};

#ifdef BUILD_TESTS
    if (useWhitelist) {
        std::string filename;
        size_t lastSlash = path.find_last_of("/\\");
        filename = lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);
        bool allowed = false;
        for (const auto& plugin : pluginWhitelist) {
            if (filename == plugin || filename == plugin + SDRPP_MOD_EXTENTSION) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            flog::info("Skipping module {0} (not in whitelist)", path);
            return mod;
        }
    }
#endif

#ifndef __ANDROID__
    if (!std::filesystem::exists(path)) {
        flog::error("{0} does not exist", path);
        return mod;
    }
    if (!std::filesystem::is_regular_file(path)) {
        flog::error("{0} isn't a loadable module", path);
        return mod;
    }
#endif

#ifdef _WIN32
    auto wide = wstr::str2wstr(path);
    mod.handle = LoadLibraryExW(wide.c_str(), NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (mod.handle == NULL) {
        auto err = GetLastError();
        LPWSTR errorMessageBuffer = NULL;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&errorMessageBuffer, 0, NULL);
        auto narrow = errorMessageBuffer == NULL ? std::string() : wstr::wstr2str(errorMessageBuffer);
        flog::error("Couldn't LoadLibraryExW {0}. Error: {1} - {2}", path.c_str(), (int64_t)err, narrow.c_str());
        if (errorMessageBuffer != NULL) { LocalFree(errorMessageBuffer); }
        return mod;
    }
    mod.api = reinterpret_cast<const SdrppModuleAPI*>(GetProcAddress(mod.handle, "sdrppModuleApi"));
#else
    try {
        mod.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    } catch (std::exception& e) {
        flog::error("Couldn't load {}: {}", path, e.what());
    } catch (...) {
        flog::error("Couldn't load {0}.", path);
    }
    if (mod.handle == NULL) {
        const char* err = dlerror();
        flog::error("Couldn't load {0}: {1}.", path, err == nullptr ? "unknown error" : err);
        return mod;
    }
    dlerror();
    mod.api = reinterpret_cast<const SdrppModuleAPI*>(dlsym(mod.handle, "sdrppModuleApi"));
    const char* symErr = dlerror();
    if (symErr != NULL) {
        flog::error("Symbol resolution failed for {0}: {1}.", path, symErr);
        closeModuleHandle(mod);
        return mod;
    }
#endif

    if (mod.api == nullptr) {
        flog::error("{0} is missing sdrppModuleApi symbol", path);
        closeModuleHandle(mod);
        return mod;
    }
    if (mod.api->abiVersion != SDRPP_MODULE_ABI_VERSION || mod.api->structSize < sizeof(SdrppModuleAPI)) {
        flog::error("{0} uses unsupported module ABI version {1}", path, mod.api->abiVersion);
        closeModuleHandle(mod);
        return mod;
    }
    if (mod.api->info == nullptr || mod.api->init == nullptr || mod.api->createInstance == nullptr ||
        mod.api->destroyInstance == nullptr || mod.api->postInit == nullptr || mod.api->enable == nullptr ||
        mod.api->disable == nullptr || mod.api->isEnabled == nullptr || mod.api->end == nullptr) {
        flog::error("{0} has an incomplete module API", path);
        closeModuleHandle(mod);
        return mod;
    }
    mod.info = mod.api->info;
    if (mod.info->name == nullptr || mod.info->name[0] == '\0') {
        flog::error("{0} has an invalid module name", path);
        closeModuleHandle(mod);
        return mod;
    }
    if (modules.find(mod.info->name) != modules.end()) {
        flog::error("{0} has the same name as an already loaded module", path);
        closeModuleHandle(mod);
        return mod;
    }
    for (auto const& [name, loaded] : modules) {
        if (mod.handle == loaded.handle) {
            Module_t existing = loaded;
            closeModuleHandle(mod);
            return existing;
        }
    }
    try {
        mod.api->init();
        modules[mod.info->name] = mod;
        flog::info(" ..... ok {}", path);
        return mod;
    } catch (std::exception& e) {
        flog::error("Failed to initialize module {0}: {}", path, e.what());
    } catch (...) {
        flog::error("Failed to initialize module {0}", path);
    }
    closeModuleHandle(mod);
    return mod;
}

int ModuleManager::createInstance(std::string name, std::string module) {
    auto moduleIt = modules.find(module);
    if (moduleIt == modules.end()) {
        flog::error("Module '{0}' doesn't exist", module);
        return -1;
    }
    if (instances.find(name) != instances.end()) {
        flog::error("A module instance with the name '{0}' already exists", name);
        return -1;
    }
    int maxCount = moduleIt->second.info->maxInstances;
    if (countModuleInstances(module) >= maxCount && maxCount > 0) {
        flog::error("Maximum number of instances reached for '{0}'", module);
        return -1;
    }
    Instance_t inst{};
    inst.module = moduleIt->second;
    void* rawInstance = inst.module.api->createInstance(name.data(), name.size());
    if (rawInstance == nullptr) {
        flog::error("Module '{0}' failed to create instance '{1}'", module, name);
        return -1;
    }
    inst.instance = static_cast<ModuleInstance*>(rawInstance);
    instances[name] = inst;
    onInstanceCreated.emit(name);
    return 0;
}

int ModuleManager::deleteInstance(std::string name) {
    auto it = instances.find(name);
    if (it == instances.end()) {
        flog::error("Tried to remove non-existent instance '{0}'", name);
        return -1;
    }
    onInstanceDelete.emit(name);
    Instance_t inst = it->second;
    inst.module.api->destroyInstance(inst.instance);
    instances.erase(it);
    onInstanceDeleted.emit(name);
    return 0;
}

int ModuleManager::deleteInstance(ModuleInstance* instance) {
    for (auto const& [name, candidate] : instances) {
        if (candidate.instance == instance) { return deleteInstance(name); }
    }
    flog::error("Tried to remove an unknown module instance");
    return -1;
}

int ModuleManager::enableInstance(std::string name) {
    auto it = instances.find(name);
    if (it == instances.end()) {
        flog::error("Cannot enable '{0}', instance doesn't exist", name);
        return -1;
    }
    it->second.module.api->enable(it->second.instance);
    return 0;
}

int ModuleManager::disableInstance(std::string name) {
    auto it = instances.find(name);
    if (it == instances.end()) {
        flog::error("Cannot disable '{0}', instance doesn't exist", name);
        return -1;
    }
    it->second.module.api->disable(it->second.instance);
    return 0;
}

bool ModuleManager::instanceEnabled(std::string name) {
    auto it = instances.find(name);
    if (it == instances.end()) {
        flog::error("Cannot check if '{0}' is enabled, instance doesn't exist", name);
        return false;
    }
    return it->second.module.api->isEnabled(it->second.instance);
}

void ModuleManager::postInit(std::string name) {
    auto it = instances.find(name);
    if (it == instances.end()) {
        flog::error("Cannot post-init '{0}', instance doesn't exist", name);
        return;
    }
    it->second.module.api->postInit(it->second.instance);
}

std::string ModuleManager::getInstanceModuleName(std::string name) {
    auto it = instances.find(name);
    if (it == instances.end()) {
        flog::error("Cannot get module name of'{0}', instance doesn't exist", name);
        return "";
    }
    return std::string(it->second.module.info->name);
}

int ModuleManager::countModuleInstances(std::string module) {
    auto moduleIt = modules.find(module);
    if (moduleIt == modules.end()) {
        flog::error("Cannot count instances of '{0}', Module doesn't exist", module);
        return -1;
    }
    int count = 0;
    for (auto const& [name, instance] : instances) {
        if (instance.module == moduleIt->second) { count++; }
    }
    return count;
}

void ModuleManager::doPostInitAll() {
    for (auto& [name, inst] : instances) {
        flog::info("Running post-init for {0}", name);
        inst.module.api->postInit(inst.instance);
    }
}
