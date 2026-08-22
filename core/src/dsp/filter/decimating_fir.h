#pragma once
#include "fir.h"

namespace dsp::filter {
    template <class D, class T>
    class DecimatingFIR : public FIR<D, T> {
        using base_type = FIR<D, T>;
    public:
        using base_type::getMaxInputCount;

        DecimatingFIR() {}

        DecimatingFIR(stream<D>* in, tap<T>& taps, int decimation) { init(in, taps, decimation); }

        void init(stream<D>* in, tap<T>& taps, int decimation) {
            _decimation = decimation;
            base_type::init(in, taps);
        }

        void setTaps(tap<T>& taps) {
            assert(base_type::_block_init);
            base_type::validateTapCount(taps);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            offset = 0;
            base_type::setTaps(taps);
            base_type::tempStart();
        }

        void setDecimation(int decimation) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            _decimation = decimation;
            offset = 0;
            base_type::tempStart();
        }

        void reset() {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            offset = 0;
            base_type::reset();
            base_type::tempStart();
        }

        inline int process(int count, const D* in, D* out) {
            // Copy data to work buffer
            memcpy(base_type::bufStart, in, count * sizeof(D));

            // Do convolution
            int outCount = 0;
            for (; offset < count; offset += _decimation) {
                if constexpr (std::is_same_v<D, float> && std::is_same_v<T, float>) {
                    volk_32f_x2_dot_prod_32f(&out[outCount++], &base_type::buffer[offset], base_type::_taps.taps, base_type::_taps.size);
                }
                if constexpr ((std::is_same_v<D, complex_t> || std::is_same_v<D, stereo_t>) && std::is_same_v<T, float>) {
                    volk_32fc_32f_dot_prod_32fc((lv_32fc_t*)&out[outCount++], (lv_32fc_t*)&base_type::buffer[offset], base_type::_taps.taps, base_type::_taps.size);
                }
                if constexpr ((std::is_same_v<D, complex_t> || std::is_same_v<D, stereo_t>) && std::is_same_v<T, complex_t>) {
                    volk_32fc_x2_dot_prod_32fc((lv_32fc_t*)&out[outCount++], (lv_32fc_t*)&base_type::buffer[offset], (lv_32fc_t*)base_type::_taps.taps, base_type::_taps.size);
                }
            }
            offset -= count;

            // Move unused data
            memmove(base_type::buffer, &base_type::buffer[count], (base_type::_taps.size - 1) * sizeof(D));

            return outCount;
        }

        int run() {
            return runBounded(base_type::_in, base_type::out,
                [this]() { return getMaxInputCount(base_type::out.getBufferSize()); },
                [this](int count, const D* in, D* out) { return process(count, in, out); });
        }

        int getMaxInputCount(int maxOutputCount) const {
            long long outputInputCount = offset + ((long long)maxOutputCount * _decimation);
            if (outputInputCount <= 0) { return 0; }
            return (int)(std::min)(outputInputCount, (long long)base_type::getMaxInputCount());
        }

    protected:
        int _decimation;
        int offset = 0;
    };
}
