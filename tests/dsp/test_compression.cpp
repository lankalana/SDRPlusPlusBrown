// dsp::compression::SampleStreamCompressor / SampleStreamDecompressor.
//
// This is the wire format used by the network source/sink pair, so the header
// layout is an ABI: two uint16 fields, a float scaler, then the payload. The
// tests pin the layout byte for byte as well as the round-trip accuracy.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <vector>

#include <dsp/compression/sample_stream_compressor.h>
#include <dsp/compression/sample_stream_decompressor.h>

#include "support/dsp_test_helpers.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    using dsp::compression::PCMType;
    using dsp::compression::SampleStreamCompressor;
    using dsp::compression::SampleStreamDecompressor;

    // Worst-case output size for `count` complex samples plus the header.
    std::vector<uint8_t> packetBuffer(int count) {
        return std::vector<uint8_t>(8 + count * sizeof(dsp::complex_t));
    }

    double maxAbsError(const std::vector<dsp::complex_t>& a, const std::vector<dsp::complex_t>& b) {
        double worst = 0.0;
        for (size_t i = 0; i < a.size() && i < b.size(); i++) {
            worst = std::max(worst, (double)std::fabs(a[i].re - b[i].re));
            worst = std::max(worst, (double)std::fabs(a[i].im - b[i].im));
        }
        return worst;
    }
}

TEST_CASE("compressor writes the expected header layout", "[dsp][compression]") {
    const int n = 16;
    std::vector<dsp::complex_t> in(n, { 0.5f, -0.25f });
    auto buf = packetBuffer(n);

    int len = SampleStreamCompressor::process(n, PCMType::PCM_TYPE_I16, in.data(), buf.data());

    uint16_t compressionType, sampleType;
    float scaler;
    memcpy(&compressionType, &buf[0], 2);
    memcpy(&sampleType, &buf[2], 2);
    memcpy(&scaler, &buf[4], 4);

    REQUIRE(compressionType == 0);
    REQUIRE(sampleType == (uint16_t)PCMType::PCM_TYPE_I16);
    REQUIRE(scaler == Approx(0.5f));
    REQUIRE(len == 8 + n * 2 * (int)sizeof(int16_t));
}

TEST_CASE("float32 mode is lossless", "[dsp][compression]") {
    const int n = 256;
    auto re = noise(n, 1234);
    auto im = noise(n, 4321);
    std::vector<dsp::complex_t> in(n);
    for (int i = 0; i < n; i++) { in[i] = { re[i], im[i] }; }

    auto buf = packetBuffer(n);
    int len = SampleStreamCompressor::process(n, PCMType::PCM_TYPE_F32, in.data(), buf.data());
    REQUIRE(len == 8 + n * (int)sizeof(dsp::complex_t));

    std::vector<dsp::complex_t> out(n);
    int outCount = SampleStreamDecompressor::process(len, buf.data(), out.data());
    REQUIRE(outCount == n);

    for (int i = 0; i < n; i++) {
        INFO("sample " << i);
        REQUIRE(out[i].re == in[i].re);
        REQUIRE(out[i].im == in[i].im);
    }
}

TEST_CASE("int16 mode round-trips within a quantisation step", "[dsp][compression]") {
    const int n = 1024;
    auto re = noise(n, 11);
    auto im = noise(n, 22);
    std::vector<dsp::complex_t> in(n);
    for (int i = 0; i < n; i++) { in[i] = { re[i] * 0.9f, im[i] * 0.9f }; }

    auto buf = packetBuffer(n);
    int len = SampleStreamCompressor::process(n, PCMType::PCM_TYPE_I16, in.data(), buf.data());
    REQUIRE(len == 8 + n * 2 * (int)sizeof(int16_t));

    std::vector<dsp::complex_t> out(n);
    REQUIRE(SampleStreamDecompressor::process(len, buf.data(), out.data()) == n);

    // 16 bits over a peak-normalized range: better than 1e-3 everywhere.
    REQUIRE(maxAbsError(in, out) < 1e-3);
}

TEST_CASE("int8 mode round-trips within its coarser step", "[dsp][compression]") {
    const int n = 1024;
    auto re = noise(n, 33);
    auto im = noise(n, 44);
    std::vector<dsp::complex_t> in(n);
    for (int i = 0; i < n; i++) { in[i] = { re[i] * 0.9f, im[i] * 0.9f }; }

    auto buf = packetBuffer(n);
    int len = SampleStreamCompressor::process(n, PCMType::PCM_TYPE_I8, in.data(), buf.data());
    REQUIRE(len == 8 + n * 2 * (int)sizeof(int8_t));

    std::vector<dsp::complex_t> out(n);
    REQUIRE(SampleStreamDecompressor::process(len, buf.data(), out.data()) == n);

    REQUIRE(maxAbsError(in, out) < 0.05);
    // And it is genuinely worse than int16, i.e. the scaler is being applied.
    REQUIRE(maxAbsError(in, out) > 1e-4);
}

