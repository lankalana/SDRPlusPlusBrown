#pragma once
#include "frequency_xlator.h"
#include "../multirate/rational_resampler.h"

namespace dsp::channel {
    class RxVFO : public Processor<complex_t, complex_t> {
        using base_type = Processor<complex_t, complex_t>;
    public:
        RxVFO() {}

        RxVFO(stream<complex_t>* in, double inSamplerate, double outSamplerate, double bandwidth, double offset) { init(in, inSamplerate, outSamplerate, bandwidth, offset); }

        ~RxVFO() {
            if (!base_type::_block_init) { return; }
            base_type::stop();
            taps::free(ftaps);
        }

        void init(stream<complex_t>* in, double inSamplerate, double outSamplerate, double bandwidth, double offset) {
            _inSamplerate = inSamplerate;
            _outSamplerate = outSamplerate;
            _bandwidth = bandwidth;
            _offset = offset;
            filterNeeded = (_bandwidth != _outSamplerate);
            ftaps.taps = NULL;

            xlator.init(NULL, -_offset, _inSamplerate);
            resamp.init(NULL, _inSamplerate, _outSamplerate);
            ftaps = generateTaps(_bandwidth, _outSamplerate);
            filter.init(NULL, ftaps);

            base_type::init(in);
        }

        void setInSamplerate(double inSamplerate) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            _inSamplerate = inSamplerate;
            xlator.setOffset(-_offset, _inSamplerate);
            resamp.setInSamplerate(_inSamplerate);
            base_type::tempStart();
        }

        void setOutSamplerate(double outSamplerate, double bandwidth) {
            assert(base_type::_block_init);
            bool newFilterNeeded = (bandwidth != outSamplerate);
            tap<float> newTaps;
            if (newFilterNeeded) {
                newTaps = generateTaps(bandwidth, outSamplerate);
            }
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            TempStopGuard stopGuard(*this);
            try {
                resamp.setOutSamplerate(outSamplerate);
                if (newFilterNeeded) {
                    filter.setTaps(newTaps);
                    taps::free(ftaps);
                    ftaps = newTaps;
                    newTaps.taps = NULL;
                    newTaps.size = 0;
                }
            }
            catch (...) {
                taps::free(newTaps);
                throw;
            }
            _outSamplerate = outSamplerate;
            _bandwidth = bandwidth;
            filterNeeded = newFilterNeeded;
        }

        void setBandwidth(double bandwidth) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            bool newFilterNeeded = (bandwidth != _outSamplerate);
            tap<float> newTaps;
            if (newFilterNeeded) {
                newTaps = generateTaps(bandwidth, _outSamplerate);
            }
            TempStopGuard stopGuard(*this);
            try {
                if (newFilterNeeded) {
                    filter.setTaps(newTaps);
                    taps::free(ftaps);
                    ftaps = newTaps;
                    newTaps.taps = NULL;
                    newTaps.size = 0;
                }
            }
            catch (...) {
                taps::free(newTaps);
                throw;
            }
            _bandwidth = bandwidth;
            filterNeeded = newFilterNeeded;
        }

        void setOffset(double offset) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _offset = offset;
            xlator.setOffset(-_offset, _inSamplerate);
        }

        void reset() {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            xlator.reset();
            resamp.reset();
            filter.reset();
            base_type::tempStart();
        }

        inline int process(int count, const complex_t* in, complex_t* out) {
            xlator.process(count, in, out);
            count = resamp.process(count, out, out);
            if (filterNeeded) {
                filter.process(count, out, out);
            }
            return count;
        }

        int run() {
            return runBounded(base_type::_in, out,
                [this]() {
                    int processOutputCapacity = filterNeeded ? (std::min)(out.getBufferSize(), filter.getMaxInputCount()) : out.getBufferSize();
                    return resamp.getMaxInputCount(processOutputCapacity);
                },
                [this](int count, const complex_t* in, complex_t* out) { return process(count, in, out); });
        }

    protected:
        tap<float> generateTaps(double bandwidth, double outSamplerate) {
            double filterWidth = bandwidth / 2.0;
            tap<float> generatedTaps = taps::lowPass(filterWidth, filterWidth * 0.1, outSamplerate);
            try {
                filter.validateTapCount(generatedTaps);
            }
            catch (...) {
                taps::free(generatedTaps);
                throw;
            }
            return generatedTaps;
        }

        FrequencyXlator xlator;
        multirate::RationalResampler<complex_t> resamp;
        filter::FIR<complex_t, float> filter;
        tap<float> ftaps;
        bool filterNeeded;

        double _inSamplerate;
        double _outSamplerate;
        double _bandwidth;
        double _offset;

    };
}
