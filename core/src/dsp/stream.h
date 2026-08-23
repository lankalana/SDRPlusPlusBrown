#pragma once
#include <assert.h>
#include <string.h>
#include <mutex>
#include <atomic>
#include <functional>
#include <new>
#include <utils/flog.h>
#include <condition_variable>
//#include <volk/volk.h>
#include <utils/usleep.h>

#include "buffer/buffer.h"

// The stream handoff is single-producer/single-consumer. Multiple independent readers are not supported.
inline constexpr int DEFAULT_STREAM_BUFFER_SIZE = 1000000;
#define STREAM_BUFFER_SIZE DEFAULT_STREAM_BUFFER_SIZE

struct lazy_stream_t {};
inline constexpr lazy_stream_t lazy_stream{};

extern void logDebugMessage(const char *msg);

namespace dsp {
    class untyped_stream {
    public:
        virtual ~untyped_stream() {}
        virtual bool swap(int size) { return false; }
        virtual int read() { return -1; }
        virtual void flush() {}
        virtual void stopWriter() {}
        virtual void clearWriteStop() {}
        virtual void stopReader() {}
        virtual void clearReadStop() {}
    };




    template <class T>
    class stream : public untyped_stream {
    public:

        const char *origin;
        char originBuf[100] = "stream without origin";
        bool debugTraffic = false;
        std::function<void(const T*, int)> outputHook;
        std::function<void(const T*, int)> inputHook;
        int nReaders = 0;

        stream() {
            static std::atomic_int streamCount = 0;
            int sc = streamCount.fetch_add(1, std::memory_order_relaxed);
            snprintf(originBuf, sizeof(originBuf), "stream %d", sc);
            this->origin = &originBuf[0];
            initBuffers();
        }
        stream(const char *origin) : stream() {
            this->origin = origin;
        }

        stream(lazy_stream_t, const char *origin) : origin(origin) {}

        void initBuffers() {
            ensureCapacity(DEFAULT_STREAM_BUFFER_SIZE);
        }

        virtual ~stream() {
            free();
        }

        virtual void setBufferSize(int samples) {
            resizeCapacity(samples, true);
        }

        void ensureCapacity(int samples) {
            if (samples <= bufferSize) { return; }
            resizeCapacity(samples, false);
        }

        bool isAllocated() const {
            return writeBuf0 && readBuf0;
        }

        int getBufferSize() const {
            return bufferSize;
        }

        virtual inline bool swap(int size) {
            if (size < 0 || size > bufferSize || !writeBuf || !readBuf) {
                flog::error("Stream {} rejected swap of {} samples with capacity {}", origin, size, bufferSize);
                assert(size >= 0 && size <= bufferSize && writeBuf && readBuf);
                return false;
            }
            {
                // Wait to either swap or stop
                std::unique_lock<std::mutex> lck(swapMtx);
                swapCV.wait(lck, [this] { return (canSwap || writerStop); });

                // If writer was stopped, abandon operation
                if (writerStop) { return false; }

                // Swap buffers
                T* temp = writeBuf;
                writeBuf = readBuf;
                readBuf = temp;
                canSwap = false;
            }

            // Notify reader that some data is ready
            {
                std::lock_guard<std::mutex> lck(rdyMtx);
                dataSize = size;
                dataReady = true;
            }
            rdyCV.notify_all();

            return true;
        }

        virtual inline int read() {
            // Wait for data to be ready or to be stopped
            if (!readBuf0 || !writeBuf) {
                return -1;
            }
            std::unique_lock<std::mutex> lck(rdyMtx);
            nReaders++;
            rdyCV.wait(lck, [this] { return (dataReady || readerStop); });

            auto rv = readerStop ? -1 : dataSize;
            if (debugTraffic) {
                flog::info("reading stream {}: return {} samples", origin, rv);
            }
            nReaders--;
            return (rv);
        }

        virtual inline bool isDataReady() {
            {
                std::lock_guard<std::mutex> lck(rdyMtx);
                return dataReady;
            }
        }

        virtual inline void flush() {
            // Clear data ready
            {
                std::lock_guard<std::mutex> lck(rdyMtx);
                dataReady = false;
            }

            // Notify writer that buffers can be swapped
            {
                std::lock_guard<std::mutex> lck(swapMtx);
                canSwap = true;
            }

            swapCV.notify_all();
        }

        virtual void stopWriter() {
            {
                std::lock_guard<std::mutex> lck(swapMtx);
                writerStop = true;
            }
            swapCV.notify_all();
        }