TEST_CASE("compression is exact for values that land on the grid", "[dsp][compression]") {
    // With a peak of 1.0 the int8 grid is 1/128. Feeding multiples of it must
    // come back unchanged, which catches off-by-one scaling errors.
    const int n = 8;
    std::vector<dsp::complex_t> in(n);
    for (int i = 0; i < n; i++) { in[i] = { (float)i / 8.0f, -(float)i / 8.0f }; }
    in[n - 1] = { 1.0f, -1.0f }; // set the peak explicitly

    auto buf = packetBuffer(n);
    int len = SampleStreamCompressor::process(n, PCMType::PCM_TYPE_I8, in.data(), buf.data());

    std::vector<dsp::complex_t> out(n);
    SampleStreamDecompressor::process(len, buf.data(), out.data());

    for (int i = 0; i < n - 1; i++) {
        INFO("sample " << i);
        REQUIRE(out[i].re == Approx(in[i].re).margin(1.0 / 128.0));
        REQUIRE(out[i].im == Approx(in[i].im).margin(1.0 / 128.0));
    }
}

TEST_CASE("compressor handles a zero-length packet", "[dsp][compression]") {
    // The scaler is forced to 1.0 for count == 0 to avoid reading an
    // uninitialised max index.
    auto buf = packetBuffer(1);
    dsp::complex_t dummy{ 0.0f, 0.0f };

    int len = SampleStreamCompressor::process(0, PCMType::PCM_TYPE_I16, &dummy, buf.data());
    REQUIRE(len == 8);

    float scaler;
    memcpy(&scaler, &buf[4], 4);
    REQUIRE(scaler == Approx(1.0f));

    std::vector<dsp::complex_t> out(1);
    REQUIRE(SampleStreamDecompressor::process(len, buf.data(), out.data()) == 0);
}

TEST_CASE("decompressor rejects an unknown sample type", "[dsp][compression]") {
    std::vector<uint8_t> buf(64, 0);
    uint16_t bogus = 999;
    memcpy(&buf[2], &bogus, 2);

    std::vector<dsp::complex_t> out(8);
    REQUIRE(SampleStreamDecompressor::process(64, buf.data(), out.data()) == 0);
}

TEST_CASE("round-trip preserves the sample count for every PCM type", "[dsp][compression]") {
    const int n = 300; // deliberately not a power of two
    std::vector<dsp::complex_t> in(n);
    for (int i = 0; i < n; i++) { in[i] = { 0.5f, 0.5f }; }

    for (auto type : { PCMType::PCM_TYPE_I8, PCMType::PCM_TYPE_I16, PCMType::PCM_TYPE_F32 }) {
        INFO("pcm type " << (int)type);
        auto buf = packetBuffer(n);
        int len = SampleStreamCompressor::process(n, type, in.data(), buf.data());
        std::vector<dsp::complex_t> out(n);
        REQUIRE(SampleStreamDecompressor::process(len, buf.data(), out.data()) == n);
    }
}

TEST_CASE("scaler is the maximum value, not the maximum magnitude",
          "[dsp][compression][characterization]") {
    // KNOWN LIMITATION, pinned: the scaler is picked with volk_32f_index_max_32u,
    // which finds the largest signed value. When the negative excursion is
    // larger than the positive one, the negative samples scale past the integer
    // range and clip. Here the peak is -1.0 but the scaler ends up at 0.1, so
    // the reconstruction is badly wrong.
    const int n = 4;
    std::vector<dsp::complex_t> in(n);
    for (int i = 0; i < n; i++) { in[i] = { -1.0f, 0.1f }; }

    auto buf = packetBuffer(n);
    int len = SampleStreamCompressor::process(n, PCMType::PCM_TYPE_I8, in.data(), buf.data());

    float scaler;
    memcpy(&scaler, &buf[4], 4);
    REQUIRE(scaler == Approx(0.1f));

    std::vector<dsp::complex_t> out(n);
    SampleStreamDecompressor::process(len, buf.data(), out.data());

    // The imaginary part (the actual maximum) survives; the real part does not.
    REQUIRE(out[0].im == Approx(0.1f).margin(1e-3));
    REQUIRE(out[0].re != Approx(-1.0f).margin(0.1));
}

TEST_CASE("compressor block sizes its output buffer for the worst case", "[dsp][compression]") {
    // init() must reserve room for a full float32 packet plus the header,
    // otherwise the F32 path overruns the stream buffer.
    SampleStreamCompressor comp;
    comp.init(nullptr, PCMType::PCM_TYPE_F32);
    REQUIRE(comp.out.getBufferSize() >= (int)(STREAM_BUFFER_SIZE * sizeof(dsp::complex_t) + 8));
}
