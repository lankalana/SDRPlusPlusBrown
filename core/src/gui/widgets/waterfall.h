#pragma once
#include <core.h>
#include <vector>
#include <mutex>
#include <gui/widgets/bandplan.h>
#include <signal_path/receiver.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <utils/event.h>
#include <ctm.h>

#include <utils/opengl_include_code.h>

#define WATERFALL_RESOLUTION 1000

namespace ImGui {



    class WaterfallVFO {
    public:
        void setOffset(double offset);
        void setCenterOffset(double offset);
        void setBandwidth(double bw);
        void setReference(int ref);
        void setSnapInterval(double interval);
        void setNotchOffset(double offset);
        void setNotchVisible(bool visible);
        void updateDrawingVars(double viewBandwidth, float dataWidth, double viewOffset, ImVec2 widgetPos, int fftHeight); // NOTE: Datawidth double???
        void draw(ImGuiWindow* window, bool selected);

        enum {
            REF_LOWER,
            REF_CENTER,
            REF_UPPER,
            _REF_COUNT
        };

        double generalOffset;
        double centerOffset;
        double lowerOffset;
        double upperOffset;
        double bandwidth;
        double snapInterval = 5000;
        int reference = REF_CENTER;

        double notchOffset = 0;
        bool notchVisible = false;

        // Presentation metadata. The label identifies overlapping receivers where color alone
        // isn't enough; statusText is an optional short suffix such as "REC".
        std::string label;
        std::string statusText;
        bool showLabel = true;
        ReceiverOwner owner = ReceiverOwner::MANUAL;
        std::string labelText; // label + statusText, rebuilt in updateDrawingVars

        bool leftClamped;
        bool rightClamped;

        ImVec2 rectMin;
        ImVec2 rectMax;
        ImVec2 lineMin;
        ImVec2 lineMax;
        ImVec2 wfRectMin;
        ImVec2 wfRectMax;
        ImVec2 wfLineMin;
        ImVec2 wfLineMax;
        ImVec2 lbwSelMin;
        ImVec2 lbwSelMax;
        ImVec2 rbwSelMin;
        ImVec2 rbwSelMax;
        ImVec2 wfLbwSelMin;
        ImVec2 wfLbwSelMax;
        ImVec2 wfRbwSelMin;
        ImVec2 wfRbwSelMax;
        ImVec2 notchMin;
        ImVec2 notchMax;
        ImVec2 labelMin;
        ImVec2 labelMax;

        bool centerOffsetChanged = false;
        bool lowerOffsetChanged = false;
        bool upperOffsetChanged = false;
        bool redrawRequired = true;
        bool lineVisible = true;
        bool bandwidthChanged = false;

        double minBandwidth;
        double maxBandwidth;
        bool bandwidthLocked;

        ImU32 color = IM_COL32(255, 255, 255, 50);

        Event<double> onUserChangedBandwidth;
        Event<double> onUserChangedNotch;
        Event<int> onUserChangedDemodulator;
    };

    /**
     * A detected signal drawn over the FFT. Frequencies are absolute, so markers stay correct
     * while the user pans and zooms.
     */
    struct DetectionMarker {
        enum State {
            CANDIDATE, // not confirmed yet: thin outline only
            ACTIVE,    // confirmed
            IGNORED,   // matched an ignore rule
            RECORDING
        };

        double lowerFrequency = 0.0;
        double upperFrequency = 0.0;
        State state = CANDIDATE;
        std::string label;
    };

    class WaterFall {
    public:
        WaterFall();
        virtual ~WaterFall();

        void init();

        void draw();
        float* getFFTBuffer();
        void pushFFT();

        void updatePallette(float colors[][3], int colorCount);
        void updatePalletteFromArray(const float* colors, int colorCount);

        void setCenterFrequency(double freq);
        double getCenterFrequency();

        void setBandwidth(double bandWidth);
        double getBandwidth();

        void setUsableSpectrumRatio(double spectrumratio);
        double getUsableSpectrumRatio();

        void setViewBandwidth(double bandWidth);
        double getViewBandwidth();

        void setViewOffset(double offset);
        double getViewOffset();

        void setFFTMin(float min);
        float getFFTMin();

        void setFFTMax(float max);
        float getFFTMax();

        void setWaterfallMin(float min);
        float getWaterfallMin();

        void setWaterfallMax(float max);
        float getWaterfallMax();

        void setZoom(double zoomLevel);
        void setOffset(double zoomOffset);

        std::pair<int, int> autoRange();

        // Single entry point for changing the active receiver. Every selection path (cursor click,
        // label click, receiver dropdown, Page Up/Down, creation, deletion) must go through this.
        void selectVFO(const std::string& name);
        void selectFirstVFO();

