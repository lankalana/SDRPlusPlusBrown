// utils/riff.h and utils/wav.h: the recorder's file format layer.
//
// These write real files into the build tree's scratch directory. The RIFF
// tests decode the produced bytes by hand, because the size back-patching is
// the whole point of the class and nothing else verifies it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include <utils/riff.h>
#include <utils/wav.h>

#include "support/tmp_dir.h"

using Catch::Approx;
using namespace sdrpp_test;

namespace {
    std::vector<uint8_t> readAll(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    }

    uint32_t le32(const std::vector<uint8_t>& d, size_t off) {
        uint32_t v;
        memcpy(&v, &d[off], 4);
        return v;
    }

    uint16_t le16(const std::vector<uint8_t>& d, size_t off) {
        uint16_t v;
        memcpy(&v, &d[off], 2);
        return v;
    }

    std::string tag(const std::vector<uint8_t>& d, size_t off) {
        return std::string((const char*)&d[off], 4);
    }
}

// ------------------------------------------------------------------- RIFF

TEST_CASE("riff::Writer produces a well-formed empty container", "[utils][riff]") {
    ScopedTmpFile file("riff_empty");

    {
        riff::Writer w;
        REQUIRE(w.open(file.path(), "TEST"));
        REQUIRE(w.isOpen());
    } // destructor closes

    auto data = readAll(file.path());
    REQUIRE(data.size() == 12);
    REQUIRE(tag(data, 0) == "RIFF");
    REQUIRE(le32(data, 4) == 4); // just the form label
    REQUIRE(tag(data, 8) == "TEST");
}

