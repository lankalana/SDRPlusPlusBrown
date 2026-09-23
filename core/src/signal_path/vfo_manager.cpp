#include <signal_path/vfo_manager.h>
#include <signal_path/signal_path.h>
#include <gui/gui.h>

VFOManager::VFO::VFO(std::string name, int reference, double offset, double bandwidth, double sampleRate, double minBandwidth, double maxBandwidth, bool bandwidthLocked) {
    this->name = name;
    _bandwidth = bandwidth;
    dspVFO = sigpath::iqFrontEnd.addVFO(name, sampleRate, bandwidth, offset);
    wtfVFOStorage = std::make_unique<ImGui::WaterfallVFO>();
    wtfVFO = wtfVFOStorage.get();
    wtfVFO->setReference(reference);
    wtfVFO->setBandwidth(bandwidth);
    wtfVFO->setOffset(offset);
    wtfVFO->minBandwidth = minBandwidth;
    wtfVFO->maxBandwidth = maxBandwidth;
    wtfVFO->bandwidthLocked = bandwidthLocked;
    wtfVFO->label = name;
    wtfVFO->owner = receivers::ownerFromName(name);
    output = &dspVFO->out;
    gui::waterfall.vfos[name] = wtfVFO;
}

VFOManager::VFO::~VFO() {
    dspVFO->stop();
    gui::waterfall.vfos.erase(name);
    if (gui::waterfall.selectedVFO == name) {
        gui::waterfall.selectFirstVFO();
    }
    sigpath::iqFrontEnd.removeVFO(name);
}

void VFOManager::VFO::setOffset(double offset) {
    wtfVFO->setOffset(offset);
    dspVFO->setOffset(wtfVFO->centerOffset);
}

double VFOManager::VFO::getOffset() {
    return wtfVFO->generalOffset;
}

void VFOManager::VFO::setCenterOffset(double offset) {
    wtfVFO->setCenterOffset(offset);
    dspVFO->setOffset(offset);
}

void VFOManager::VFO::setBandwidth(double bandwidth, bool updateWaterfall) {
    if (_bandwidth == bandwidth) { return; }
    _bandwidth = bandwidth;
    if (updateWaterfall) { wtfVFO->setBandwidth(bandwidth); }
    dspVFO->setBandwidth(bandwidth);
}

void VFOManager::VFO::setSampleRate(double sampleRate, double bandwidth) {
    dspVFO->setOutSamplerate(sampleRate, bandwidth);
    wtfVFO->setBandwidth(bandwidth);
}

void VFOManager::VFO::setReference(int ref) {
    wtfVFO->setReference(ref);
}

void VFOManager::VFO::setSnapInterval(double interval) {
    wtfVFO->setSnapInterval(interval);
}

void VFOManager::VFO::setBandwidthLimits(double minBandwidth, double maxBandwidth, bool bandwidthLocked) {
    wtfVFO->minBandwidth = minBandwidth;
    wtfVFO->maxBandwidth = maxBandwidth;
    wtfVFO->bandwidthLocked = bandwidthLocked;
}

bool VFOManager::VFO::getBandwidthChanged(bool erase) {
    bool val = wtfVFO->bandwidthChanged;
    if (erase) { wtfVFO->bandwidthChanged = false; }
    return val;
}

double VFOManager::VFO::getBandwidth() {
    return wtfVFO->bandwidth;
}

int VFOManager::VFO::getReference() {
    return wtfVFO->reference;
}

void VFOManager::VFO::setColor(ImU32 color) {
    wtfVFO->color = color;
}

std::string VFOManager::VFO::getName() {
    return name;
}

ReceiverOwner VFOManager::VFO::getOwner() {
    return wtfVFO->owner;
}

void VFOManager::VFO::setLabel(const std::string& label) {
    wtfVFO->label = label;
}

void VFOManager::VFO::setStatusText(const std::string& status) {
    wtfVFO->statusText = status;
}

VFOManager::VFOManager() {
}

VFOManager::VFO* VFOManager::createVFO(std::string name, int reference, double offset, double bandwidth, double sampleRate, double minBandwidth, double maxBandwidth, bool bandwidthLocked) {
    if (vfos.contains(name) || name == "") {
        return NULL;
    }
    auto vfo = std::make_unique<VFO>(name, reference, offset, bandwidth, sampleRate, minBandwidth, maxBandwidth, bandwidthLocked);
    auto* vfoPtr = vfo.get();
    vfos.emplace(name, std::move(vfo));
    onVfoCreated.emit(vfoPtr);
    return vfoPtr;
}

void VFOManager::deleteVFO(VFOManager::VFO* vfo) {
    std::string name = "";
    for (auto const& [_name, _vfo] : vfos) {
        if (_vfo.get() == vfo) {
            name = _name;
            break;
        }
    }
    if (name == "") {
        return;
    }
    onVfoDelete.emit(vfo);
    vfos.erase(name);
    onVfoDeleted.emit(name);
}

void VFOManager::setOffset(std::string name, double offset) {
    if (!vfos.contains(name)) {
        return;
    }
    vfos[name]->setOffset(offset);
}

