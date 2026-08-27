// IQFrontEnd: the block that everything downstream of the source hangs off.
//
// What is covered here:
//   * the two pure helpers (genDCBlockRate, genReshapeParams) that decide the
//     DC blocker corner and the FFT decimation, exactly;
//   * the graph behaviour: bound IQ streams see the samples, VFOs tune and
//     resample, the FFT branch fires at roughly the configured rate.
//
// What is NOT covered, and why:
//   * setFFTSize() calls updateFFTPath(true), which reaches into gui::waterfall.
//     The source itself flags this as making the module untestable. setFFTRate
//     and setFFTWindow take the updateWaterfall == false path and are covered.
//   * setDecimation() calls core::setInputSampleRate(), which touches the
//     global signal path and the GUI.
//   * VFOManager and SinkManager pull in gui/widgets/waterfall.h and ImGui, so
//     they cannot be linked into a headless test binary at all today.
//
// Cost note: IQFrontEnd owns a dsp::buffer::SampleFrameBuffer<complex_t>, which
// allocates 32 buffers of STREAM_BUFFER_SIZE complex samples in init() —
// 256 MB. The tests therefore keep exactly one initialized front end alive at a
// time. See the note in tests/dsp/test_frame_buffer.cpp.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <signal_path/iq_frontend.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace std::chrono_literals;
using namespace sdrpp_test;

namespace {
    // genDCBlockRate and genReshapeParams are protected statics.
    struct Helpers : public IQFrontEnd {
        using IQFrontEnd::genDCBlockRate;
        using IQFrontEnd::genReshapeParams;
    };

    // Counts FFT frames and hands the front end somewhere to write them.
    struct FFTSink {
        std::vector<float> buffer;
        std::atomic<int> frames{ 0 };

        static float* acquire(void* ctx) { return ((FFTSink*)ctx)->buffer.data(); }
        static void release(void* ctx) { ((FFTSink*)ctx)->frames++; }
    };
}

// ------------------------------------------------------------- pure helpers

TEST_CASE("genDCBlockRate scales the corner with the sample rate", "[signalpath][frontend]") {
    // A fixed 50 Hz corner expressed as a fraction of the sample rate.
    REQUIRE(Helpers::genDCBlockRate(48000.0) == Approx(50.0 / 48000.0));
    REQUIRE(Helpers::genDCBlockRate(2400000.0) == Approx(50.0 / 2400000.0));
    // Doubling the rate halves the normalized rate.
    REQUIRE(Helpers::genDCBlockRate(96000.0) == Approx(Helpers::genDCBlockRate(48000.0) / 2.0));
}

TEST_CASE("genReshapeParams keeps a whole FFT frame when it fits", "[signalpath][frontend]") {
    int skip = 0, keep = 0;

    // 48 kHz, 1024 bin FFT, 20 frames per second: 2400 samples per frame, of
    // which 1024 are kept and the rest skipped.
    Helpers::genReshapeParams(48000.0, 1024, 20.0, skip, keep);
    REQUIRE(keep == 1024);
    REQUIRE(skip == 2400 - 1024);
}

TEST_CASE("genReshapeParams keeps a full FFT when the interval is shorter", "[signalpath][frontend]") {
    int skip = 0, keep = 0;

    // 48 kHz, 8192 bin FFT, 60 requested frames per second: a complete FFT
    // overlaps the previous frame to preserve both resolution and update rate.
    Helpers::genReshapeParams(48000.0, 8192, 60.0, skip, keep);
    REQUIRE(keep == 8192);
    REQUIRE(skip == 800 - 8192);
}

TEST_CASE("genReshapeParams handles an exact fit", "[signalpath][frontend]") {
    int skip = 0, keep = 0;
    Helpers::genReshapeParams(48000.0, 4800, 10.0, skip, keep);
    REQUIRE(keep == 4800);
    REQUIRE(skip == 0);
}

TEST_CASE("genReshapeParams always advances overlapping frames", "[signalpath][frontend]") {
    int skip = 0, keep = 0;
    Helpers::genReshapeParams(100.0, 64, 1000.0, skip, keep);
    REQUIRE(keep == 64);
    REQUIRE(skip == 1 - 64);
}

