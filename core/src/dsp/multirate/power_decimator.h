#pragma once
#include <stdexcept>
#include "../filter/decimating_fir.h"
#include "../taps/from_array.h"
#include "decim/plans.h"

namespace dsp::multirate {
    template<class T>
    class PowerDecimator : public Processor<T, T> {
        using base_type = Processor<T, T>;
    public:
        PowerDecimator() {}

        PowerDecimator(stream<T>* in, unsigned int ratio) { init(in, ratio); }

        ~PowerDecimator() {
            if (!base_type::_block_init) { return; }
            base_type::stop();
            freeFirs();
        }

        void init(stream<T>* in, unsigned int ratio) {
            validateRatio(ratio);
            _ratio = ratio;
            buildFirs(_ratio, decimFirs, decimTaps, stageCount);
            base_type::init(in);
        }

        static inline unsigned int getMaxRatio() {
            return 1 << decim::plans_len;
        }

        static inline int getMaxPower() {
            return decim::plans_len;
        }

        void setRatio(unsigned int ratio) {
            assert(base_type::_block_init);
            validateRatio(ratio);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            TempStopGuard stopGuard(*this);
            std::vector<filter::DecimatingFIR<T, float>*> newFirs;
            std::vector<tap<float>> newTaps;
            int newStageCount = 0;
            buildFirs(ratio, newFirs, newTaps, newStageCount);

            decimFirs.swap(newFirs);
            decimTaps.swap(newTaps);
            _ratio = ratio;
            stageCount = newStageCount;
            freeFirs(newFirs, newTaps);
        }

        void reset() {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            for (auto& fir : decimFirs) {
                fir->reset();
            }
            base_type::tempStart();
        }

        inline int process(int count, const T* in, T* out) {
            // If the ratio is 1, no need to decimate
            if (_ratio == 1) {
                memcpy(out, in, count * sizeof(T));
                return count;
            }
            
            // Process data through each stage
            const T* data = in;
            int last = stageCount - 1;
            for (int i = 0; i < stageCount; i++) {
                auto fir = decimFirs[i];
                count = fir->process(count, data, out);
                data = out;
            }
            return count;
        }

        int getMaxInputCount() const {
            int maxInputCount = STREAM_BUFFER_SIZE + 64000;
            for (auto fir : decimFirs) {
                maxInputCount = (std::min)(maxInputCount, fir->getMaxInputCount());
            }
            return maxInputCount;
        }

        int run() {
            return runBounded(base_type::_in, base_type::out,
                [this]() { return getMaxInputCount(base_type::out.getBufferSize()); },
                [this](int count, const T* in, T* out) { return process(count, in, out); });
        }

        int getMaxInputCount(int maxOutputCount) const {
            if (_ratio == 1) { return maxOutputCount; }
            return (std::min)(getMaxInputCount(), decimFirs[0]->getMaxInputCount(maxOutputCount));
        }

    protected:
        static void freeFirs(std::vector<filter::DecimatingFIR<T, float>*>& firs, std::vector<tap<float>>& tapsList) {
            for (auto& fir : firs) { delete fir; }
            for (auto& taps : tapsList) { dsp::taps::free(taps); }
            firs.clear();
            tapsList.clear();
        }

        void freeFirs() {
            freeFirs(decimFirs, decimTaps);
        }

        static int planIndex(unsigned int ratio) {
            int exponent = 0;
            while (ratio > 1) {
                ratio >>= 1;
                exponent++;
            }
            return exponent - 1;
        }

        static void buildFirs(unsigned int ratio, std::vector<filter::DecimatingFIR<T, float>*>& firs,
                              std::vector<tap<float>>& tapsList, int& newStageCount) {
            // Generate filters based on DDC plan
            if (ratio > 1) {
                int planId = planIndex(ratio);
                decim::plan plan = decim::plans[planId];
                newStageCount = plan.stageCount;
                try {
                    for (int i = 0; i < newStageCount; i++) {
                        tap<float> newTaps = dsp::taps::fromArray<float>(plan.stages[i].tapcount, plan.stages[i].taps);
                        auto fir = new filter::DecimatingFIR<T, float>(NULL, newTaps, plan.stages[i].decimation);
                        fir->out.free();
                        tapsList.push_back(newTaps);
                        firs.push_back(fir);
                    }
                }
                catch (...) {
                    freeFirs(firs, tapsList);
                    throw;
                }
            }
        }

        static bool checkRatio(unsigned int ratio) {
            // Make sure ratio is a power of two, non-zero and lower or equal to maximum
            return ((ratio & (ratio - 1)) == 0) && ratio && ratio <= getMaxRatio();
        }

        static void validateRatio(unsigned int ratio) {
            if (!checkRatio(ratio)) {
                throw std::invalid_argument("Power decimator ratio must be a supported non-zero power of two");
            }
        }

        std::vector<filter::DecimatingFIR<T, float>*> decimFirs;
        std::vector<tap<float>> decimTaps;
        unsigned int _ratio = 1;
        int stageCount = 0;
    };
}