        virtual void clearWriteStop() {
            std::lock_guard<std::mutex> lck(swapMtx);
            writerStop = false;
        }

        virtual void stopReader() {
            {
                std::lock_guard<std::mutex> lck(rdyMtx);
                readerStop = true;
            }
            rdyCV.notify_all();
            while (true) {
                {
                    std::unique_lock<std::mutex> lck(rdyMtx);
                    if (nReaders == 0) {
                        break;
                    }
                }
                usleep(1000);
            }
        }

        virtual void clearReadStop() {
            std::lock_guard<std::mutex> lck(rdyMtx);
            readerStop = false;
        }

        void free() {
            std::scoped_lock<std::mutex, std::mutex> lck(swapMtx, rdyMtx);
            if (writeBuf0) { buffer::free(writeBuf0); }
            if (readBuf0) { buffer::free(readBuf0); }
            writeBuf0 = NULL;
            readBuf0 = NULL;
            writeBuf = NULL;
            readBuf = NULL;
            bufferSize = 0;
        }

        T* writeBuf = NULL;
        T* readBuf = NULL;
        T* writeBuf0 = NULL;
        T* readBuf0 = NULL;

    private:
        void resizeCapacity(int samples, bool allowShrink) {
            if (samples <= 0) {
                flog::error("Cannot set stream {} capacity to {} samples", origin, samples);
                assert(samples > 0);
                return;
            }
            if (!allowShrink && samples <= bufferSize) { return; }
            std::scoped_lock<std::mutex, std::mutex> lck(swapMtx, rdyMtx);
            bool idle = canSwap && !dataReady && nReaders == 0;
            if (!idle) {
                flog::error("Cannot resize active stream {}", origin);
                assert(idle);
                return;
            }
            T* newWriteBuf = buffer::alloc<T>(samples);
            T* newReadBuf = buffer::alloc<T>(samples);
            if (!newWriteBuf || !newReadBuf) {
                if (newWriteBuf) { buffer::free(newWriteBuf); }
                if (newReadBuf) { buffer::free(newReadBuf); }
                throw std::bad_alloc();
            }
            buffer::free(writeBuf0);
            buffer::free(readBuf0);
            bufferSize = samples;
            writeBuf0 = newWriteBuf;
            readBuf0 = newReadBuf;
            //buffer::register_buffer_dbg(writeBuf0, origin ? origin: "stream without origin, sbs");
            //buffer::register_buffer_dbg(readBuf0, origin ? origin: "stream without origin, sbs");
            readBuf = readBuf0;
            writeBuf = writeBuf0;
        }

        std::mutex swapMtx;
        std::condition_variable swapCV;
        bool canSwap = true;

        std::mutex rdyMtx;
        std::condition_variable rdyCV;
        bool dataReady = false;

        bool readerStop = false;
        bool writerStop = false;

        int dataSize = 0;
        int bufferSize = 0;
    };

    template <class T>
    struct queue {

        std::mutex lock;
        std::vector<T> data;
        std::atomic_int dataSize = 0;
        size_t readOffset = 0;

        void fillFrom(const T*ptr, int size) {
            std::lock_guard lck(lock);
            int pos = data.size();
            data.resize(size + pos);
            memcpy(&data[pos], ptr, size * sizeof(T));
            dataSize = (int)(data.size() - readOffset);
        }

        int maybeFillFrom(const dsp::stream<T> &str) {
            if (str.isDataReady()) {
                return fillFrom(str);
            }
            return 0;
        }

        // waits
        int fillFrom(const dsp::stream<T> &str) {
            int size = str.read();
            std::lock_guard lck(lock);
            if (size >= 0) {
                int pos = data.size();
                data.resize(size + pos);
                memcpy(&data[pos], str.readBuf, size * sizeof(T));
                str.flush();
                dataSize = (int)(data.size() - readOffset);
            }
            return size;
        }

        bool isDataReady(int size) {
            return dataSize >= size;
        }

        bool consume(T *dest, int size) {
            if (!isDataReady(size)) {
                return false;
            }
            std::lock_guard lck(lock);
            return consume_(dest, size);
        }

        bool consume_(T *dest, int size) {
            if (size < 0 || size > dataSize) {
                return false;
            }
            if (dest) {
                memcpy(dest, data.data() + readOffset, size * sizeof(T));
            }
            readOffset += size;
            if (readOffset >= 1024 && readOffset >= data.size() / 2) {
                data.erase(data.begin(), data.begin() + readOffset);
                readOffset = 0;
            }
            dataSize = (int)(data.size() - readOffset);
            return true;
        }

    };

}
