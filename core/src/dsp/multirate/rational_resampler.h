#pragma once
#include <vector>
#include <numeric>
#include "../processor.h"
//#include "../filter/decimating_fir.h"
//#include "../taps/from_array.h"
#include "polyphase_resampler.h"
#include "power_decimator.h"
#include "../taps/low_pass.h"
#include "../window/nuttall.h"

namespace dsp::multirate {
    template<class T>
    class RationalResampler : public Processor<T, T> {
        using base_type = Processor<T, T>;
    public:
        RationalResampler() {}

        RationalResampler(stream<T>* in, double inSamplerate, double outSamplerate) { init(in, inSamplerate, outSamplerate); }

        ~RationalResampler() {
            if (!base_type::_block_init) { return; }
            base_type::stop();
            taps::free(rtaps);
        }

        void init(stream<T>* in, double inSamplerate, double outSamplerate) {
            // Dummy initialization since only used for processing
            rtaps = taps::lowPass(0.25, 0.1, 1.0);
            decim.init(NULL, 2);
            resamp.init(NULL, 1, 1, rtaps);

            decim.out.free();
            resamp.out.free();

            // Proper configuration
            reconfigure(inSamplerate, outSamplerate);
            _inSamplerate = inSamplerate;
            _outSamplerate = outSamplerate;

            base_type::init(in);
        }

        void reset() {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            base_type::tempStop();
            decim.reset();
            resamp.reset();
            base_type::tempStart();
        }

        void setInSamplerate(double inSamplerate) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            TempStopGuard stopGuard(*this);
            reconfigure(inSamplerate, _outSamplerate);
            _inSamplerate = inSamplerate;
        }

        void setOutSamplerate(double outSamplerate) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            TempStopGuard stopGuard(*this);
            reconfigure(_inSamplerate, outSamplerate);
            _outSamplerate = outSamplerate;
        }

        void setRates(double inSamplerate, double outSamplerate) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            TempStopGuard stopGuard(*this);
            reconfigure(inSamplerate, outSamplerate);
            _inSamplerate = inSamplerate;
            _outSamplerate = outSamplerate;
        }

        double getInSampleRate() {
            return _inSamplerate;
        }

        double getOutSampleRate() {
            return _outSamplerate;
        }

        inline int process(int count, const T* in, T* out) {
            switch(mode) {
                case Mode::BOTH:
                    count = decim.process(count, in, out);
                    return resamp.process(count, out, out);
                case Mode::DECIM_ONLY:
                    return decim.process(count, in, out);
                case Mode::RESAMP_ONLY:
                    return resamp.process(count, in, out);
                case Mode::NONE:
                    memcpy(out, in, count * sizeof(T));
                    return count;
            }
            return count;
        }

        int getMaxInputCount(int maxOutputCount) const {
            switch(mode) {
                case Mode::BOTH:
                    return decim.getMaxInputCount((std::min)(maxOutputCount, resamp.getMaxInputCount(maxOutputCount)));
                case Mode::DECIM_ONLY:
                    return decim.getMaxInputCount(maxOutputCount);
                case Mode::RESAMP_ONLY:
                    return resamp.getMaxInputCount(maxOutputCount);
                case Mode::NONE:
                    return maxOutputCount;
            }
            return maxOutputCount;
        }

        int run() {
            return runBounded(base_type::_in, base_type::out,
                [this]() { return getMaxInputCount(base_type::out.getBufferSize()); },
                [this](int count, const T* in, T* out) { return process(count, in, out); });
        }

    protected:
        enum Mode {
            BOTH,
            DECIM_ONLY,
            RESAMP_ONLY,
            NONE
        };

        void reconfigure(double inSamplerate, double outSamplerate) {
            // Calculate highest power-of-two decimation for the power decimator 
            int predecPower = std::min<int>(floor(log2(inSamplerate / outSamplerate)), PowerDecimator<T>::getMaxRatio());
            int predecRatio = std::min<int>(1 << predecPower, PowerDecimator<T>::getMaxRatio());
            double intSamplerate = inSamplerate;

            // Configure the DDC
            bool useDecim = (inSamplerate > outSamplerate && predecPower > 0);
            if (useDecim) {
                intSamplerate = inSamplerate / (double)predecRatio;
            }

            // Calculate interpolation and decimation for polyphase resampler
            int IntSR = round(intSamplerate);
            int OutSR = round(outSamplerate);
            int gcd = std::gcd(IntSR, OutSR);
            int interp = OutSR / gcd;
            int decim = IntSR / gcd;

            // Check for excessive error
            double actualOutSR = (double)IntSR * (double)interp / (double)decim;
            double error = abs((actualOutSR - outSamplerate) / outSamplerate) * 100.0;
            if (error > 0.01) {
                fprintf(stderr, "Warning: resampling error is over 0.01%%: %lf\n", error);
            }
            
            // If the power decimator already did all the work, don't use the resampler
            if (interp == decim) {
                if (useDecim) { this->decim.setRatio(predecRatio); }
                mode = useDecim ? Mode::DECIM_ONLY : Mode::NONE;
                return;
            }

            // Configure the polyphase resampler
            double tapSamplerate = intSamplerate * (double)interp;
            double tapBandwidth = std::min<double>(inSamplerate, outSamplerate) / 2.0;
            double tapTransWidth = tapBandwidth * 0.1;
            tap<float> newTaps = taps::lowPass(tapBandwidth, tapTransWidth, tapSamplerate);
            for (int i = 0; i < newTaps.size; i++) { newTaps.taps[i] *= (float)interp; }
            try {
                resamp.setRatio(interp, decim, newTaps);
            }
            catch (...) {
                taps::free(newTaps);
                throw;
            }
            taps::free(rtaps);
            rtaps = newTaps;
            if (useDecim) { this->decim.setRatio(predecRatio); }

//            printf("[Resamp] predec: %d, interp: %d, decim: %d, inacc: %lf%%, taps: %d\n", predecRatio, interp, decim, error, rtaps.size);

            mode = useDecim ? Mode::BOTH : Mode::RESAMP_ONLY;
        }
        
        PowerDecimator<T> decim;
        PolyphaseResampler<T> resamp;
        tap<float> rtaps;
        double _inSamplerate;
        double _outSamplerate;
        Mode mode;
    };
}
