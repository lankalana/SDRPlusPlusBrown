#pragma once

// Helpers shared by the DSP tests.
//
// Two styles of test are supported:
//
//   * "cold" tests call a block's process() directly with plain arrays. They are
//     deterministic, fast and don't involve any threading. Prefer them.
//   * "hot" tests start the block's worker thread and push data through real
//     dsp::stream objects. Use StreamFeeder/StreamCollector for those; they
//     exercise the swap/flush/stop protocol that the refactor must preserve.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include <dsp/stream.h>
#include <dsp/types.h>

namespace sdrpp_test {

    inline constexpr double PI = 3.14159265358979323846;

    // ---------------------------------------------------------------- signals

    // Real cosine at `freq` Hz sampled at `sampleRate` Hz.
    inline std::vector<float> cosine(int count, double freq, double sampleRate, double amplitude = 1.0, double phase = 0.0) {
        std::vector<float> out(count);
        for (int i = 0; i < count; i++) {
            out[i] = (float)(amplitude * std::cos(2.0 * PI * freq * (double)i / sampleRate + phase));
        }
        return out;
    }

    // Complex exponential at `freq` Hz sampled at `sampleRate` Hz.
    inline std::vector<dsp::complex_t> complexTone(int count, double freq, double sampleRate, double amplitude = 1.0, double phase = 0.0) {
        std::vector<dsp::complex_t> out(count);
        for (int i = 0; i < count; i++) {
            double t = 2.0 * PI * freq * (double)i / sampleRate + phase;
            out[i] = { (float)(amplitude * std::cos(t)), (float)(amplitude * std::sin(t)) };
        }
        return out;
    }

    inline std::vector<float> constant(int count, float value) {
        return std::vector<float>(count, value);
    }

    inline std::vector<float> ramp(int count, float start = 0.0f, float step = 1.0f) {
        std::vector<float> out(count);
        for (int i = 0; i < count; i++) { out[i] = start + step * (float)i; }
        return out;
    }

    // Deterministic pseudo-random noise. Uses a fixed LCG so failures reproduce.
    inline std::vector<float> noise(int count, unsigned seed = 12345, float amplitude = 1.0f) {
        std::vector<float> out(count);
        unsigned state = seed;
        for (int i = 0; i < count; i++) {
            state = state * 1664525u + 1013904223u;
            out[i] = amplitude * ((float)(state >> 8) / (float)(1u << 23) - 1.0f);
        }
        return out;
    }

    // ----------------------------------------------------------- measurements

    inline double rms(const std::vector<float>& v) {
        if (v.empty()) { return 0.0; }
        double acc = 0.0;
        for (float s : v) { acc += (double)s * (double)s; }
        return std::sqrt(acc / (double)v.size());
    }

    inline double rms(const std::vector<dsp::complex_t>& v) {
        if (v.empty()) { return 0.0; }
        double acc = 0.0;
        for (const auto& s : v) { acc += (double)s.re * s.re + (double)s.im * s.im; }
        return std::sqrt(acc / (double)v.size());
    }

    inline double mean(const std::vector<float>& v) {
        if (v.empty()) { return 0.0; }
        return std::accumulate(v.begin(), v.end(), 0.0) / (double)v.size();
    }

    inline float peak(const std::vector<float>& v) {
        float m = 0.0f;
        for (float s : v) { m = (std::max)(m, std::fabs(s)); }
        return m;
    }

