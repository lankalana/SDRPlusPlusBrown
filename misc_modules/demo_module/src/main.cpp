#include <imgui.h>
#include <module/module_api.h>
#include <gui/gui.h>

SDRPP_MODULE_INFO{
    /* Name:            */ "demo",
    /* Description:     */ "My fancy new module",
    /* Author:          */ "author1;author2,author3,etc...",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

class DemoModule : public ModuleInstance {
public:
    DemoModule(std::string name) {
        this->name = name;
        gui::menu.registerEntry(name, menuHandler, this, NULL);
    }

    ~DemoModule() {
        gui::menu.removeEntry(name);
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static void menuHandler(void* ctx) {
        DemoModule* _this = (DemoModule*)ctx;
        ImGui::Text("Hello SDR++, my name is %s", _this->name.c_str());
    }

    std::string name;
    bool enabled = true;
};

MOD_EXPORT void sdrppModuleInit() {
    // Nothing here
}

MOD_EXPORT void* sdrppModuleCreateInstance(const char* instanceName, size_t instanceNameLen) {
    std::string name(instanceName, instanceNameLen);
    return new DemoModule(name);
}

MOD_EXPORT void sdrppModuleDestroyInstance(void* instance) {
    delete (DemoModule*)instance;
}

MOD_EXPORT void sdrppModuleEnd() {
    // Nothing here
}

SDRPP_MODULE_EXPORT_API;
