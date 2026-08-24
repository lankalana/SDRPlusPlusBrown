// Human readable frequency parsing and formatting.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <utils/hrfreq.h>

using Catch::Approx;

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
    double freq = 0.0;

    REQUIRE(hrfreq::fromString("14.074MHz", freq));
    REQUIRE(freq == Approx(14074000.0));

    REQUIRE(hrfreq::fromString("7k", freq));
    REQUIRE(freq == Approx(7000.0));

    REQUIRE(hrfreq::fromString("1G", freq));
    REQUIRE(freq == Approx(1e9));

    REQUIRE(hrfreq::fromString("500Hz", freq));
    REQUIRE(freq == Approx(500.0));
}

TEST_CASE("hrfreq::fromString is case insensitive on the unit", "[utils][hrfreq]") {
    double a = 0.0, b = 0.0;
    REQUIRE(hrfreq::fromString("14.074mhz", a));
    REQUIRE(hrfreq::fromString("14.074MHZ", b));
    REQUIRE(a == Approx(b));
    REQUIRE(a == Approx(14074000.0));
}

TEST_CASE("hrfreq::fromString without a unit assumes Hz", "[utils][hrfreq]") {
    double freq = 0.0;
    REQUIRE(hrfreq::fromString("1234", freq));
    REQUIRE(freq == Approx(1234.0));
}

TEST_CASE("hrfreq::fromString ignores thousands separators", "[utils][hrfreq]") {
    double freq = 0.0;
    REQUIRE(hrfreq::fromString("14,074,000Hz", freq));
    REQUIRE(freq == Approx(14074000.0));
}

TEST_CASE("hrfreq::fromString skips leading junk", "[utils][hrfreq]") {
    double freq = 0.0;
    REQUIRE(hrfreq::fromString("freq: 145.500MHz", freq));
    REQUIRE(freq == Approx(145500000.0));
}

TEST_CASE("hrfreq::fromString handles negative values", "[utils][hrfreq]") {
    double freq = 0.0;
    REQUIRE(hrfreq::fromString("-2.5kHz", freq));
    REQUIRE(freq == Approx(-2500.0));
}

TEST_CASE("hrfreq::fromString reports failure on garbage", "[utils][hrfreq]") {
    double freq = 12345.0;
    REQUIRE_FALSE(hrfreq::fromString("", freq));
    REQUIRE_FALSE(hrfreq::fromString("abc", freq));
    REQUIRE(freq == 12345.0); // untouched on failure
}

TEST_CASE("hrfreq round-trips through toString and fromString", "[utils][hrfreq]") {
    const double values[] = { 0.0, 1234.0, 14074000.0, 145500000.0, 1.42e9 };
    for (double v : values) {
        INFO("value: " << v);
        double parsed = 0.0;
        REQUIRE(hrfreq::fromString(hrfreq::toString(v), parsed));
        REQUIRE(parsed == Approx(v).margin(1.0));
    }
}

TEST_CASE("hrfreq::fromString accepts an unknown unit by ignoring it", "[utils][hrfreq]") {
    // Characterization: an unrecognised scale character logs a warning but the
    // numeric part is still returned unscaled.
    double freq = 0.0;
    REQUIRE(hrfreq::fromString("100X", freq));
    REQUIRE(freq == Approx(100.0));
}
