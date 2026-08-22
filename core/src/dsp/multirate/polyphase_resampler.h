#pragma once
#include "../processor.h"
#include "../taps/tap.h"
#include "polyphase_bank.h"
#include <stdexcept>

namespace dsp::multirate {
    template<class T>
    class PolyphaseResampler : public Processor<T, T> {
        using base_type = Processor<T, T>;
    public:
        static constexpr int WORK_BUFFER_SIZE = STREAM_BUFFER_SIZE + 64000;

        PolyphaseResampler() {}

        PolyphaseResampler(stream<T>* in, int interp, int decim, tap<float> taps) { init(in, interp, decim, taps); }

        ~PolyphaseResampler() {
            if (!base_type::_block_init) { return; }
            base_type::stop();
            buffer::free(buffer);
            freePolyphaseBank(phases);
        }

        void init(stream<T>* in, int interp, int decim, tap<float> taps) {
            validateConfiguration(interp, decim, taps);
            _interp = interp;
            _decim = decim;
            _taps = taps;

            // Build filter bank
            phases = buildPolyphaseBank(_interp, _taps);

            // Allocate delay buffer
            buffer = buffer::alloc<T>(WORK_BUFFER_SIZE);
            bufStart = &buffer[phases.tapsPerPhase - 1];
            buffer::clear<T>(buffer, phases.tapsPerPhase - 1);

            base_type::init(in);
        }

        void setRatio(int interp, int decim, tap<float>& taps) {
            assert(base_type::_block_init);
            validateConfiguration(interp, decim, taps);
            PolyphaseBank<float> newPhases = buildPolyphaseBank(interp, taps);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            TempStopGuard stopGuard(*this);

            // Update settings
            _interp = interp;
            _decim = decim;
            _taps = taps;

            // Re-generate polyphase bank
            freePolyphaseBank(phases);
            phases = newPhases;

            // Reset buffer
            bufStart = &buffer[phases.tapsPerPhase - 1];
            reset();
        }

        void reset() {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            buffer::clear<T>(buffer, phases.tapsPerPhase - 1);
            phase = 0;
            offset = 0;
            base_type::tempStart();
        }

        inline int process(int count, const T* in, T* out) {
            int outCount = 0;

            // Copy input to buffer
            memcpy(bufStart, in, count * sizeof(T));

            while (offset < count) {
                // Do convolution
                if constexpr (std::is_same_v<T, float>) {
                    volk_32f_x2_dot_prod_32f(&out[outCount++], &buffer[offset], phases.phases[phase], phases.tapsPerPhase);
                }
                if constexpr (std::is_same_v<T, complex_t> || std::is_same_v<T, stereo_t>) {
                    volk_32fc_32f_dot_prod_32fc((lv_32fc_t*)&out[outCount++], (lv_32fc_t*)&buffer[offset], phases.phases[phase], phases.tapsPerPhase);
                }

                // Increment phase
                phase += _decim;

                // Branchless phase advance if phase wrap arround occurs
                offset += phase / _interp;

                // Wrap around if needed
                phase = phase % _interp;
            }
            offset -= count;

            // Move delay
            memmove(buffer, &buffer[count], (phases.tapsPerPhase - 1) * sizeof(T));

            return outCount;
        }

        int getMaxInputCount(int maxOutputCount) const {
            int scratchInputCount = WORK_BUFFER_SIZE - (phases.tapsPerPhase - 1);
            long long outputInputCount = offset + ((long long)phase + ((long long)maxOutputCount * _decim)) / _interp;
            if (outputInputCount <= 0) { return 0; }
            if (outputInputCount > scratchInputCount) { return scratchInputCount; }
            return (int)outputInputCount;
        }

        int run() {
            return runBounded(base_type::_in, base_type::out,
                [this]() { return getMaxInputCount(base_type::out.getBufferSize()); },
                [this](int count, const T* in, T* out) { return process(count, in, out); });
        }

    protected:
        static void validateConfiguration(int interp, int decim, const tap<float>& taps) {
            if (interp <= 0 || decim <= 0 || taps.size <= 0) {
                throw std::invalid_argument("Polyphase resampler ratio and tap count must be positive");
            }
            int tapsPerPhase = (taps.size + interp - 1) / interp;
            if (tapsPerPhase > WORK_BUFFER_SIZE) {
                throw std::invalid_argument("Polyphase resampler tap count exceeds work buffer capacity");
            }
        }

        int _interp;
        int _decim;
        tap<float> _taps;
        PolyphaseBank<float> phases;
        int phase = 0;
        int offset = 0;
        T* buffer;
        T* bufStart;

    };
}
