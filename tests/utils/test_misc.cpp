// Smaller utils headers: freq_formatting, color, wstr, kmeans and the
// pbkdf2/sha256 implementation used by the server's authentication.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <utils/color.h>
#include <utils/freq_formatting.h>
#include <utils/kmeans.h>
#include <utils/pbkdf2_sha256.h>
#include <utils/wstr.h>

using Catch::Approx;

// ------------------------------------------------------------ formatFreq

TEST_CASE("formatFreq picks a unit and trims trailing zeros", "[utils][freq]") {
    REQUIRE(utils::formatFreq(0.0) == "0Hz");
    REQUIRE(utils::formatFreq(500.0) == "500Hz");
    REQUIRE(utils::formatFreq(1000.0) == "1KHz");
    REQUIRE(utils::formatFreq(1500.0) == "1.5KHz");
    REQUIRE(utils::formatFreq(1000000.0) == "1MHz");
    REQUIRE(utils::formatFreq(14074000.0) == "14.074MHz");
    REQUIRE(utils::formatFreq(145500000.0) == "145.5MHz");
}

TEST_CASE("formatFreq switches units at the thresholds", "[utils][freq]") {
    REQUIRE(utils::formatFreq(999.0) == "999Hz");
    REQUIRE(utils::formatFreq(1000.0) == "1KHz");
    REQUIRE(utils::formatFreq(999999.0) == "999.999KHz");
    REQUIRE(utils::formatFreq(1000000.0) == "1MHz");
    // Note there is no GHz branch: gigahertz frequencies stay in MHz.
    REQUIRE(utils::formatFreq(1.42e9) == "1420MHz");
}

// ---------------------------------------------------------------- color

TEST_CASE("RGBtoHSL and HSLtoRGB round-trip saturated colours", "[utils][color]") {
    struct RGB { float r, g, b; };
    const RGB samples[] = {
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 1.0f },
        { 1.0f, 0.0f, 1.0f },
    };

    for (const auto& s : samples) {
        INFO("rgb " << s.r << "," << s.g << "," << s.b);
        float h, sat, l;
        color::RGBtoHSL(s.r, s.g, s.b, h, sat, l);

        float r, g, b;
        color::HSLtoRGB(h, sat, l, r, g, b);
        REQUIRE(r == Approx(s.r).margin(1e-4));
        REQUIRE(g == Approx(s.g).margin(1e-4));
        REQUIRE(b == Approx(s.b).margin(1e-4));
    }
}

TEST_CASE("RGBtoHSL puts the primaries at the expected hues", "[utils][color]") {
    float h, s, l;

    color::RGBtoHSL(1.0f, 0.0f, 0.0f, h, s, l);
    REQUIRE(h == Approx(0.0f).margin(1e-3));
    REQUIRE(s == Approx(1.0f).margin(1e-3));
    REQUIRE(l == Approx(0.5f).margin(1e-3));

    color::RGBtoHSL(0.0f, 1.0f, 0.0f, h, s, l);
    REQUIRE(h == Approx(120.0f).margin(1e-3));

    color::RGBtoHSL(0.0f, 0.0f, 1.0f, h, s, l);
    REQUIRE(h == Approx(240.0f).margin(1e-3));
}

TEST_CASE("RGBtoHSL reports zero saturation for greys", "[utils][color]") {
    float h, s, l;

    color::RGBtoHSL(0.5f, 0.5f, 0.5f, h, s, l);
    REQUIRE(h == Approx(0.0f));
    REQUIRE(s == Approx(0.0f));
    REQUIRE(l == Approx(0.5f));

    color::RGBtoHSL(0.0f, 0.0f, 0.0f, h, s, l);
    REQUIRE(l == Approx(0.0f));
    REQUIRE(s == Approx(0.0f));

    color::RGBtoHSL(1.0f, 1.0f, 1.0f, h, s, l);
    REQUIRE(l == Approx(1.0f));
    REQUIRE(s == Approx(0.0f));
}

TEST_CASE("HSLtoRGB with zero saturation gives a grey", "[utils][color]") {
    float r, g, b;
    color::HSLtoRGB(200.0f, 0.0f, 0.25f, r, g, b);
    REQUIRE(r == Approx(0.25f));
    REQUIRE(g == Approx(0.25f));
    REQUIRE(b == Approx(0.25f));
}

