#pragma once
#include "../dsp/channel/rx_vfo.h"
#include <gui/widgets/waterfall.h>
#include <signal_path/receiver.h>
#include <utils/event.h>
#include <memory>
#include <vector>

class VFOManager {
public:
    VFOManager();

    // Read-only snapshot of a VFO, so that callers don't have to reach into gui::waterfall.vfos.
    struct VFOState {
        std::string name;
        double offset;
        double bandwidth;
        int reference;
        ReceiverOwner owner;
    };

    class VFO {
    public:
        VFO(std::string name, int reference, double offset, double bandwidth, double sampleRate, double minBandwidth, double maxBandwidth, bool bandwidthLocked);
        ~VFO();

        void setOffset(double offset);
        double getOffset();
        void setCenterOffset(double offset);
        void setBandwidth(double bandwidth, bool updateWaterfall = true);
        void setSampleRate(double sampleRate, double bandwidth);
        void setReference(int ref);
        void setSnapInterval(double interval);
        void setBandwidthLimits(double minBandwidth, double maxBandwidth, bool bandwidthLocked);
        bool getBandwidthChanged(bool erase = true);
        double getBandwidth();
        int getReference();
        void setColor(ImU32 color);
        std::string getName();
        ReceiverOwner getOwner();
        void setLabel(const std::string& label);
        void setStatusText(const std::string& status);

        dsp::stream<dsp::complex_t>* output;

        friend class VFOManager;

        dsp::channel::RxVFO* dspVFO;
        ImGui::WaterfallVFO* wtfVFO;

    private:
        std::unique_ptr<ImGui::WaterfallVFO> wtfVFOStorage;
        std::string name;
        double _bandwidth;

    };

    VFOManager::VFO* createVFO(std::string name, int reference, double offset, double bandwidth, double sampleRate, double minBandwidth, double maxBandwidth, bool bandwidthLocked);
    void deleteVFO(VFOManager::VFO* vfo);

    void setOffset(std::string name, double offset);
    double getOffset(std::string name);
    void setCenterOffset(std::string name, double offset);
    void setBandwidth(std::string name, double bandwidth, bool updateWaterfall = true);
    void setSampleRate(std::string name, double sampleRate, double bandwidth);
    void setReference(std::string name, int ref);
    void setBandwidthLimits(std::string name, double minBandwidth, double maxBandwidth, bool bandwidthLocked);
    bool getBandwidthChanged(std::string name, bool erase = true);
    double getBandwidth(std::string name);
    void setColor(std::string name, ImU32 color);
    std::string getName();
    int getReference(std::string name);
    bool vfoExists(std::string name);
    void setStatusText(std::string name, const std::string& status);

    // Read APIs. Prefer these over touching gui::waterfall.vfos directly.
    VFO* getVFO(const std::string& name);
    std::vector<VFOState> getVFOStates() const;
    std::vector<std::string> getVFONames() const;

    /**
     * Move a VFO to an absolute frequency by changing only its offset. This never touches the
     * source: the SDR center frequency is owned by the user.
     * Returns false if the VFO doesn't exist.
     */
    bool setAbsoluteFrequency(const std::string& name, double centerFrequency, double absoluteFrequency);
    double getAbsoluteFrequency(const std::string& name, double centerFrequency);

    /**
     * Fixed-center placement check: does a receiver tuned to `tuneFrequency` with the given
     * bandwidth and reference fit entirely inside the usable spectrum
     * [centerFrequency - usableBandwidth/2, centerFrequency + usableBandwidth/2]?
     *
     * REF_CENTER covers [tune - bw/2, tune + bw/2], REF_LOWER (USB) covers [tune, tune + bw],
     * REF_UPPER (LSB) covers [tune - bw, tune].
     */
    static bool passbandFitsInSpectrum(double centerFrequency, double usableBandwidth,
                                       double tuneFrequency, double bandwidth, int reference);

    void updateFromWaterfall(ImGui::WaterFall* wtf);

    Event<VFOManager::VFO*> onVfoCreated;
    Event<VFOManager::VFO*> onVfoDelete;
    Event<std::string> onVfoDeleted;

private:
    std::map<std::string, std::unique_ptr<VFO>> vfos;
};