double VFOManager::getOffset(std::string name) {
    auto it = vfos.find(name);
    if (it == vfos.end()) {
        return 0;
    }
    return it->second->getOffset();
}

void VFOManager::setCenterOffset(std::string name, double offset) {
    if (!vfos.contains(name)) {
        return;
    }
    vfos[name]->setCenterOffset(offset);
}

void VFOManager::setBandwidth(std::string name, double bandwidth, bool updateWaterfall) {
    if (!vfos.contains(name)) {
        return;
    }
    vfos[name]->setBandwidth(bandwidth, updateWaterfall);
}

void VFOManager::setSampleRate(std::string name, double sampleRate, double bandwidth) {
    if (!vfos.contains(name)) {
        return;
    }
    vfos[name]->setSampleRate(sampleRate, bandwidth);
}

void VFOManager::setReference(std::string name, int ref) {
    if (!vfos.contains(name)) {
        return;
    }
    vfos[name]->setReference(ref);
}

void VFOManager::setBandwidthLimits(std::string name, double minBandwidth, double maxBandwidth, bool bandwidthLocked) {
    if (!vfos.contains(name)) {
        return;
    }
    vfos[name]->setBandwidthLimits(minBandwidth, maxBandwidth, bandwidthLocked);
}

bool VFOManager::getBandwidthChanged(std::string name, bool erase) {
    if (!vfos.contains(name)) {
        return false;
    }
    return vfos[name]->getBandwidthChanged(erase);
}

double VFOManager::getBandwidth(std::string name) {
    if (!vfos.contains(name)) {
        return NAN;
    }
    return vfos[name]->getBandwidth();
}

int VFOManager::getReference(std::string name) {
    if (!vfos.contains(name)) {
        return -1;
    }
    return vfos[name]->getReference();
}

void VFOManager::setColor(std::string name, ImU32 color) {
    if (!vfos.contains(name)) {
        return;
    }
    return vfos[name]->setColor(color);
}

bool VFOManager::vfoExists(std::string name) {
    return vfos.contains(name);
}

void VFOManager::setStatusText(std::string name, const std::string& status) {
    auto it = vfos.find(name);
    if (it == vfos.end()) {
        return;
    }
    it->second->setStatusText(status);
}

VFOManager::VFO* VFOManager::getVFO(const std::string& name) {
    auto it = vfos.find(name);
    if (it == vfos.end()) {
        return NULL;
    }
    return it->second.get();
}

std::vector<VFOManager::VFOState> VFOManager::getVFOStates() const {
    std::vector<VFOState> states;
    states.reserve(vfos.size());
    for (auto const& [name, vfo] : vfos) {
        VFOState state;
        state.name = name;
        state.offset = vfo->wtfVFO->generalOffset;
        state.bandwidth = vfo->wtfVFO->bandwidth;
        state.reference = vfo->wtfVFO->reference;
        state.owner = vfo->wtfVFO->owner;
        states.push_back(state);
    }
    return states;
}

std::vector<std::string> VFOManager::getVFONames() const {
    std::vector<std::string> names;
    names.reserve(vfos.size());
    for (auto const& [name, vfo] : vfos) {
        names.push_back(name);
    }
    return names;
}

bool VFOManager::setAbsoluteFrequency(const std::string& name, double centerFrequency, double absoluteFrequency) {
    auto it = vfos.find(name);
    if (it == vfos.end()) {
        return false;
    }
    // Only the offset moves. The source center frequency belongs to the user.
    it->second->setOffset(absoluteFrequency - centerFrequency);
    return true;
}

double VFOManager::getAbsoluteFrequency(const std::string& name, double centerFrequency) {
    auto it = vfos.find(name);
    if (it == vfos.end()) {
        return NAN;
    }
    return centerFrequency + it->second->getOffset();
}

bool VFOManager::passbandFitsInSpectrum(double centerFrequency, double usableBandwidth,
                                        double tuneFrequency, double bandwidth, int reference) {
    double specLow = centerFrequency - (usableBandwidth / 2.0);
    double specHigh = centerFrequency + (usableBandwidth / 2.0);

    double passLow, passHigh;
    if (reference == ImGui::WaterfallVFO::REF_LOWER) {
        passLow = tuneFrequency;
        passHigh = tuneFrequency + bandwidth;
    }
    else if (reference == ImGui::WaterfallVFO::REF_UPPER) {
        passLow = tuneFrequency - bandwidth;
        passHigh = tuneFrequency;
    }
    else {
        passLow = tuneFrequency - (bandwidth / 2.0);
        passHigh = tuneFrequency + (bandwidth / 2.0);
    }

    return passLow >= specLow && passHigh <= specHigh;
}

void VFOManager::updateFromWaterfall(ImGui::WaterFall* wtf) {
    for (auto const& [name, vfo] : vfos) {
        if (vfo->wtfVFO->centerOffsetChanged) {
            vfo->wtfVFO->centerOffsetChanged = false;
            vfo->dspVFO->setOffset(vfo->wtfVFO->centerOffset);
        }
    }
}
