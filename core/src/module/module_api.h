#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#define MOD_EXPORT extern "C" __declspec(dllexport)
#define SDRPP_MOD_EXTENTSION ".dll"
#else
#define MOD_EXPORT extern "C"
#ifdef __APPLE__
#define SDRPP_MOD_EXTENTSION ".dylib"
#else
#define SDRPP_MOD_EXTENTSION ".so"
#endif
#endif

struct ModuleInfo {
    const char* name;
    const char* description;
    const char* author;
    int versionMajor;
    int versionMinor;
    int versionBuild;
    int maxInstances;
};

// Same-build C++ SDK surface. Module-specific interfaces returned here may use
// the C++ ABI; this class is deliberately not part of the loader ABI.
class ModuleInstance {
public:
    virtual ~ModuleInstance() {}
    virtual void postInit() = 0;
    virtual void enable() = 0;
    virtual void disable() = 0;
    virtual bool isEnabled() = 0;
    virtual void* getInterface(const char* name) { return nullptr; }
    virtual std::string handleDebugCommand(const std::string& cmd, const std::string& args) { return "{}"; }
};

constexpr uint32_t SDRPP_MODULE_ABI_VERSION = 1;

struct SdrppModuleAPI {
    uint32_t abiVersion;
    size_t structSize;
    const ModuleInfo* info;
    void (*init)();
    void* (*createInstance)(const char* name, size_t nameLen);
    void (*destroyInstance)(void* instance);
    void (*postInit)(void* instance);
    void (*enable)(void* instance);
    void (*disable)(void* instance);
    bool (*isEnabled)(void* instance);
    void (*end)();
};

#define SDRPP_MODULE_INFO MOD_EXPORT const ModuleInfo sdrppModuleInfo

#define SDRPP_MODULE_EXPORT_API                                                        \
    MOD_EXPORT void sdrppModulePostInit(void* instance) {                             \
        static_cast<ModuleInstance*>(instance)->postInit();                           \
    }                                                                                  \
    MOD_EXPORT void sdrppModuleEnable(void* instance) {                               \
        static_cast<ModuleInstance*>(instance)->enable();                             \
    }                                                                                  \
    MOD_EXPORT void sdrppModuleDisable(void* instance) {                              \
        static_cast<ModuleInstance*>(instance)->disable();                            \
    }                                                                                  \
    MOD_EXPORT bool sdrppModuleIsEnabled(void* instance) {                            \
        return static_cast<ModuleInstance*>(instance)->isEnabled();                   \
    }                                                                                  \
    MOD_EXPORT const SdrppModuleAPI sdrppModuleApi = {                                \
        SDRPP_MODULE_ABI_VERSION, sizeof(SdrppModuleAPI), &sdrppModuleInfo,            \
        sdrppModuleInit, sdrppModuleCreateInstance, sdrppModuleDestroyInstance,        \
        sdrppModulePostInit, sdrppModuleEnable, sdrppModuleDisable,                    \
        sdrppModuleIsEnabled, sdrppModuleEnd                                           \
    }
