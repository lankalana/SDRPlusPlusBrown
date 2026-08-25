#pragma once

#include <fftw3.h>
#include "fftw_mshv_plug.h"
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utils/flog.h>

struct FFT_PLAN_IMPL {
    FFT_PLAN_IMPL() = default;
    fftwf_plan plan = nullptr;
    bool inputC = false;
    bool outputC = false;
    bool alive = false;
};

struct PlanStorage {
    std::vector<FFT_PLAN_IMPL> allPlans;
    std::shared_mutex plansLock;
    int allocatePlan() {
        for (std::size_t i = 0; i < allPlans.size(); i++) {
            if (!allPlans[i].alive) {
                allPlans[i].alive = true;
                return static_cast<int>(i);
            }
        }
        FFT_PLAN_IMPL impl;
        impl.alive = true;
        allPlans.emplace_back(impl);
        return static_cast<int>(allPlans.size() - 1);
    }

    void freePlan(int i) {
        if (i < 0 || static_cast<std::size_t>(i) >= allPlans.size() || !allPlans[i].alive) {
            flog::error("freePlan: not allocated");
            return;
        }
        auto &p = allPlans[i];
        p.alive = false;
    }


};

inline FFT_PLAN Fftplug_allocate_plan_c2c(PlanStorage &s, int nfft, bool forward) {
    std::unique_lock lock(s.plansLock);
    int ix = s.allocatePlan();
    auto &p = s.allPlans[ix];
    p.inputC = true;
    p.outputC = true;
    p.plan = fftwf_plan_dft_1d(nfft, nullptr, nullptr, forward ? FFTW_FORWARD: FFTW_BACKWARD, FFTW_ESTIMATE_PATIENT);
    return FFT_PLAN {ix};
}

inline FFT_PLAN Fftplug_allocate_plan_r2c(PlanStorage &s, int nfft) {
    std::unique_lock lock(s.plansLock);
    int ix = s.allocatePlan();
    auto &p = s.allPlans[ix];
    p.inputC = false;
    p.outputC = true;
    p.plan = fftwf_plan_dft_r2c_1d(nfft, nullptr, nullptr, FFTW_ESTIMATE_PATIENT);
    return FFT_PLAN {ix};
}


inline void Fftplug_free_plan(PlanStorage &s, FFT_PLAN plan) {
    std::unique_lock lock(s.plansLock);
    if (plan.handle < 0 || static_cast<std::size_t>(plan.handle) >= s.allPlans.size() || !s.allPlans[plan.handle].alive) {
        flog::error("fftplug_free_plan: invalid plan");
        return;
    }
    fftwf_destroy_plan(s.allPlans[plan.handle].plan);
    s.allPlans[plan.handle].plan = nullptr;
    s.freePlan(plan.handle);
}

inline void Fftplug_execute_plan(PlanStorage &s, FFT_PLAN plan, void *source, int, void *dest, int) {
    std::shared_lock lock(s.plansLock);
    if (plan.handle < 0 || static_cast<std::size_t>(plan.handle) >= s.allPlans.size() || !s.allPlans[plan.handle].alive) {
        flog::error("fftplug_execute_plan: invalid plan");
        return;
    }
    auto &impl = s.allPlans[plan.handle];
    if (impl.inputC && impl.outputC) {
        fftwf_execute_dft(impl.plan, (fftwf_complex *)source, (fftwf_complex *)dest);
    } else {
        fftwf_execute_dft_r2c(impl.plan, (float*)source, (fftwf_complex*)dest);
    }
}



extern std::shared_ptr<PlanStorage> nativeStorage;
