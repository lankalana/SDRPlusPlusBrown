#pragma once
#include "buffer.h"
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace dsp::buffer {
    // SPSC FIFO. All state and stop predicates are protected by stateMtx.
    template <class T>
    class RingBuffer {
    public:
        RingBuffer() {}
        RingBuffer(int maxLatency) { init(maxLatency); }
        ~RingBuffer() { free(); }

        void init(int maxLatency, int capacity = 0) {
            if (maxLatency <= 0) { throw std::invalid_argument("Ring buffer max latency must be positive"); }
            if (capacity <= 0) { capacity = maxLatency * 2; }
            if (capacity < maxLatency) { capacity = maxLatency; }
            T* newBuffer = buffer::alloc<T>(capacity);
            if (!newBuffer) { throw std::bad_alloc(); }
            buffer::clear(newBuffer, capacity);
            std::lock_guard<std::mutex> lck(stateMtx);
            if (_buffer) { buffer::free(_buffer); }
            _buffer = newBuffer;
            size = capacity;
            this->maxLatency = maxLatency;
            readc = 0;
            writec = 0;
            readable = 0;
            _stopReader = false;
            _stopWriter = false;
        }

        int read(T* data, int len) { return readAndSkip(data, len, 0); }

        int readAndSkip(T* data, int len, int skip) {
            if (len < 0 || skip < 0) { return -1; }
            if (!consume(data, len)) { return -1; }
            if (!consume(NULL, skip)) { return -1; }
            return len;
        }

        int write(const T* data, int len) {
            if (len < 0) { return -1; }
            int written = 0;
            std::unique_lock<std::mutex> lck(stateMtx);
            while (written < len) {
                canWriteVar.wait(lck, [this] { return _stopWriter || writableLocked() > 0; });
                if (_stopWriter) { return -1; }
                int count = (std::min)(len - written, writableLocked());
                copyIn(data + written, count);
                written += count;
                readable += count;
                canReadVar.notify_one();
            }
            return len;
        }

        int waitUntilReadable() {
            std::unique_lock<std::mutex> lck(stateMtx);
            canReadVar.wait(lck, [this] { return _stopReader || readable > 0; });
            return _stopReader ? -1 : readable;
        }

        int waitUntilwritable() {
            std::unique_lock<std::mutex> lck(stateMtx);
            canWriteVar.wait(lck, [this] { return _stopWriter || writableLocked() > 0; });
            return _stopWriter ? -1 : writableLocked();
        }

        int getReadable(bool = true) {
            std::lock_guard<std::mutex> lck(stateMtx);
            return readable;
        }

        int getWritable(bool = true) {
            std::lock_guard<std::mutex> lck(stateMtx);
            return writableLocked();
        }

        void stopReader() {
            std::lock_guard<std::mutex> lck(stateMtx);
            _stopReader = true;
            canReadVar.notify_all();
        }

        void stopWriter() {
            std::lock_guard<std::mutex> lck(stateMtx);
            _stopWriter = true;
            canWriteVar.notify_all();
        }

        bool getReadStop() { std::lock_guard<std::mutex> lck(stateMtx); return _stopReader; }
        bool getWriteStop() { std::lock_guard<std::mutex> lck(stateMtx); return _stopWriter; }
        void clearReadStop() { std::lock_guard<std::mutex> lck(stateMtx); _stopReader = false; }
        void clearWriteStop() { std::lock_guard<std::mutex> lck(stateMtx); _stopWriter = false; }

        void setMaxLatency(int maxLatency) {
            if (maxLatency <= 0) { throw std::invalid_argument("Ring buffer max latency must be positive"); }
            std::lock_guard<std::mutex> lck(stateMtx);
            if (maxLatency > size) {
                growLocked(maxLatency);
            }
            this->maxLatency = maxLatency;
            canWriteVar.notify_all();
        }

    private:
        bool consume(T* data, int len) {
            int consumed = 0;
            std::unique_lock<std::mutex> lck(stateMtx);
            while (consumed < len) {
                canReadVar.wait(lck, [this] { return _stopReader || readable > 0; });
                if (_stopReader) { return false; }
                int count = (std::min)(len - consumed, readable);
                copyOut(data ? data + consumed : NULL, count);
                consumed += count;
                readable -= count;
                canWriteVar.notify_one();
            }
            return true;
        }

        int writableLocked() const {
            return (std::max)(0, (std::min)(size - readable, maxLatency - readable));
        }

        void growLocked(int requiredCapacity) {
            int newSize = (std::max)(requiredCapacity, size * 2);
            T* newBuffer = buffer::alloc<T>(newSize);
            if (!newBuffer) { throw std::bad_alloc(); }

            int first = (std::min)(readable, size - readc);
            if (first) { memcpy(newBuffer, &_buffer[readc], first * sizeof(T)); }
            if (readable > first) { memcpy(newBuffer + first, _buffer, (readable - first) * sizeof(T)); }

            buffer::free(_buffer);
            _buffer = newBuffer;
            size = newSize;
            readc = 0;
            writec = readable;
        }

        void copyIn(const T* data, int count) {
            int first = (std::min)(count, size - writec);
            memcpy(&_buffer[writec], data, first * sizeof(T));
            if (count > first) { memcpy(_buffer, data + first, (count - first) * sizeof(T)); }
            writec = (writec + count) % size;
        }

        void copyOut(T* data, int count) {
            int first = (std::min)(count, size - readc);
            if (data) {
                memcpy(data, &_buffer[readc], first * sizeof(T));
                if (count > first) { memcpy(data + first, _buffer, (count - first) * sizeof(T)); }
            }
            readc = (readc + count) % size;
        }

        void free() {
            std::lock_guard<std::mutex> lck(stateMtx);
            if (_buffer) { buffer::free(_buffer); }
            _buffer = NULL;
        }

        T* _buffer = NULL;
        int size = 0;
        int readc = 0;
        int writec = 0;
        int readable = 0;
        int maxLatency = 0;
        bool _stopReader = false;
        bool _stopWriter = false;
        std::mutex stateMtx;
        std::condition_variable canReadVar;
        std::condition_variable canWriteVar;
    };
}