TEST_CASE("genReshapeParams rounds the frame interval", "[signalpath][frontend]") {
    int skip = 0, keep = 0;
    // 44100 / 30 = 1470 exactly.
    Helpers::genReshapeParams(44100.0, 512, 30.0, skip, keep);
    REQUIRE(keep == 512);
    REQUIRE(skip == 1470 - 512);

    // 44100 / 7 = 6300 exactly; a rate that does not divide evenly must still
    // produce a positive interval.
    Helpers::genReshapeParams(44100.0, 512, 13.0, skip, keep);
    REQUIRE(keep == 512);
    REQUIRE(skip > 0);
}

// ------------------------------------------------------------- graph tests

TEST_CASE("IQFrontEnd passes IQ through to a bound stream", "[signalpath][frontend][slow]") {
    const double sr = 48000.0;

    dsp::stream<dsp::complex_t> source;
    source.setBufferSize(8192);

    FFTSink fft;
    fft.buffer.resize(4096);

    IQFrontEnd fe;
    fe.init(&source, sr, /*buffering*/ false, /*decimRatio*/ 1, /*dcBlocking*/ false,
            /*fftSize*/ 1024, /*fftRate*/ 20.0, IQFrontEnd::RECTANGULAR,
            FFTSink::acquire, FFTSink::release, &fft);

    REQUIRE(fe.getSampleRate() == Approx(sr));
    REQUIRE(fe.getEffectiveSamplerate() == Approx(sr));

    dsp::stream<dsp::complex_t> tap;
    tap.setBufferSize(8192);
    fe.bindIQStream(&tap);

    StreamCollector<dsp::complex_t> collector(&tap);
    StreamFeeder<dsp::complex_t> feeder(&source);

    fe.start();
    auto data = complexTone(4096, 1000.0, sr, 0.5);
    REQUIRE(feeder.feed(data, 1024));
    REQUIRE(collector.waitFor(4096));

    auto got = collector.data();
    REQUIRE(got.size() >= 4096);
    for (size_t i = 0; i < 4096; i++) {
        INFO("sample " << i);
        REQUIRE(got[i].re == Approx(data[i].re).margin(1e-6));
        REQUIRE(got[i].im == Approx(data[i].im).margin(1e-6));
    }

    fe.stop();
    collector.stop();
    fe.unbindIQStream(&tap);
}