    // Magnitude of the DFT of `v` at `freq` Hz. Used to check filter responses
    // without needing a full FFT.
    inline double goertzelMag(const std::vector<float>& v, double freq, double sampleRate) {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < v.size(); i++) {
            double t = 2.0 * PI * freq * (double)i / sampleRate;
            re += (double)v[i] * std::cos(t);
            im -= (double)v[i] * std::sin(t);
        }
        return std::sqrt(re * re + im * im) / (double)v.size();
    }

    inline double goertzelMag(const std::vector<dsp::complex_t>& v, double freq, double sampleRate) {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < v.size(); i++) {
            double t = 2.0 * PI * freq * (double)i / sampleRate;
            double c = std::cos(t), s = std::sin(t);
            re += (double)v[i].re * c + (double)v[i].im * s;
            im += (double)v[i].im * c - (double)v[i].re * s;
        }
        return std::sqrt(re * re + im * im) / (double)v.size();
    }

    // Frequency response of a real tap set at `freq` Hz, normalized to |H|.
    template <class T>
    inline double tapResponse(const T* taps, unsigned int size, double freq, double sampleRate) {
        double re = 0.0, im = 0.0;
        for (unsigned int i = 0; i < size; i++) {
            double t = 2.0 * PI * freq * (double)i / sampleRate;
            re += (double)taps[i] * std::cos(t);
            im -= (double)taps[i] * std::sin(t);
        }
        return std::sqrt(re * re + im * im);
    }

    // Complex-tap variant (complex band-pass taps are asymmetric).
    inline double tapResponse(const dsp::complex_t* taps, unsigned int size, double freq, double sampleRate) {
        double re = 0.0, im = 0.0;
        for (unsigned int i = 0; i < size; i++) {
            double t = 2.0 * PI * freq * (double)i / sampleRate;
            double c = std::cos(t), s = std::sin(t);
            re += (double)taps[i].re * c + (double)taps[i].im * s;
            im += (double)taps[i].im * c - (double)taps[i].re * s;
        }
        return std::sqrt(re * re + im * im);
    }

    // ------------------------------------------------------- stream plumbing

    // Writes samples into a stream in fixed size chunks. Blocks while the
    // consumer hasn't flushed, exactly like a real producer block would.
    template <class T>
    class StreamFeeder {
    public:
        explicit StreamFeeder(dsp::stream<T>* str) : _str(str) {}

        // Returns false if the stream's writer was stopped mid-way.
        bool feed(const T* data, int count) {
            if (count > _str->getBufferSize()) { return false; }
            memcpy(_str->writeBuf, data, count * sizeof(T));
            return _str->swap(count);
        }

        bool feed(const std::vector<T>& data, int chunk = 0) {
            int total = (int)data.size();
            int step = chunk > 0 ? chunk : total;
            for (int off = 0; off < total; off += step) {
                int n = (std::min)(step, total - off);
                if (!feed(&data[off], n)) { return false; }
            }
            return true;
        }

    private:
        dsp::stream<T>* _str;
    };

    // Drains a stream on a background thread and accumulates everything it saw.
    template <class T>
    class StreamCollector {
    public:
        explicit StreamCollector(dsp::stream<T>* str) : _str(str) {
            _thread = std::thread([this]() { loop(); });
        }

        ~StreamCollector() { stop(); }

        StreamCollector(const StreamCollector&) = delete;
        StreamCollector& operator=(const StreamCollector&) = delete;

        // Waits until at least `count` samples have been collected.
        // Returns false on timeout, which the caller should treat as a failure.
        bool waitFor(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
            std::unique_lock<std::mutex> lck(_mtx);
            return _cv.wait_for(lck, timeout, [&]() { return _data.size() >= count || _done; });
        }

        // Waits until no new sample arrived for `quiet`, i.e. the graph settled.
        void waitIdle(std::chrono::milliseconds quiet = std::chrono::milliseconds(50),
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
            auto deadline = std::chrono::steady_clock::now() + timeout;
            size_t last = size();
            while (std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(quiet);
                size_t now = size();
                if (now == last) { return; }
                last = now;
            }
        }

        size_t size() {
            std::lock_guard<std::mutex> lck(_mtx);
            return _data.size();
        }

        std::vector<T> data() {
            std::lock_guard<std::mutex> lck(_mtx);
            return _data;
        }

        // Number of times the producer swapped, and the size of each swap.
        std::vector<int> chunks() {
            std::lock_guard<std::mutex> lck(_mtx);
            return _chunks;
        }

        void stop() {
            if (_stopped) { return; }
            _stopped = true;
            _str->stopReader();
            if (_thread.joinable()) { _thread.join(); }
            _str->clearReadStop();
        }

    private:
        void loop() {
            while (true) {
                int n = _str->read();
                if (n < 0) { break; }
                {
                    std::lock_guard<std::mutex> lck(_mtx);
                    _data.insert(_data.end(), _str->readBuf, _str->readBuf + n);
                    _chunks.push_back(n);
                }
                _str->flush();
                _cv.notify_all();
            }
            {
                std::lock_guard<std::mutex> lck(_mtx);
                _done = true;
            }
            _cv.notify_all();
        }

        dsp::stream<T>* _str;
        std::thread _thread;
        std::mutex _mtx;
        std::condition_variable _cv;
        std::vector<T> _data;
        std::vector<int> _chunks;
        bool _done = false;
        bool _stopped = false;
    };

    // Small streams keep the tests cheap: the default stream allocates two
    // 1M-sample buffers, which is 16 MB per complex stream.
    template <class T>
    inline void shrink(dsp::stream<T>& str, int samples) {
        str.setBufferSize(samples);
    }

} // namespace sdrpp_test
