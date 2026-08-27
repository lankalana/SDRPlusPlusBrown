/* The algorithms, source code, look-and-feel of WSJT-X and related
 * programs, and protocol specifications for the modes FSK441, FT8, JT4,
 * JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144, are Copyright © 2001-2017
 * by one or more of the following authors: Joseph Taylor, K1JT; Bill
 * Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, IV3NWV; Greg Beam,
 * KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; Philip Karn, KA9Q;
 * and other members of the WSJT Development Group.
 *
 * MSHV Decoder
 * Rewritten into C++ and modified by Hrisimir Hristov, LZ2HV 2015-2017
 * May be used under the terms of the GNU General Public License (GPL)
 */

#include "decoderms.h"

#include <algorithm>
#include <exception>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
constexpr int FT8_SAMPLE_COUNT = 16 * 12000;
constexpr int FT4_SAMPLE_COUNT = 8 * 12000;

class WorkingReset {
public:
    explicit WorkingReset(std::atomic_bool& working) : working(working) {}
    ~WorkingReset() {
        working.store(false);
    }

private:
    std::atomic_bool& working;
};
}

DecoderMs::DecoderMs() : f2a(std::make_shared<F2a>())
{
}

DecoderMs::~DecoderMs() = default;

void DecoderMs::setMode(int newMode)
{
    if (newMode != MODE_FT8 && newMode != MODE_FT4) {
        throw std::invalid_argument("DecoderMs supports only FT8 and FT4");
    }
    if (mode != newMode) {
        f2a->DestroyPlansAll(true);
        mode = newMode;
    }
}

void DecoderMs::SetDecoderDeep(int depth)
{
    decoderDepth = depth;
    if (mode == MODE_FT8) {
        ft8Decoder(0).SetStDecoderDeep(depth);
    } else {
        ft4Decoder(0).SetStDecoderDeep(depth);
    }
}

void DecoderMs::SetThrLevel(int threads)
{
    workerCount = std::clamp(threads,1,MAX_WORKERS);
}

void DecoderMs::SetWords(QStringList words,int newContestCq,int newContestType)
{
    myCall = words.count() > 0 && !words.at(0).isEmpty() ? words.at(0) : QString("NOT__EXIST");
    myBaseCall = words.count() > 1 && !words.at(1).isEmpty() ? words.at(1) : QString("NOT__EXIST");
    contestCq = newContestCq;
    contestType = newContestType;

    if (mode == MODE_FT8) {
        ft8Decoder(0).SetStWords(myCall,myBaseCall,contestCq,contestType);
    } else {
        ft4Decoder(0).SetStWords(myCall,myBaseCall,contestCq,contestType);
    }
}

void DecoderMs::SetCalsHash(QStringList calls)
{
    hisCall = calls.count() > 1 && !calls.at(1).isEmpty() ? calls.at(1) : QString("NOCALL");
    if (mode == MODE_FT8) {
        ft8Decoder(0).SetStHisCall(hisCall);
    } else {
        ft4Decoder(0).SetStHisCall(hisCall);
    }
}

void DecoderMs::SetResultsCallback(std::function<void(const char *)> callback)
{
    resultsCallback = std::move(callback);
    for (auto& decoder : ft8Decoders) {
        if (decoder) {
            decoder->SetResultsCallback(resultsCallback);
        }
    }
    for (auto& decoder : ft4Decoders) {
        if (decoder) {
            decoder->SetResultsCallback(resultsCallback);
        }
    }
}

bool DecoderMs::IsWorking() const
{
    return working.load();
}

int DecoderMs::effectiveWorkerCount() const
{
    const double minimumBandwidth = mode == MODE_FT8 ? 300.0 : 400.0;
    int count = workerCount;
    while (count > 1 && (highFrequency-lowFrequency)/count < minimumBandwidth) {
        --count;
    }
    return count;
}

void DecoderMs::configure(DecoderFt8& decoder)
{
    decoder.SetStWords(myCall,myBaseCall,contestCq,contestType);
    decoder.SetStHisCall(hisCall);
    decoder.SetStDecoderDeep(decoderDepth);
    decoder.SetResultsCallback(resultsCallback);
}

void DecoderMs::configure(DecoderFt4& decoder)
{
    decoder.SetStWords(myCall,myBaseCall,contestCq,contestType);
    decoder.SetStHisCall(hisCall);
    decoder.SetStDecoderDeep(decoderDepth);
    decoder.SetResultsCallback(resultsCallback);
}

