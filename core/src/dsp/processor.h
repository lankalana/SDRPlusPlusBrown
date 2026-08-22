#pragma once
#include "block.h"
#include <stdio.h>

// These macros define a run() function using a specic expression for processing
// This is needed because not all process functions have the same arguments

#define OVERRIDE_PROC_RUN(exp)\
    int run() {\
        int count = _in->read();\
        if (count < 0) {\
            return -1;\
        }\
        \
        exp;\
        \
        base_type::_in->flush();\
        if (!base_type::out.swap(count)) { return -1; }\
        return count;\
    }

#define OVERRIDE_MULTIRATE_PROC_RUN(exp)\
    int run() {\
        int count = _in->read();\
        if (count < 0) {\
            return -1;\
        }\
        \
        int outCount = exp;\
        \
        base_type::_in->flush();\
        if (outCount) {\
            if (!base_type::out.swap(outCount)) { return -1; }\
        }\
        return count;\
    }

#define DEFAULT_PROC_RUN            OVERRIDE_PROC_RUN(process(count, base_type::_in->readBuf, base_type::out.writeBuf))
#define DEFAULT_MULTIRATE_PROC_RUN  OVERRIDE_MULTIRATE_PROC_RUN(process(count, base_type::_in->readBuf, base_type::out.writeBuf))

namespace dsp {
    template <class I, class O, class G, class F>
    int runBounded(stream<I>* in, stream<O>& out, G getMaxInputCount, F process) {
        int count = in->read();
        if (count < 0) { return -1; }

        int inputOffset = 0;
        int totalOutCount = 0;
        while (inputOffset < count) {
            int maxInputCount = getMaxInputCount();
            if (maxInputCount <= 0) {
                flog::error("DSP processor output buffer is too small for the configured ratio");
                in->flush();
                return -1;
            }
            int inputCount = (std::min)(count - inputOffset, maxInputCount);
            int outCount = process(inputCount, &in->readBuf[inputOffset], out.writeBuf);
            assert(outCount >= 0 && outCount <= out.getBufferSize());
            if (outCount < 0 || outCount > out.getBufferSize()) {
                flog::error("DSP processor generated {} samples for an output buffer with capacity {}", outCount, out.getBufferSize());
                in->flush();
                return -1;
            }
            totalOutCount += outCount;
            inputOffset += inputCount;
            if (outCount && !out.swap(outCount)) {
                in->flush();
                return -1;
            }
        }
        in->flush();
        return totalOutCount;
    }

    template <class I, class O>
    class Processor : public block {
    public:
        Processor() {
            out.origin = "process.out";
        }

        Processor(stream<I>* in) {
            out.origin = "process.out";
            init(in);
        }

        virtual ~Processor() {}

        virtual void init(stream<I>* in) {
            _in = in;
            registerInput(_in);
            registerOutput(&out);
            _block_init = true;
        }

        virtual void setInput(stream<I>* in) {
            if (!_block_init) {
                fprintf(stderr, "!processor._block_init\n");
                assert(_block_init);
            }
            std::lock_guard<std::recursive_mutex> lck(ctrlMtx);
            tempStop();
            unregisterInput(_in);
            _in = in;
            if (_in) {
                registerInput(_in);
            }
            tempStart();
        }

        FILE *debugF = nullptr;
        FILE *debugF0 = nullptr;

        void debugDump(const std::string &dest) {
            if (false) {
                if (debugF) {
                    fclose(debugF);
                }
                if (debugF0) {
                    fclose(debugF0);
                }
                debugF0 = fopen((dest + ".in").c_str(), "wb");
                debugF = fopen(dest.c_str(), "wb");
                if (debugF) {
                    out.outputHook = [=](const O *buf, int n) {
                        fwrite(buf, sizeof(O *), n, debugF);
                    };
                    out.inputHook = [=](const I *buf, int n) {
                        fwrite(buf, sizeof(I *), n, debugF0);
                    };
                }
            }
        }

        virtual int run() = 0;

        stream<O> out = "processor.out";

    protected:
        stream<I>* _in;
    };
}