        void showWaterfall();
        void hideWaterfall();

        void showBandplan();
        void hideBandplan();

        void setFFTHeight(int height);
        int getFFTHeight();

        void setRawFFTSize(int size);

        void setFullWaterfallUpdate(bool fullUpdate);

        void setBandPlanPos(int pos);

        void setFFTHold(bool hold);
        void setFFTHoldSpeed(float speed);

        void setFFTSmoothing(bool enabled);
        void setFFTSmoothingSpeed(float speed);

        void setSNRSmoothing(bool enabled);
        void setSNRSmoothingSpeed(float speed);

        float* acquireLatestFFT(int& width);
        void releaseLatestFFT();

        // Replace the detection overlay. Safe to call from the DSP thread.
        void setDetectionMarkers(const std::vector<DetectionMarker>& markers);
        void clearDetectionMarkers();
        bool showDetections = true;

        /**
         * The detection floor and the threshold derived from it, drawn across the FFT so the user
         * can place a manual floor against what they can actually see.
         *
         * `floorDb`/`thresholdDb` are uniform samples across [lowFrequency, highFrequency]. A
         * single sample means a flat level that spans the whole width.
         */
        struct NoiseFloorOverlay {
            bool visible = false;
            double lowFrequency = 0.0;
            double highFrequency = 0.0;
            std::vector<float> floorDb;
            std::vector<float> thresholdDb;
        };

        void setNoiseFloorOverlay(const NoiseFloorOverlay& overlay);
        void clearNoiseFloorOverlay();
        bool showNoiseFloor = true;

        bool centerFreqMoved = false;
        bool vfoFreqChanged = false;
        bool bandplanEnabled = false;
        bandplan::BandPlan_t* bandplan = NULL;

        bool mouseInFFTResize = false;
        bool mouseInFreq = false;
        bool mouseInFFT = false;
        bool mouseInWaterfall = false;
        bool horizontalScaleVisible = true;

        float selectedVFOSNR = 0.0f;

        bool centerFrequencyLocked = false;

        std::map<std::string, WaterfallVFO*> vfos;
        std::string selectedVFO = "";
        bool selectedVFOChanged = false;
        Event<std::string> onVFOSelected;
        bool quiet = false;

        struct FFTRedrawArgs {
            ImVec2 min;
            ImVec2 max;
            double lowFreq;
            double highFreq;
            double freqToPixelRatio;
            double pixelToFreqRatio;
            ImGuiWindow* window;
        };

        Event<FFTRedrawArgs> onFFTRedraw;

        /**
         * One complete raw (unzoomed) FFT frame of the captured spectrum, as pushed by the IQ
         * front end. Emitted from the DSP thread once per frame; `data` is only valid for the
         * duration of the call.
         */
        struct RawFFTFrame {
            const float* data;
            int binCount;
            double centerFrequency;
            double spanHz; // whole captured bandwidth
            double usableSpectrumRatio;
        };

        Event<RawFFTFrame> onRawFFT;

        struct WaterfallDrawArgs {
            ImGuiWindow* window;
            ImVec2 wfMin;
            ImVec2 wfMax;

        };

        Event<WaterfallDrawArgs> afterWaterfallDraw;

        struct InputHandlerArgs {
            ImVec2 fftRectMin;
            ImVec2 fftRectMax;
            ImVec2 freqScaleRectMin;
            ImVec2 freqScaleRectMax;
            ImVec2 waterfallRectMin;
            ImVec2 waterfallRectMax;
            double lowFreq;
            double highFreq;
            double freqToPixelRatio;
            double pixelToFreqRatio;
        };

        bool inputHandled = false;
        bool alwaysDrawLine = false;
        bool VFOMoveSingleClick = false;
        Event<InputHandlerArgs> onInputProcess;

        enum {
            REF_LOWER,
            REF_CENTER,
            REF_UPPER,
            _REF_COUNT
        };

        enum {
            BANDPLAN_POS_BOTTOM,
            BANDPLAN_POS_TOP,
            _BANDPLAN_POS_COUNT
        };

        ImVec2 fftAreaMin;
        ImVec2 fftAreaMax;
        ImVec2 freqAreaMin;
        ImVec2 freqAreaMax;
        ImVec2 wfMin;
        ImVec2 wfMax;
        int WATERFALL_NUMBER_OF_SECTIONS = 64;

        bool containsFrequency(double d);
        void updateWaterfallFb(const std::string &where = ""); // called from android, from outside
        ImVec2 widgetPos;
        ImVec2 widgetEndPos;