TEST_CASE("HSLtoRGB covers every hue sector", "[utils][color]") {
    for (float h = 0.0f; h < 360.0f; h += 30.0f) {
        INFO("hue " << h);
        float r, g, b;
        color::HSLtoRGB(h, 1.0f, 0.5f, r, g, b);
        // Fully saturated mid-lightness colours span the whole [0, 1] range.
        REQUIRE(r >= -1e-5f);
        REQUIRE(r <= 1.0f + 1e-5f);
        REQUIRE(g >= -1e-5f);
        REQUIRE(g <= 1.0f + 1e-5f);
        REQUIRE(b >= -1e-5f);
        REQUIRE(b <= 1.0f + 1e-5f);
        REQUIRE(std::max(std::max(r, g), b) == Approx(1.0f).margin(1e-4));
    }
}

// ----------------------------------------------------------------- wstr

TEST_CASE("wstr converts to wide and back", "[utils][wstr]") {
    const std::string original = "Hello, SDR++";
    auto wide = wstr::str2wstr(original);
    REQUIRE(wstr::wstr2str(wide) == original);
}

TEST_CASE("wstr round-trips UTF-8 outside ASCII", "[utils][wstr]") {
    // Written as explicit bytes so the test does not depend on the compiler's
    // source encoding: "äöå" followed by a space and the CJK ideograph U+65E5.
    const std::string original = "\xC3\xA4\xC3\xB6\xC3\xA5 \xE6\x97\xA5";
    auto wide = wstr::str2wstr(original);
    REQUIRE(wstr::wstr2str(wide) == original);
}

TEST_CASE("wstr handles the empty string", "[utils][wstr]") {
    auto wide = wstr::str2wstr("");
    REQUIRE(wstr::wstr2str(wide).empty());
}

// -------------------------------------------------------------- pbkdf2

namespace {
    std::string hex(const uint8_t* data, size_t len) {
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (size_t i = 0; i < len; i++) {
            out += digits[data[i] >> 4];
            out += digits[data[i] & 0xF];
        }
        return out;
    }
}

TEST_CASE("sha256 matches the published test vectors", "[utils][crypto]") {
    uint8_t digest[SHA256_DIGESTLEN];
    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_final(&ctx, digest);
    REQUIRE(hex(digest, sizeof digest) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const char* abc = "abc";
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t*)abc, 3);
    sha256_final(&ctx, digest);
    REQUIRE(hex(digest, sizeof digest) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("sha256 is insensitive to how the input is chunked", "[utils][crypto]") {
    const std::string msg(200, 'x');

    uint8_t whole[SHA256_DIGESTLEN], chunked[SHA256_DIGESTLEN];
    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t*)msg.data(), (uint32_t)msg.size());
    sha256_final(&ctx, whole);

    sha256_init(&ctx);
    for (size_t off = 0; off < msg.size(); off += 7) {
        uint32_t n = (uint32_t)std::min<size_t>(7, msg.size() - off);
        sha256_update(&ctx, (const uint8_t*)msg.data() + off, n);
    }
    sha256_final(&ctx, chunked);

    REQUIRE(hex(whole, sizeof whole) == hex(chunked, sizeof chunked));
}

