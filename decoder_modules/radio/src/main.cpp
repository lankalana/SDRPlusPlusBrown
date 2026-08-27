#include "radio_module.h"

ConfigManager config;

std::map<DeemphasisMode, double> deempTaus = {
    { DEEMP_MODE_22US, 22e-6 },
    { DEEMP_MODE_50US, 50e-6 },
    { DEEMP_MODE_75US, 75e-6 }
};

std::map<IFNRPreset, double> ifnrTaps = {
    { IFNR_PRESET_NOAA_APT, 9},
    { IFNR_PRESET_VOICE, 15 },
    { IFNR_PRESET_NARROW_BAND, 31 },
    { IFNR_PRESET_BROADCAST, 32 }
};


SDRPP_MODULE_INFO{
    /* Name:            */ "radio",
    /* Description:     */ "Analog radio decoder",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 2, 0, 0,
    /* Max instances    */ -1
};

const int RadioModule::SPECTRUM_BUF_SIZE;

MOD_EXPORT void sdrppModuleInit() {
    json def = json({});
    config.setPath(std::string(core::getRoot()) + "/radio_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* sdrppModuleCreateInstance(const char* instanceName, size_t instanceNameLen) {
    std::string name(instanceName, instanceNameLen);
    return new RadioModule(name);
}

MOD_EXPORT void sdrppModuleDestroyInstance(void* instance) {
    delete (RadioModule*)instance;
}

MOD_EXPORT void sdrppModuleEnd() {
    config.disableAutoSave();
    config.save();
}

SDRPP_MODULE_EXPORT_API;
