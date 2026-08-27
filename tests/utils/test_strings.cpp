// utils/strings.h: splitting, joining, trimming and the percentile helpers used
// by the waterfall/noise-floor code.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <utils/strings.h>

using Catch::Approx;

TEST_CASE("splitStringV splits on any of the separator characters", "[utils][strings]") {
    std::vector<std::string> parts;

    splitStringV("a,b,c", ",", parts);
    REQUIRE(parts == std::vector<std::string>{ "a", "b", "c" });

    splitStringV("a,b;c", ",;", parts);
    REQUIRE(parts == std::vector<std::string>{ "a", "b", "c" });
}

TEST_CASE("splitStringV edge cases", "[utils][strings]") {
    std::vector<std::string> parts;

    SECTION("empty input produces nothing") {
        splitStringV("", ",", parts);
        REQUIRE(parts.empty());
    }

    SECTION("no separator yields the whole string") {
        splitStringV("abc", ",", parts);
        REQUIRE(parts == std::vector<std::string>{ "abc" });
    }

    SECTION("leading separator yields an empty first field") {
        splitStringV(",a", ",", parts);
        REQUIRE(parts == std::vector<std::string>{ "", "a" });
    }

    SECTION("trailing separator does not add an empty last field") {
        splitStringV("a,", ",", parts);
        REQUIRE(parts == std::vector<std::string>{ "a" });
    }

    SECTION("consecutive separators produce empty fields") {
        splitStringV("a,,b", ",", parts);
        REQUIRE(parts == std::vector<std::string>{ "a", "", "b" });
    }

    SECTION("a lone separator produces one empty field") {
        splitStringV(",", ",", parts);
        REQUIRE(parts == std::vector<std::string>{ "" });
    }
}

TEST_CASE("coalesceSplit drops empty fields", "[utils][strings]") {
    std::vector<std::string> parts = { "a", "", "b", "", "" };
    coalesceSplit(parts);
    REQUIRE(parts == std::vector<std::string>{ "a", "b" });

    std::vector<std::string> allEmpty = { "", "" };
    coalesceSplit(allEmpty);
    REQUIRE(allEmpty.empty());
}

TEST_CASE("joinStringV is the inverse of splitStringV for simple input", "[utils][strings]") {
    std::vector<std::string> parts;
    splitStringV("one/two/three", "/", parts);
    REQUIRE(joinStringV("/", parts) == "one/two/three");

    std::vector<std::string> single = { "only" };
    REQUIRE(joinStringV(",", single) == "only");

    std::vector<std::string> none;
    REQUIRE(joinStringV(",", none).empty());
}

TEST_CASE("removeSubstrings removes every occurrence", "[utils][strings]") {
    std::string s = "a-b-c-";
    removeSubstrings(s, "-");
    REQUIRE(s == "abc");

    std::string none = "abc";
    removeSubstrings(none, "x");
    REQUIRE(none == "abc");

    // Overlapping removals: "aaa" minus "aa" leaves one "a".
    std::string overlap = "aaa";
    removeSubstrings(overlap, "aa");
    REQUIRE(overlap == "a");
}

TEST_CASE("replaceSubstrings replaces every occurrence", "[utils][strings]") {
    std::string s = "a.b.c";
    replaceSubstrings(s, ".", "::");
    REQUIRE(s == "a::b::c");

    // The replacement is not rescanned, so this terminates.
    std::string grow = "xx";
    replaceSubstrings(grow, "x", "xy");
    REQUIRE(grow == "xyxy");
}

TEST_CASE("trimString removes leading and trailing whitespace", "[utils][strings]") {
    std::string s = "  hello \t\n";
    trimString(s);
    REQUIRE(s == "hello");

    std::string inner = " a b ";
    trimString(inner);
    REQUIRE(inner == "a b");

    std::string blank = "   ";
    trimString(blank);
    REQUIRE(blank.empty());

    std::string empty;
    trimString(empty);
    REQUIRE(empty.empty());
}

TEST_CASE("percentile returns an order statistic", "[utils][strings][percentile]") {
    std::vector<float> data;
    for (int i = 0; i < 100; i++) { data.push_back((float)i); }

    // The implementation indexes with (n-1)*p and uses a 1-based kth-smallest
    // selection, so p == 0.5 lands just above the median.
    REQUIRE(percentile::percentile(data, 0.0) == Approx(0.0f));

    std::vector<float> data2;
    for (int i = 0; i < 100; i++) { data2.push_back((float)i); }
    float mid = percentile::percentile(data2, 0.5);
    REQUIRE(mid >= 48.0f);
    REQUIRE(mid <= 51.0f);

    std::vector<float> data3;
    for (int i = 0; i < 100; i++) { data3.push_back((float)i); }
    float high = percentile::percentile(data3, 0.99);
    REQUIRE(high >= 97.0f);
}

TEST_CASE("percentile is order independent", "[utils][strings][percentile]") {
    std::vector<float> ascending, descending;
    for (int i = 0; i < 51; i++) { ascending.push_back((float)i); }
    for (int i = 50; i >= 0; i--) { descending.push_back((float)i); }

    REQUIRE(percentile::percentile(ascending, 0.25) == percentile::percentile(descending, 0.25));
}

TEST_CASE("percentile of an empty vector is zero", "[utils][strings][percentile]") {
    std::vector<float> empty;
    REQUIRE(percentile::percentile(empty, 0.5) == 0.0f);
    REQUIRE(percentile::percentile_sampling(empty, 0.5) == 0.0f);
}

TEST_CASE("percentile_sampling approximates percentile on large inputs", "[utils][strings][percentile]") {
    // It subsamples to 100 points once the input is large, so it only has to be
    // close, not exact.
    std::vector<float> data;
    for (int i = 0; i < 10000; i++) { data.push_back((float)i); }
    std::vector<float> copy = data;

    float sampled = percentile::percentile_sampling(data, 0.5);
    float exact = percentile::percentile(copy, 0.5);

    REQUIRE(std::fabs(sampled - exact) < 200.0f);
}

TEST_CASE("kthSmallest selects the requested order statistic", "[utils][strings][percentile]") {
    std::vector<int> data = { 7, 1, 5, 3, 9, 2 };
    REQUIRE(percentile::kthSmallest(data.data(), 0, (int)data.size() - 1, 1) == 1);

    std::vector<int> data2 = { 7, 1, 5, 3, 9, 2 };
    REQUIRE(percentile::kthSmallest(data2.data(), 0, (int)data2.size() - 1, 6) == 9);

    std::vector<int> data3 = { 7, 1, 5, 3, 9, 2 };
    REQUIRE(percentile::kthSmallest(data3.data(), 0, (int)data3.size() - 1, 3) == 3);
}
