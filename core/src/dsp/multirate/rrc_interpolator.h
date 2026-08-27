#pragma once
#include "../multirate/polyphase_resampler.h"
#include "../taps/root_raised_cosine.h"
#include <numeric>

namespace dsp::multirate {
    template <class T>
    class RRCInterpolator : public Processor<T, T> {
        using base_type = Processor<T, T>;
    public:
        RRCInterpolator() {}

        RRCInterpolator(stream<T>* in, double symbolrate, double samplerate, double rrcBeta, int rrcTapCount) { init(in, symbolrate, samplerate, rrcBeta, rrcTapCount); }

        void init(stream<T>* in, double symbolrate, double samplerate, double rrcBeta, int rrcTapCount) {
            _symbolrate = symbolrate;
            _samplerate = samplerate;
            _rrcBeta = rrcBeta;
            _rrcTapCount = rrcTapCount;

            rrcTaps = taps::rootRaisedCosine<float>(_rrcTapCount, rrcBeta, _symbolrate, _samplerate);
            resamp.init(NULL, 1, 1, rrcTaps);
            resamp.out.free();
            genTaps(_symbolrate, _samplerate, _rrcBeta, _rrcTapCount);

            base_type::init(in);
        }

        void setRates(double symbolrate, double samplerate) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            TempStopGuard stopGuard(*this);
            genTaps(symbolrate, samplerate, _rrcBeta, _rrcTapCount);
            _symbolrate = symbolrate;
            _samplerate = samplerate;
        }

        void setRRCParam(double rrcBeta, int rrcTapCount) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            TempStopGuard stopGuard(*this);
            genTaps(_symbolrate, _samplerate, rrcBeta, rrcTapCount);
            _rrcBeta = rrcBeta;
            _rrcTapCount = rrcTapCount;
        }

        void reset() {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            resamp.reset();
            base_type::tempStart();
        }

        inline int process(int count, const T* in, T* out) {
            return resamp.process(count, in, out);
        }

        int run() {
            return runBounded(base_type::_in, base_type::out,
                [this]() { return resamp.getMaxInputCount(base_type::out.getBufferSize()); },
                [this](int count, const T* in, T* out) { return process(count, in, out); });
        }

    private:
        void genTaps(double symbolrate, double samplerate, double rrcBeta, int rrcTapCount) {
            // Calculate the rational samplerate ratio
            int InSR = round(symbolrate);
            int OutSR = round(samplerate);
            int gcd = std::gcd(InSR, OutSR);
            int interp = OutSR / gcd;
            int decim = InSR / gcd;

            // Configure resampler
            double tapSamplerate = symbolrate * (double)interp;
            tap<float> newTaps = taps::rootRaisedCosine<float>(rrcTapCount * interp, rrcBeta, symbolrate, tapSamplerate);
            for (int i = 0; i < newTaps.size; i++) { newTaps.taps[i] *= (float)interp; }
            try {
                resamp.setRatio(interp, decim, newTaps);
            }
            catch (...) {
                taps::free(newTaps);
                throw;
            }
            taps::free(rrcTaps);
            rrcTaps = newTaps;
        }

        double _symbolrate;
        double _samplerate;
        double _rrcBeta;
        int _rrcTapCount;

        tap<float> rrcTaps;
        multirate::PolyphaseResampler<T> resamp;

    };
}
