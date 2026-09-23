#pragma once
#include <cstdint>
#include <vector>

namespace dsp::detector {

    /**
     * Where the detection floor comes from.
     *
     * MANUAL is a single flat level in dB, chosen by the user against what they can see on the
     * waterfall. It is independent of tuning, so it survives retuning and sample-rate changes and
     * needs no measurement at all.
     *
     * MEASURED estimates a per-bin floor from the spectrum on demand, when the user asks for it.
     * The estimator is spectral rather than temporal: for each bin it takes a low percentile over
     * a wide frequency window. A per-bin average over *time* would be wrong on any band full of
     * permanently-on carriers (FM broadcast being the obvious case), because each carrier's own
     * bins would learn the carrier as their floor and only its instantaneous fluctuations would
     * poke through -- yielding a scatter of narrow fragments instead of a few wide stations.
     */
    enum class NoiseFloorMode {
        MANUAL,
        MEASURED
    };

    enum class MeasurementState {
        IDLE,
        MEASURING,
        READY
    };

    const char* toString(NoiseFloorMode mode);
    const char* toString(MeasurementState state);

    /**
     * The detection floor and the threshold derived from it.
     *
     * Threshold is always simply `floor + marginDb`. One knob, so that what the user sees drawn
     * on the waterfall is exactly what decides a detection.
     *
     * Nothing here ever starts by itself: the floor is either set by the user or measured when
     * the user asks. Not thread safe; the owner serializes access.
     */
    class NoiseFloorModel {
    public:
        static constexpr float DEFAULT_MANUAL_FLOOR_DB = -85.0f;
        static constexpr float DEFAULT_MARGIN_DB = 10.0f;
        static constexpr float DEFAULT_PERCENTILE = 0.25f;
        static constexpr double DEFAULT_WINDOW_HZ = 2e6;

        // Tell the model the shape of the spectrum it will be fed. Discards any measured floor if
        // the shape changed.
        void configure(int binCount, double binWidthHz);
        int getBinCount() const { return binCount; }

        void setMode(NoiseFloorMode mode);
        NoiseFloorMode getMode() const { return mode; }

        void setManualFloorDb(float db);
        float getManualFloorDb() const { return manualFloorDb; }

        void setMarginDb(float db);
        float getMarginDb() const { return marginDb; }

        // Width of the frequency window the spectral percentile is taken over, and where in that
        // window the floor sits. The window must be comfortably wider than the widest signal to
        // be detected, or a signal drags its own floor up with it.
        void setSpectralWindowHz(double hz);
        double getSpectralWindowHz() const { return spectralWindowHz; }
        void setSpectralPercentile(float percentile);
        float getSpectralPercentile() const { return spectralPercentile; }

        /**
         * Begin a measurement over `frames` frames. Frames are averaged first to take the
         * variance out, then the spectral percentile is taken. Switches the mode to MEASURED.
         */
        void startMeasurement(int frames);
        void cancelMeasurement();

        // Feed a frame. Returns true on the call that completes a measurement. Frames are ignored
        // unless a measurement is running.
        bool addFrame(const float* fft, int count);

        MeasurementState getMeasurementState() const { return measurementState; }
        float getMeasurementProgress() const;

        // Drop a measured floor. A manual floor is a user setting and is left alone.
        void resetMeasurement();

        // True when the model can decide detections: always in MANUAL, only once measured in
        // MEASURED.
        bool isUsable() const;

        // +infinity when not usable, so nothing can ever be detected against an absent floor.
        float getFloorDb(int bin) const;
        float getThresholdDb(int bin) const;

        // The measured per-bin floor, empty unless a measurement completed.
        const std::vector<float>& getMeasuredFloor() const { return measuredFloor; }

        /**
         * A single flat level representing the floor of one frame, for the "set the manual level
         * from what is on screen" action.
         */
        static float estimateFlatFloorDb(const float* fft, int count,
                                         float percentile = DEFAULT_PERCENTILE);

        /**
         * Per-bin spectral floor: for each bin, the `percentile` value over a window of
         * `windowBins` centred on it. Exposed for testing.
         */
        static void spectralFloor(const float* fft, int count, int windowBins, float percentile,
                                  std::vector<float>& out);

    private:
        void finalizeMeasurement();

        NoiseFloorMode mode = NoiseFloorMode::MANUAL;
        MeasurementState measurementState = MeasurementState::IDLE;

        int binCount = 0;
        double binWidthHz = 0.0;

        float manualFloorDb = DEFAULT_MANUAL_FLOOR_DB;
        float marginDb = DEFAULT_MARGIN_DB;

        double spectralWindowHz = DEFAULT_WINDOW_HZ;
        float spectralPercentile = DEFAULT_PERCENTILE;

        int framesRequired = 0;
        int framesSeen = 0;
        std::vector<double> accumulator;

        std::vector<float> measuredFloor;
    };
}