TEST_CASE("riff::Writer back-patches chunk sizes", "[utils][riff]") {
    ScopedTmpFile file("riff_chunk");

    {
        riff::Writer w;
        REQUIRE(w.open(file.path(), "TEST"));
        w.beginChunk("data");
        const uint8_t payload[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        w.write(std::span{payload});
        w.endChunk();
        w.close();
    }

    auto data = readAll(file.path());
    // 12 byte RIFF header + 8 byte chunk header + 10 bytes of payload.
    REQUIRE(data.size() == 30);
    REQUIRE(tag(data, 0) == "RIFF");
    REQUIRE(le32(data, 4) == 22); // form label + subchunk header + payload
    REQUIRE(tag(data, 12) == "data");
    REQUIRE(le32(data, 16) == 10);
    for (int i = 0; i < 10; i++) { REQUIRE(data[20 + i] == (uint8_t)i); }
}

TEST_CASE("riff::Writer accumulates sizes across several writes", "[utils][riff]") {
    ScopedTmpFile file("riff_multi");

    {
        riff::Writer w;
        REQUIRE(w.open(file.path(), "TEST"));
        w.beginChunk("blob");
        for (int i = 0; i < 5; i++) {
            uint8_t b = (uint8_t)i;
            w.write(std::span{&b, size_t{1}});
        }
        w.endChunk();
        w.close();
    }

    auto data = readAll(file.path());
    REQUIRE(le32(data, 16) == 5);
}

TEST_CASE("riff::Writer nests LIST chunks and rolls sizes up", "[utils][riff]") {
    ScopedTmpFile file("riff_list");

    {
        riff::Writer w;
        REQUIRE(w.open(file.path(), "TEST"));
        w.beginList("INFO");
        w.beginChunk("aaaa");
        const uint8_t payload[4] = { 1, 2, 3, 4 };
        w.write(std::span{payload});
        w.endChunk();
        w.endList();
        w.close();
    }

    auto data = readAll(file.path());
    REQUIRE(tag(data, 12) == "LIST");
    // LIST payload: 4 byte form label + (8 byte header + 4 byte payload).
    REQUIRE(le32(data, 16) == 16);
    REQUIRE(tag(data, 20) == "INFO");
    REQUIRE(tag(data, 24) == "aaaa");
    REQUIRE(le32(data, 28) == 4);
    // The RIFF size covers everything below it.
    REQUIRE(le32(data, 4) == 4 + 8 + 16);
}

TEST_CASE("riff::Writer rejects malformed sequences", "[utils][riff]") {
    ScopedTmpFile file("riff_bad");

    riff::Writer w;
    REQUIRE(w.open(file.path(), "TEST"));

    // endList when the top chunk is not a LIST.
    w.beginChunk("data");
    REQUIRE_THROWS_AS(w.endList(), std::runtime_error);
    w.endChunk();

    // endChunk with only the RIFF chunk left, then again with nothing.
    w.close();
    REQUIRE_FALSE(w.isOpen());
}

TEST_CASE("riff::Writer refuses to write outside a chunk", "[utils][riff]") {
    riff::Writer w;
    const uint8_t b = 0;
    // Nothing opened at all: the chunk stack is empty.
    REQUIRE_THROWS_AS(w.write(std::span{&b, size_t{1}}), std::runtime_error);
}

TEST_CASE("riff::Writer open reports failure on an unwritable path", "[utils][riff]") {
    riff::Writer w;
    REQUIRE_FALSE(w.open(tmpDir() + "/no_such_directory_here/file.riff", "TEST"));
    REQUIRE_FALSE(w.isOpen());
}

// -------------------------------------------------------------------- WAV

TEST_CASE("wav::Writer validates its constructor arguments", "[utils][wav]") {
    REQUIRE_THROWS_AS(wav::Writer(0, 48000), std::runtime_error);
    REQUIRE_THROWS_AS(wav::Writer(2, 0), std::runtime_error);
    REQUIRE_NOTHROW(wav::Writer(1, 8000));
}

TEST_CASE("wav::Writer writes a standard 44 byte header", "[utils][wav]") {
    ScopedTmpFile file("wav_header");

    {
        wav::Writer w(2, 48000, wav::FORMAT_WAV, wav::SAMP_TYPE_INT16);
        REQUIRE(w.open(file.path()));
        REQUIRE(w.isOpen());
        std::vector<float> samples(64 * 2, 0.0f);
        w.write(samples.data(), 64);
        REQUIRE(w.getSamplesWritten() == 64);
        w.close();
    }

    auto data = readAll(file.path());
    REQUIRE(data.size() == 44 + 64 * 2 * 2);

    REQUIRE(tag(data, 0) == "RIFF");
    REQUIRE(tag(data, 8) == "WAVE");
    REQUIRE(tag(data, 12) == "fmt ");
    REQUIRE(le32(data, 16) == 16);
    REQUIRE(le16(data, 20) == wav::CODEC_PCM);
    REQUIRE(le16(data, 22) == 2);          // channels
    REQUIRE(le32(data, 24) == 48000);      // sample rate
    REQUIRE(le32(data, 28) == 48000 * 4);  // bytes per second
    REQUIRE(le16(data, 32) == 4);          // block align
    REQUIRE(le16(data, 34) == 16);         // bit depth
    REQUIRE(tag(data, 36) == "data");
    REQUIRE(le32(data, 40) == 64 * 2 * 2);
    // The RIFF size is the file size minus the 8 byte RIFF header.
    REQUIRE(le32(data, 4) == data.size() - 8);
}

TEST_CASE("wav::Writer marks float32 files with the float codec", "[utils][wav]") {
    ScopedTmpFile file("wav_float");

    {
        wav::Writer w(1, 8000, wav::FORMAT_WAV, wav::SAMP_TYPE_FLOAT32);
        REQUIRE(w.open(file.path()));
        std::vector<float> samples = { 0.25f, -0.5f, 1.0f };
        w.write(samples.data(), 3);
        w.close();
    }

    auto data = readAll(file.path());
    REQUIRE(le16(data, 20) == wav::CODEC_FLOAT);
    REQUIRE(le16(data, 34) == 32);
    REQUIRE(le32(data, 40) == 12);

    // Float samples are written verbatim.
    float v;
    memcpy(&v, &data[44], 4);
    REQUIRE(v == Approx(0.25f));
    memcpy(&v, &data[48], 4);
    REQUIRE(v == Approx(-0.5f));
}

TEST_CASE("wav::Writer scales int16 samples to full scale", "[utils][wav]") {
    ScopedTmpFile file("wav_i16");

    {
        wav::Writer w(1, 8000, wav::FORMAT_WAV, wav::SAMP_TYPE_INT16);
        REQUIRE(w.open(file.path()));
        std::vector<float> samples = { 0.0f, 1.0f, -1.0f, 0.5f };
        w.write(samples.data(), 4);
        w.close();
    }

    auto data = readAll(file.path());
    int16_t s;
    memcpy(&s, &data[44], 2);
    REQUIRE(s == 0);
    memcpy(&s, &data[46], 2);
    REQUIRE(s == 32767);
    memcpy(&s, &data[48], 2);
    REQUIRE(s == -32767);
    memcpy(&s, &data[50], 2);
    REQUIRE(s == Approx(16383).margin(2));
}

TEST_CASE("wav::Writer offsets uint8 samples around 128", "[utils][wav]") {
    ScopedTmpFile file("wav_u8");

    {
        wav::Writer w(1, 8000, wav::FORMAT_WAV, wav::SAMP_TYPE_UINT8);
        REQUIRE(w.open(file.path()));
        std::vector<float> samples = { 0.0f, 1.0f, -1.0f };
        w.write(samples.data(), 3);
        w.close();
    }

    auto data = readAll(file.path());
    REQUIRE(le16(data, 34) == 8);
    REQUIRE(data[44] == 128);
    REQUIRE(data[45] == 255);
    REQUIRE(data[46] == 1);
}

TEST_CASE("wav::Writer refuses parameter changes while open", "[utils][wav]") {
    ScopedTmpFile file("wav_locked");

    wav::Writer w(1, 8000);
    REQUIRE(w.open(file.path()));
    REQUIRE_THROWS_AS(w.setChannels(2), std::runtime_error);
    REQUIRE_THROWS_AS(w.setSamplerate(44100), std::runtime_error);
    REQUIRE_THROWS_AS(w.setSampleType(wav::SAMP_TYPE_FLOAT32), std::runtime_error);
    REQUIRE_THROWS_AS(w.setFormat(wav::FORMAT_RF64), std::runtime_error);
    w.close();

    // Once closed the setters work again, and are validated.
    REQUIRE_NOTHROW(w.setChannels(2));
    REQUIRE_THROWS_AS(w.setChannels(0), std::runtime_error);
    REQUIRE_THROWS_AS(w.setSamplerate(0), std::runtime_error);
}

TEST_CASE("wav::Writer write on a closed file is a no-op", "[utils][wav]") {
    wav::Writer w(1, 8000);
    std::vector<float> samples = { 1.0f };
    REQUIRE_NOTHROW(w.write(samples.data(), 1));
    REQUIRE(w.getSamplesWritten() == 0);
}

TEST_CASE("wav::Writer reopening resets the sample counter", "[utils][wav]") {
    ScopedTmpFile first("wav_reopen_a");
    ScopedTmpFile second("wav_reopen_b");

    wav::Writer w(1, 8000);
    REQUIRE(w.open(first.path()));
    std::vector<float> samples(10, 0.0f);
    w.write(samples.data(), 10);
    REQUIRE(w.getSamplesWritten() == 10);

    REQUIRE(w.open(second.path()));
    REQUIRE(w.getSamplesWritten() == 0);
    w.close();
}

// ------------------------------------------------------------- wav::Reader

TEST_CASE("wav::Reader reads back what wav::Writer wrote", "[utils][wav][reader]") {
    ScopedTmpFile file("wav_roundtrip");

    std::vector<float> samples;
    for (int i = 0; i < 128; i++) { samples.push_back(std::sin(i * 0.1f) * 0.5f); }

    {
        wav::Writer w(1, 44100, wav::FORMAT_WAV, wav::SAMP_TYPE_INT16);
        REQUIRE(w.open(file.path()));
        w.write(samples.data(), 128);
        w.close();
    }

    wav::Reader r(file.path());
    REQUIRE(r.isValid());
    REQUIRE(r.error.empty());
    REQUIRE(r.getChannelCount() == 1);
    REQUIRE(r.getSampleRate() == 44100);
    REQUIRE(r.getBitDepth() == 16);

    std::vector<int16_t> back(128);
    REQUIRE(r.readSamples2(back.data(), 128 * 2) == 128 * 2);
    for (int i = 0; i < 128; i++) {
        INFO("sample " << i);
        REQUIRE((float)back[i] / 32767.0f == Approx(samples[i]).margin(1e-3));
    }
    r.close();
}

TEST_CASE("wav::Reader rejects a file that is not RIFF/WAVE", "[utils][wav][reader]") {
    ScopedTmpFile file("wav_bogus");
    {
        std::ofstream f(file.path(), std::ios::binary);
        std::string junk(64, 'x');
        f.write(junk.data(), junk.size());
    }

    wav::Reader r(file.path());
    REQUIRE_FALSE(r.isValid());
    REQUIRE_FALSE(r.error.empty());
}

TEST_CASE("wav::Reader rewind returns to the first sample", "[utils][wav][reader]") {
    ScopedTmpFile file("wav_rewind");

    std::vector<float> samples = { 0.1f, 0.2f, 0.3f, 0.4f };
    {
        wav::Writer w(1, 8000, wav::FORMAT_WAV, wav::SAMP_TYPE_INT16);
        REQUIRE(w.open(file.path()));
        w.write(samples.data(), 4);
        w.close();
    }

    wav::Reader r(file.path());
    REQUIRE(r.isValid());

    int16_t a[2], b[2];
    REQUIRE(r.readSamples2(a, 4) == 4);
    r.rewind();
    REQUIRE(r.readSamples2(b, 4) == 4);
    REQUIRE(a[0] == b[0]);
    REQUIRE(a[1] == b[1]);
    r.close();
}

TEST_CASE("wav::Reader readSamples2 reports a short read at EOF", "[utils][wav][reader]") {
    ScopedTmpFile file("wav_short");

    std::vector<float> samples = { 0.1f, 0.2f };
    {
        wav::Writer w(1, 8000, wav::FORMAT_WAV, wav::SAMP_TYPE_INT16);
        REQUIRE(w.open(file.path()));
        w.write(samples.data(), 2);
        w.close();
    }

    wav::Reader r(file.path());
    std::vector<uint8_t> buf(1024);
    REQUIRE(r.readSamples2(buf.data(), buf.size()) == 4);
    r.close();
}

TEST_CASE("wav::Reader readSamples loops back to the start", "[utils][wav][reader]") {
    // Characterization: readSamples (unlike readSamples2) wraps to the first
    // sample when it hits EOF, so the caller always gets a full buffer. The
    // baseband player relies on that; a rewrite must not "fix" it silently.
    ScopedTmpFile file("wav_loop");

    std::vector<float> samples = { 0.5f, -0.5f };
    {
        wav::Writer w(1, 8000, wav::FORMAT_WAV, wav::SAMP_TYPE_INT16);
        REQUIRE(w.open(file.path()));
        w.write(samples.data(), 2);
        w.close();
    }

    wav::Reader r(file.path());
    REQUIRE(r.isValid());

    int16_t buf[4] = { 0, 0, 0, 0 };
    r.readSamples(buf, sizeof buf);
    REQUIRE(buf[0] == buf[2]);
    REQUIRE(buf[1] == buf[3]);
    r.close();
}

TEST_CASE("wav::ComplexDumper is safe to use even when disabled", "[utils][wav][dumper]") {
    // On Windows and in release builds it never opens a file at all; the calls
    // must still be harmless.
    wav::ComplexDumper dumper(48000, tmpPath("dump") + ".cf32");
    std::vector<dsp::complex_t> data(16, { 1.0f, 2.0f });
    REQUIRE_NOTHROW(dumper.dump(data.data(), (int)data.size()));
    REQUIRE_NOTHROW(dumper.clear());
}
