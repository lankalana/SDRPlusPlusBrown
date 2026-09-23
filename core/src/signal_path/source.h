#pragma once
#include <string>
#include <vector>
#include <map>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <utils/event.h>

class SourceManager {
public:
    SourceManager();

    struct SourceHandler {
        dsp::stream<dsp::complex_t>* stream;
        void (*menuHandler)(void* ctx);
        void (*selectHandler)(void* ctx);
        void (*deselectHandler)(void* ctx);
        void (*startHandler)(void* ctx);
        void (*stopHandler)(void* ctx);
        void (*tuneHandler)(double freq, void* ctx);
        void* ctx;
    };

    enum TuningMode {
        NORMAL,
        PANADAPTER
    };

    /**
     * Calibration-critical properties of the source. The automatic reception subsystem compares
     * this against the state its noise floor was measured under; any difference invalidates the
     * calibration. Subscribing to onSourceStateChanged avoids polling GUI state.
     */
    struct State {
        std::string sourceName;
        double centerFrequency = 0.0;
        double sampleRate = 0.0;
        int decimation = 1;

        bool operator==(const State& o) const {
            return sourceName == o.sourceName && centerFrequency == o.centerFrequency &&
                   sampleRate == o.sampleRate && decimation == o.decimation;
        }
        bool operator!=(const State& o) const { return !(*this == o); }
    };

    void registerSource(std::string name, SourceHandler* handler);
    void unregisterSource(std::string name);
    void selectSource(std::string name);
    void showSelectedMenu();
    void start();
    void stop();
    void tune(double freq);
    void setTuningOffset(double offset);
    void setTuningMode(TuningMode mode);
    void setPanadapterIF(double freq);
    const std::string& getSelectedName() const { return selectedName; }

    std::vector<std::string> getSourceNames();

    // Report a calibration-critical parameter. Each emits onSourceStateChanged when the value
    // actually changes. Called from wherever the parameter is owned (tune, setInputSampleRate,
    // IQFrontEnd::setDecimation), so that consumers get one consistent view.
    void reportSampleRate(double sampleRate);
    void reportDecimation(int decimation);
    const State& getState() const { return state; }

    Event<std::string> onSourceSelected;
    Event<std::string> onSourceRegistered;
    Event<std::string> onSourceUnregister;
    Event<std::string> onSourceUnregistered;
    Event<double> onTuneChanged;
    Event<double> onRetune;
    Event<State> onSourceStateChanged;
    int secondsAdjustment;

#ifndef BUILD_TESTS
private:
#endif

    void emitStateIfChanged(const State& previous);

    State state;
    std::map<std::string, SourceHandler*> sources;
    std::string selectedName;
    SourceHandler* selectedHandler = NULL;
    double tuneOffset;
    double currentFreq;
    double ifFreq = 0.0;
    TuningMode tuneMode = TuningMode::NORMAL;
    dsp::stream<dsp::complex_t> nullSource;
};
