#pragma once
#include <cstdint>
#include <vector>

namespace dsp::detector {

    enum class CalibrationState {
        UNCALIBRATED,
        CALIBRATING,
        READY
    };

    const char* toString(CalibrationState state);

    /**
     * Calibrated noise floor of a single FFT bin.
     *
     * The model is deliberately per-bin: a single global threshold such as "signal > -70 dB" is
     * useless across a spectrum whose floor slopes with the analog filter shape.
     */
    struct NoiseBin {
        float baselineDb = 0.0f;
        float deviationDb = 0.0f;
    };

    /**
     * Collects FFT frames and builds a per-bin noise floor from them using robust statistics
     * (median / MAD), so that a transmission which happens to be on the air during calibration
     * does not drag its bins' baseline up with it.
     *
     * Not thread safe; the owner serializes access.
     */
    class NoiseFloorCalibration {
    public:
        // Frames actually retained for the median. Bounds memory for large FFT sizes: frames
        // beyond this are decimated away evenly across the calibration window, not dropped from
        // the end, so the sample still spans the whole duration.
        static constexpr int DEFAULT_MAX_STORED_FRAMES = 256;

        /**
         * Prepare for a calibration of `framesRequired` frames of `binCount` bins each.
         * Discards any existing model and leaves the state UNCALIBRATED.
         */
        void configure(int binCount, int framesRequired, int maxStoredFrames = DEFAULT_MAX_STORED_FRAMES);

        // Drop the model and any partial collection. State becomes UNCALIBRATED.
        void reset();

        // Start collecting. No-op unless configure() has been called with a valid shape.
        void begin();

        /**
         * Feed one FFT frame. Returns true on the single call that completes the calibration
         * (i.e. the state transitions to READY). Frames are ignored unless CALIBRATING.
         * A frame whose length differs from the configured bin count aborts the calibration.
         */
        bool addFrame(const float* fft, int count);

        CalibrationState getState() const { return state; }
        bool isReady() const { return state == CalibrationState::READY; }

        // 0..1. Meaningful while CALIBRATING; 0 when UNCALIBRATED and 1 when READY.
        float getProgress() const;

        int getBinCount() const { return binCount; }
        int getFramesRequired() const { return framesRequired; }
        int getFramesSeen() const { return framesSeen; }

        const std::vector<NoiseBin>& getBins() const { return bins; }

        /**
         * Detection threshold for a bin:
         *   baseline + max(minimumMarginDb, deviation * deviationMultiplier)
         * Returns +infinity for an out-of-range bin or an uncalibrated model, so that callers
         * comparing "power > threshold" never detect anything by accident.
         */
        float getThresholdDb(int bin) const;
        float getBaselineDb(int bin) const;

        // Both recompute the cached thresholds when the model is READY.
        void setMinimumMarginDb(float margin);
        void setDeviationMultiplier(float multiplier);
        float getMinimumMarginDb() const { return minimumMarginDb; }
        float getDeviationMultiplier() const { return deviationMultiplier; }

    private:
        void finalizeModel();
        void recomputeThresholds();

        CalibrationState state = CalibrationState::UNCALIBRATED;

        int binCount = 0;
        int framesRequired = 0;
        int maxStoredFrames = DEFAULT_MAX_STORED_FRAMES;
        int stride = 1;
        int framesSeen = 0;

        float minimumMarginDb = 10.0f;
        float deviationMultiplier = 3.0f;

        std::vector<std::vector<float>> collected; // frame-major
        std::vector<NoiseBin> bins;
        std::vector<float> thresholds;
    };
}