    private:
        void drawWaterfall();
        void drawFFT();
        void drawDetections();
        void drawNoiseFloor();
        void drawVFOs();
        void drawBandPlan();
        void processInputs();
        void onPositionChange();
        void onResize();
        void updateWaterfallTexture();

        void drawWaterfallImages();

        void updateAllVFOs(bool checkRedrawRequired = false);
        bool calculateVFOSignalInfo(float* fftLine, WaterfallVFO* vfo, float& strength, float& snr);
        void updateSignalInfo(float* fftLine);
        void commitWaterfallRow();
        void applyFFTPostProcessing();
        void resizeRawFFTHistory();

        bool waterfallUpdate = false;

        uint32_t waterfallPallet[WATERFALL_RESOLUTION];

        ImVec2 widgetSize;

        ImVec2 lastWidgetPos;
        ImVec2 lastWidgetSize;

        ImGuiWindow* window;

        std::recursive_mutex buf_mtx;
        std::recursive_mutex latestFFTMtx;
        std::mutex texMtx;
        std::mutex smoothingBufMtx;

        std::mutex detectionMtx;
        std::vector<DetectionMarker> detectionMarkers;
        NoiseFloorOverlay noiseFloorOverlay;

        float vRange;

        int maxVSteps;
        int maxHSteps;

        int dataWidth;           // Width of the FFT/oscilloscope and waterfall, taken from window size, in pixels
        int fftHeight;           // Height of the fft graph, taken from window size, in pixels
        int waterfallHeight = 0; // Height of the waterfall, taken from window size, in pixels

		double usableSpectrumRatio;
        double viewBandwidth;
        double viewOffset;

        double lowerFreq;
        double upperFreq;
        double range;

        float lastDrag;

        int vfoRef = REF_CENTER;

        // Absolute values
        double centerFreq;
        double wholeBandwidth;

        // Ranges
        float fftMin;
        float fftMax;
        float waterfallMin;
        float waterfallMax;

        //std::vector<std::vector<float>> rawFFTs;
        int rawFFTSize = 1;
        std::vector<float> rawFFTsStorage;
        std::vector<float> latestFFTStorage;
        std::vector<float> latestFFTHoldStorage;
        std::vector<float> smoothingBufStorage;
        float* rawFFTs = NULL;
        float* latestFFT = NULL;
        float* latestFFTHold = NULL;
        float* smoothingBuf = NULL;
        int currentFFTLine = 0;
        int fftLines = 0;

        std::vector<uint32_t> waterfallFbStorage;
        std::vector<float> tempDataForUpdateWaterfallFbStorage;
        uint32_t* waterfallFb = NULL;
        float* tempDataForUpdateWaterfallFb = NULL;

        int waterfallFbHeadRowIndex = 0;

        int waterfallMaxSectionHeight = 2;

        int waterfallHeadSectionIndex = 0;
        int waterfallHeadSectionHeight = 0;

        std::vector<GLuint> waterfallTexturesIdsStorage;
        std::vector<int> waterfallTexturesStatusesStorage;
        GLuint* waterfallTexturesIds = NULL;
        int* waterfallTexturesStatuses = NULL;

        bool draggingFW = false;
        int FFTAreaHeight;
        int newFFTAreaHeight;

        bool waterfallVisible = true;
        bool bandplanVisible = false;

        bool _fullUpdate = true;

        int bandPlanPos = BANDPLAN_POS_BOTTOM;

        bool fftHold = false;
        float fftHoldSpeed = 0.3f;

        bool fftSmoothing = false;
        float fftSmoothingAlpha = 0.5;
        float fftSmoothingBeta = 0.5;

        bool snrSmoothing = false;
        float snrSmoothingAlpha = 0.5;
        float snrSmoothingBeta = 0.5;

        // UI Select elements
        bool fftResizeSelect = false;
        bool freqScaleSelect = false;
        bool vfoSelect = false;
        bool vfoBorderSelect = false;
        WaterfallVFO* relatedVfo = NULL;
        ImVec2 mouseDownPos;

        ImVec2 lastMousePos;

        int rawFFTIndex(double frequency) const;
        void testAlloc(const std::string& where);

        std::vector<ImVec2> fftTraceStorage;
        std::vector<ImVec2> fftHoldTraceStorage;
        std::vector<ImVec2> floorTraceStorage;
        std::vector<ImVec2> thresholdTraceStorage;
        std::vector<float> signalInfoScratch;
        int rawFFTLineCapacity = 1;
        GLuint waterfallTextureId = 0;
        bool waterfallTextureNeedsSpecify = true;
        std::vector<uint8_t> waterfallRowsDirty;
    };


};