DecoderFt8& DecoderMs::ft8Decoder(int worker)
{
    auto& decoder = ft8Decoders.at(worker);
    if (!decoder) {
        decoder = std::make_unique<DecoderFt8>(worker,f2a);
        configure(*decoder);
    }
    return *decoder;
}

DecoderFt4& DecoderMs::ft4Decoder(int worker)
{
    auto& decoder = ft4Decoders.at(worker);
    if (!decoder) {
        decoder = std::make_unique<DecoderFt4>(worker,f2a);
        configure(*decoder);
    }
    return *decoder;
}

void DecoderMs::SetDecode(short *samples,int count,QString time,int,int mouseButton,bool,bool,bool fileOpen)
{
    const int minimumSamples = mode == MODE_FT8 ? 5*12000 : 4*12000;
    if (!samples || count < minimumSamples) {
        return;
    }

    bool expected = false;
    if (!working.compare_exchange_strong(expected,true)) {
        return;
    }
    WorkingReset reset(working);

    const int maximumSamples = mode == MODE_FT8 ? FT8_SAMPLE_COUNT : FT4_SAMPLE_COUNT;
    const int sampleCount = (std::min)(count,maximumSamples);
    const int workers = effectiveWorkerCount();

    std::vector<std::vector<double>> decodeBuffers(
        workers,std::vector<double>(maximumSamples,0.0));

    const double mean = std::accumulate(samples,samples+sampleCount,0.0)/sampleCount;
    std::transform(samples,samples+sampleCount,decodeBuffers[0].begin(),
                   [mean](short sample) { return 0.1*(sample-mean); });
    for (int worker = 1; worker < workers; ++worker) {
        std::copy(decodeBuffers[0].begin(),decodeBuffers[0].end(),decodeBuffers[worker].begin());
    }

    if (mode == MODE_FT8) {
        ft8Decoder(0).SetStDecode(time,mouseButton,fileOpen);
        for (int worker = 1; worker < workers; ++worker) {
            ft8Decoder(worker);
        }
    } else {
        ft4Decoder(0).SetStDecode(time,mouseButton);
        for (int worker = 1; worker < workers; ++worker) {
            ft4Decoder(worker);
        }
    }

    std::array<double,MAX_WORKERS+1> bounds{};
    bounds[0] = lowFrequency;
    bounds[workers] = highFrequency;
    const double slice = (highFrequency-lowFrequency)/workers;
    const double boundaryShift = mode == MODE_FT8 ? 25.0 : 50.0;
    for (int worker = 1; worker < workers; ++worker) {
        bounds[worker] = lowFrequency+slice*worker;
        if (std::abs(receiveFrequency-bounds[worker]) < 11.0) {
            bounds[worker] -= boundaryShift;
        }
    }

    std::array<bool,MAX_WORKERS> decoded{};
    const double overlap = mode == MODE_FT8 ? 50.0 : 100.0;
    const int decodePass = mouseButton > 3 ? mouseButton-3 : 100;
    const auto decodeWorker = [&](int worker) {
        const double low = worker == 0 ? bounds[worker] : bounds[worker]-overlap;
        const double high = bounds[worker+1];
        if (mode == MODE_FT8) {
            ft8Decoder(worker).ft8_decode(
                decodeBuffers[worker].data(),sampleCount,low,high,receiveFrequency,
                decoded[worker],decodePass,lowFrequency,highFrequency);
        } else {
            ft4Decoder(worker).ft4_decode(
                decodeBuffers[worker].data(),low,high,lowFrequency,highFrequency,
                receiveFrequency,decoded[worker]);
        }
    };

    if (workers == 1) {
        decodeWorker(0);
        return;
    }

    std::exception_ptr decodeException;
    std::mutex exceptionMutex;
    std::vector<std::jthread> threads;
    threads.reserve(workers);
    for (int worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&,worker] {
            try {
                decodeWorker(worker);
            } catch (...) {
                std::lock_guard lock(exceptionMutex);
                if (!decodeException) {
                    decodeException = std::current_exception();
                }
            }
        });
    }
    threads.clear();

    if (decodeException) {
        std::rethrow_exception(decodeException);
    }
}