TEST_CASE("hmac_sha256 matches RFC 4231 test case 2", "[utils][crypto]") {
    const char* key = "Jefe";
    const char* msg = "what do ya want for nothing?";

    HMAC_SHA256_CTX hmac;
    uint8_t digest[SHA256_DIGESTLEN];
    hmac_sha256_init(&hmac, (const uint8_t*)key, 4);
    hmac_sha256_update(&hmac, (const uint8_t*)msg, (uint32_t)strlen(msg));
    hmac_sha256_final(&hmac, digest);

    REQUIRE(hex(digest, sizeof digest) ==
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST_CASE("pbkdf2_sha256 matches RFC 6070 style vectors", "[utils][crypto]") {
    HMAC_SHA256_CTX ctx;
    uint8_t dk[32];

    // PBKDF2-HMAC-SHA256("password", "salt", 1, 32)
    pbkdf2_sha256(&ctx, (const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 1, dk, 32);
    REQUIRE(hex(dk, 32) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");

    // Same inputs, 2 iterations.
    pbkdf2_sha256(&ctx, (const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 2, dk, 32);
    REQUIRE(hex(dk, 32) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
}

TEST_CASE("pbkdf2_sha256 output depends on every input", "[utils][crypto]") {
    HMAC_SHA256_CTX ctx;
    uint8_t a[32], b[32];

    pbkdf2_sha256(&ctx, (const uint8_t*)"pw", 2, (const uint8_t*)"salt1", 5, 10, a, 32);
    pbkdf2_sha256(&ctx, (const uint8_t*)"pw", 2, (const uint8_t*)"salt2", 5, 10, b, 32);
    REQUIRE(hex(a, 32) != hex(b, 32));

    pbkdf2_sha256(&ctx, (const uint8_t*)"pw2", 3, (const uint8_t*)"salt1", 5, 10, b, 32);
    REQUIRE(hex(a, 32) != hex(b, 32));

    pbkdf2_sha256(&ctx, (const uint8_t*)"pw", 2, (const uint8_t*)"salt1", 5, 11, b, 32);
    REQUIRE(hex(a, 32) != hex(b, 32));
}

TEST_CASE("pbkdf2_sha256 supports a derived key longer than one block", "[utils][crypto]") {
    HMAC_SHA256_CTX ctx;
    uint8_t dk[64];
    pbkdf2_sha256(&ctx, (const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 1, dk, 64);

    // The first 32 bytes must match the 32 byte derivation: PBKDF2 blocks are
    // independent and concatenated.
    uint8_t dk32[32];
    pbkdf2_sha256(&ctx, (const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 1, dk32, 32);
    REQUIRE(hex(dk, 32) == hex(dk32, 32));
}

// ---------------------------------------------------------------- kmeans

namespace {
    // Minimal point type satisfying the KMeans template's requirements.
    struct Point1D {
        double x = 0.0;
        int group = 0;

        double kmeansCoord() const { return x; }
        void setKmeansCoord(double v) { x = v; }
        double kmeansDistanceTo(const Point1D* other) const {
            double d = x - other->x;
            return d * d;
        }
    };
}

TEST_CASE("KMeans separates two well-spaced clusters", "[utils][kmeans]") {
    // Deterministic input, but kpp seeds from rand(), so only the partition is
    // asserted, never the cluster numbering.
    std::vector<Point1D> pts;
    for (int i = 0; i < 100; i++) { pts.push_back(Point1D{ 0.0 + i * 0.001, 0 }); }
    for (int i = 0; i < 100; i++) { pts.push_back(Point1D{ 100.0 + i * 0.001, 0 }); }

    KMeans<Point1D> km;
    Point1D* centroids = km.lloyd(pts.data(), (int)pts.size(), 2, 100);
    REQUIRE(centroids != nullptr);

    // All of the first hundred share a group, all of the second hundred share
    // the other one, and the two groups differ.
    for (int i = 1; i < 100; i++) { REQUIRE(pts[i].group == pts[0].group); }
    for (int i = 101; i < 200; i++) { REQUIRE(pts[i].group == pts[100].group); }
    REQUIRE(pts[0].group != pts[100].group);

    // The centroids land near the cluster centres.
    double lo = std::min(centroids[0].x, centroids[1].x);
    double hi = std::max(centroids[0].x, centroids[1].x);
    REQUIRE(lo == Approx(0.05).margin(1.0));
    REQUIRE(hi == Approx(100.05).margin(1.0));

    free(centroids);
}

TEST_CASE("KMeans rejects degenerate configurations", "[utils][kmeans]") {
    std::vector<Point1D> pts = { { 1.0, 0 }, { 2.0, 0 } };
    KMeans<Point1D> km;

    // One cluster, no points, or more clusters than points: all return null.
    REQUIRE(km.lloyd(pts.data(), 2, 1, 10) == nullptr);
    REQUIRE(km.lloyd(pts.data(), 0, 2, 10) == nullptr);
    REQUIRE(km.lloyd(pts.data(), 2, 5, 10) == nullptr);
}

TEST_CASE("KMeans bisectionSearch finds the containing interval", "[utils][kmeans]") {
    KMeans<Point1D> km;
    double cumulative[] = { 1.0, 3.0, 6.0, 10.0 };

    REQUIRE(km.bisectionSearch(cumulative, 4, 0.5) == 0);   // below the first
    REQUIRE(km.bisectionSearch(cumulative, 4, 100.0) == 3); // above the last
    REQUIRE(km.bisectionSearch(cumulative, 4, 2.0) == 1);
    REQUIRE(km.bisectionSearch(cumulative, 4, 7.0) == 3);
    REQUIRE(km.bisectionSearch(cumulative, 0, 1.0) == 0);   // empty input
}

TEST_CASE("KMeans nearest picks the closest centroid", "[utils][kmeans]") {
    KMeans<Point1D> km;
    Point1D centroids[3] = { { 0.0, 0 }, { 10.0, 1 }, { 20.0, 2 } };

    Point1D p{ 9.0, 0 };
    REQUIRE(km.nearest(&p, centroids, 3) == 1);

    Point1D q{ 19.5, 0 };
    REQUIRE(km.nearest(&q, centroids, 3) == 2);

    REQUIRE(km.nearestDistance(&p, centroids, 3) == Approx(1.0));
}