TEST_CASE("IQFrontEnd drives the FFT branch", "[signalpath][frontend][slow]") {
    const double sr = 48000.0;
    const int fftSize = 1024;

    dsp::stream<dsp::complex_t> source;
    source.setBufferSize(16384);

    FFTSink fft;
    fft.buffer.resize(fftSize);

    IQFrontEnd fe;
    fe.init(&source, sr, false, 1, false, fftSize, 20.0, IQFrontEnd::NUTTALL,
            FFTSink::acquire, FFTSink::release, &fft);

    StreamFeeder<dsp::complex_t> feeder(&source);
    fe.start();

    // 20 frames per second at 48 kHz means one frame per 2400 samples.
    auto data = complexTone(48000, 3000.0, sr, 1.0);
    REQUIRE(feeder.feed(data, 4096));

    // The FFT output is intentionally non-blocking. This synthetic feeder runs
    // much faster than real time, so display frames may be dropped while the
    // FFT worker is busy rather than applying backpressure to the IQ path.
    for (int i = 0; i < 200 && fft.frames.load() < 1; i++) {
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(fft.frames.load() >= 1);

    // The last frame written is a power spectrum in dB; the tone must dominate.
    float peakDb = -1e9f;
    int peakBin = -1;
    for (int i = 0; i < fftSize; i++) {
        if (fft.buffer[i] > peakDb) {
            peakDb = fft.buffer[i];
            peakBin = i;
        }
    }
    // Bin 0 of an unshifted FFT is DC; 3 kHz of 48 kHz over 1024 bins is bin 64.
    REQUIRE(peakBin == Approx(64).margin(2));

    fe.stop();
}

TEST_CASE("IQFrontEnd addVFO tunes and resamples", "[signalpath][frontend][slow]") {
    const double sr = 48000.0;
    const double vfoSr = 12000.0;

    dsp::stream<dsp::complex_t> source;
    source.setBufferSize(16384);

    FFTSink fft;
    fft.buffer.resize(1024);

    IQFrontEnd fe;
    fe.init(&source, sr, false, 1, false, 1024, 20.0, IQFrontEnd::RECTANGULAR,
            FFTSink::acquire, FFTSink::release, &fft);

    auto* vfo = fe.addVFO("test", vfoSr, 6000.0, 5000.0);
    REQUIRE(vfo != nullptr);
    // Note: addVFO already started the VFO, so its output buffer is left at the
    // default size rather than resized under a running block.

    // Adding the same name twice is refused.
    REQUIRE(fe.addVFO("test", vfoSr, 6000.0, 0.0) == nullptr);

    StreamCollector<dsp::complex_t> collector(&vfo->out);
    StreamFeeder<dsp::complex_t> feeder(&source);

    fe.start();
    // A tone 1 kHz above the VFO centre must appear at +1 kHz in the VFO's
    // output, at a quarter of the sample rate.
    auto data = complexTone(65536, 6000.0, sr, 1.0);
    REQUIRE(feeder.feed(data, 4096));
    REQUIRE(collector.waitFor(8192));

    auto got = collector.data();
    std::vector<dsp::complex_t> tail(got.begin() + got.size() / 2, got.end());
    REQUIRE(goertzelMag(tail, 1000.0, vfoSr) > 0.5);
    REQUIRE(goertzelMag(tail, 4000.0, vfoSr) < 0.1);

    fe.stop();
    collector.stop();
    fe.removeVFO("test");
    // Removing a VFO that is gone is a logged no-op, not a crash.
    REQUIRE_NOTHROW(fe.removeVFO("test"));
}

TEST_CASE("IQFrontEnd DC blocking removes a constant offset", "[signalpath][frontend][slow]") {
    const double sr = 48000.0;

    dsp::stream<dsp::complex_t> source;
    source.setBufferSize(16384);

    FFTSink fft;
    fft.buffer.resize(1024);

    IQFrontEnd fe;
    fe.init(&source, sr, false, 1, /*dcBlocking*/ true, 1024, 20.0,
            IQFrontEnd::RECTANGULAR, FFTSink::acquire, FFTSink::release, &fft);

    dsp::stream<dsp::complex_t> tap;
    tap.setBufferSize(16384);
    fe.bindIQStream(&tap);

    StreamCollector<dsp::complex_t> collector(&tap);
    StreamFeeder<dsp::complex_t> feeder(&source);

    fe.start();
    std::vector<dsp::complex_t> dc(65536, { 1.0f, -1.0f });
    REQUIRE(feeder.feed(dc, 4096));
    REQUIRE(collector.waitFor(32768));

    auto got = collector.data();
    // The blocker is a slow high-pass; by the end of the run the offset is gone.
    REQUIRE(std::fabs(got.back().re) < 0.2f);
    REQUIRE(std::fabs(got.back().im) < 0.2f);

    fe.stop();
    collector.stop();
    fe.unbindIQStream(&tap);
}

TEST_CASE("IQFrontEnd setInvertIQ conjugates the stream", "[signalpath][frontend][slow]") {
    const double sr = 48000.0;

    dsp::stream<dsp::complex_t> source;
    source.setBufferSize(8192);

    FFTSink fft;
    fft.buffer.resize(1024);

    IQFrontEnd fe;
    fe.init(&source, sr, false, 1, false, 1024, 20.0, IQFrontEnd::RECTANGULAR,
            FFTSink::acquire, FFTSink::release, &fft);
    fe.setInvertIQ(true);

    dsp::stream<dsp::complex_t> tap;
    tap.setBufferSize(8192);
    fe.bindIQStream(&tap);

    StreamCollector<dsp::complex_t> collector(&tap);
    StreamFeeder<dsp::complex_t> feeder(&source);

    fe.start();
    auto data = complexTone(2048, 1000.0, sr, 0.5);
    REQUIRE(feeder.feed(data, 512));
    REQUIRE(collector.waitFor(2048));

    auto got = collector.data();
    for (size_t i = 0; i < 2048; i++) {
        INFO("sample " << i);
        REQUIRE(got[i].re == Approx(data[i].re).margin(1e-6));
        REQUIRE(got[i].im == Approx(-data[i].im).margin(1e-6));
    }

    fe.stop();
    collector.stop();
    fe.unbindIQStream(&tap);
}

TEST_CASE("IQFrontEnd addPreprocessor inserts a block in the chain",
          "[signalpath][frontend][slow]") {
    const double sr = 48000.0;

    // A trivial preprocessor that scales by two, to prove the chain rewiring
    // actually routes data through it.
    class Doubler : public dsp::Processor<dsp::complex_t, dsp::complex_t> {
        using base_type = dsp::Processor<dsp::complex_t, dsp::complex_t>;

    public:
        int process(int count, dsp::complex_t* in, dsp::complex_t* out) {
            for (int i = 0; i < count; i++) { out[i] = in[i] * 2.0f; }
            return count;
        }
        DEFAULT_PROC_RUN
    };

    dsp::stream<dsp::complex_t> source;
    source.setBufferSize(8192);

    FFTSink fft;
    fft.buffer.resize(1024);

    IQFrontEnd fe;
    fe.init(&source, sr, false, 1, false, 1024, 20.0, IQFrontEnd::RECTANGULAR,
            FFTSink::acquire, FFTSink::release, &fft);

    Doubler doubler;
    doubler.init(nullptr);
    doubler.out.setBufferSize(8192);
    fe.addPreprocessor(&doubler, true);

    dsp::stream<dsp::complex_t> tap;
    tap.setBufferSize(8192);
    fe.bindIQStream(&tap);

    StreamCollector<dsp::complex_t> collector(&tap);
    StreamFeeder<dsp::complex_t> feeder(&source);

    fe.start();
    std::vector<dsp::complex_t> data(1024, { 0.25f, 0.5f });
    REQUIRE(feeder.feed(data, 256));
    REQUIRE(collector.waitFor(1024));

    for (const auto& s : collector.data()) {
        REQUIRE(s.re == Approx(0.5f));
        REQUIRE(s.im == Approx(1.0f));
    }

    fe.stop();
    collector.stop();
    fe.unbindIQStream(&tap);
    fe.removePreprocessor(&doubler);
}

TEST_CASE("IQFrontEnd setSampleRate notifies listeners", "[signalpath][frontend][slow]") {
    dsp::stream<dsp::complex_t> source;
    source.setBufferSize(4096);

    FFTSink fft;
    fft.buffer.resize(1024);

    IQFrontEnd fe;
    fe.init(&source, 48000.0, false, 1, false, 1024, 20.0, IQFrontEnd::RECTANGULAR,
            FFTSink::acquire, FFTSink::release, &fft);

    double reported = 0.0;
    EventHandler<double> handler([](double sr, void* ctx) { *(double*)ctx = sr; }, &reported);
    fe.onEffectiveSampleRateChange.bindHandler(&handler);

    fe.setSampleRate(96000.0);
    REQUIRE(reported == Approx(96000.0));
    REQUIRE(fe.getEffectiveSamplerate() == Approx(96000.0));
    REQUIRE(fe.getSampleRate() == Approx(96000.0));

    fe.onEffectiveSampleRateChange.unbindHandler(&handler);
}

TEST_CASE("IQFrontEnd setFFTRate reconfigures the reshaper", "[signalpath][frontend][slow]") {
    dsp::stream<dsp::complex_t> source;
    source.setBufferSize(4096);

    FFTSink fft;
    fft.buffer.resize(1024);

    IQFrontEnd fe;
    fe.init(&source, 48000.0, false, 1, false, 1024, 20.0, IQFrontEnd::RECTANGULAR,
            FFTSink::acquire, FFTSink::release, &fft);

    REQUIRE(fe.getFFTRate() == Approx(20.0));
    // These take the updateWaterfall == false path, so they are safe headless.
    REQUIRE_NOTHROW(fe.setFFTRate(10.0));
    REQUIRE(fe.getFFTRate() == Approx(10.0));
    REQUIRE_NOTHROW(fe.setFFTWindow(IQFrontEnd::BLACKMAN));
    REQUIRE_NOTHROW(fe.setFFTWindow(IQFrontEnd::NUTTALL));
}

TEST_CASE("IQFrontEnd tracks its stream time", "[signalpath][frontend]") {
    IQFrontEnd fe;
    // getCurrentStreamTime with nothing set returns the wall clock; setting it
    // pins it, which is what the file-source replay path relies on.
    fe.setCurrentStreamTime(1700000000000LL);
    REQUIRE(fe.getCurrentStreamTime() == 1700000000000LL);
}
