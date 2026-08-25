#include "fftw_mshv_plug.h"

#ifdef __wasm__

#else


#include "fftw_mshv_plug_original.h"


PlanStorage nativeStorage;


extern "C" {
    FFT_PLAN fftplug_allocate_plan_c2c(int nfft, bool forward) {
        return Fftplug_allocate_plan_c2c(nativeStorage, nfft, forward);
    }

    FFT_PLAN fftplug_allocate_plan_r2c(int nfft) {
        return Fftplug_allocate_plan_r2c(nativeStorage, nfft);
    }

    void fftplug_free_plan(FFT_PLAN plan) {
        Fftplug_free_plan(nativeStorage, plan);
    }

    void fftplug_execute_plan(FFT_PLAN plan, void *source, int sourceSize, void *dest, int destSize) {
        Fftplug_execute_plan(nativeStorage, plan, source, sourceSize, dest, destSize);
    }

}


#endif
