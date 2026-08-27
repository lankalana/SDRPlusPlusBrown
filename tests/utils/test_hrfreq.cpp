// Human readable frequency parsing and formatting.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include <utils/hrfreq.h>

using Catch::Approx;

namespace {
    double parseFrequency(std::string_view text) {
        auto result = hrfreq::fromString(text);
        REQUIRE(result);
        return *result;
    }
}

TEST_CASE("hrfreq::toString picks the right unit", "[utils][hrfreq]") {
    REQUIRE(hrfreq::toString(0.0) == "0Hz");
    REQUIRE(hrfreq::toString(100.0) == "100Hz");
    REQUIRE(hrfreq::toString(999.0) == "999Hz");
    REQUIRE(hrfreq::toString(1000.0) == "1KHz");
    REQUIRE(hrfreq::toString(1500.0) == "1.5KHz");
    REQUIRE(hrfreq::toString(1e6) == "1MHz");
    REQUIRE(hrfreq::toString(14074000.0) == "14.074MHz");
    REQUIRE(hrfreq::toString(1e9) == "1GHz");
    REQUIRE(hrfreq::toString(1.42e9) == "1.42GHz");
}

TEST_CASE("hrfreq::toString trims trailing zeros", "[utils][hrfreq]") {
    REQUIRE(hrfreq::toString(1000000.0) == "1MHz");
    REQUIRE(hrfreq::toString(1100000.0) == "1.1MHz");
    REQUIRE(hrfreq::toString(1010000.0) == "1.01MHz");
    REQUIRE(hrfreq::toString(1000001.0) == "1.000001MHz");
}

TEST_CASE("hrfreq::fromString understands unit suffixes", "[utils][hrfreq]") {
    REQUIRE(parseFrequency("14.074MHz") == Approx(14074000.0));
    REQUIRE(parseFrequency("7k") == Approx(7000.0));
    REQUIRE(parseFrequency("1G") == Approx(1e9));
    REQUIRE(parseFrequency("500Hz") == Approx(500.0));
}

TEST_CASE("hrfreq::fromString is case insensitive on the unit", "[utils][hrfreq]") {
    const double a = parseFrequency("14.074mhz");
    const double b = parseFrequency("14.074MHZ");
    REQUIRE(a == Approx(b));
    REQUIRE(a == Approx(14074000.0));
}

TEST_CASE("hrfreq::fromString without a unit assumes Hz", "[utils][hrfreq]") {
    REQUIRE(parseFrequency("1234") == Approx(1234.0));
}

TEST_CASE("hrfreq::fromString ignores thousands separators", "[utils][hrfreq]") {
    REQUIRE(parseFrequency("14,074,000Hz") == Approx(14074000.0));
}

TEST_CASE("hrfreq::fromString skips leading junk", "[utils][hrfreq]") {
    REQUIRE(parseFrequency("freq: 145.500MHz") == Approx(145500000.0));
}

TEST_CASE("hrfreq::fromString handles negative values", "[utils][hrfreq]") {
    REQUIRE(parseFrequency("-2.5kHz") == Approx(-2500.0));
}

TEST_CASE("hrfreq::fromString reports failure on garbage", "[utils][hrfreq]") {
    const auto empty = hrfreq::fromString("");
    const auto garbage = hrfreq::fromString("abc");
    REQUIRE_FALSE(empty);
    REQUIRE_FALSE(garbage);
    REQUIRE_FALSE(empty.error().empty());
    REQUIRE_FALSE(garbage.error().empty());
}

TEST_CASE("hrfreq round-trips through toString and fromString", "[utils][hrfreq]") {
    const double values[] = { 0.0, 1234.0, 14074000.0, 145500000.0, 1.42e9 };
    for (double v : values) {
        INFO("value: " << v);
        REQUIRE(parseFrequency(hrfreq::toString(v)) == Approx(v).margin(1.0));
    }
}

TEST_CASE("hrfreq::fromString accepts an unknown unit by ignoring it", "[utils][hrfreq]") {
    // Characterization: an unrecognised scale character logs a warning but the
    // numeric part is still returned unscaled.
    REQUIRE(parseFrequency("100X") == Approx(100.0));
}
