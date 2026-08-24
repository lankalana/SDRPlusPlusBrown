// utils/optionlist.h: the key/name/value triple list behind every combo box in
// the UI. Its `txt` member is a NUL-separated blob handed straight to ImGui, so
// the string packing is part of the contract.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <utils/optionlist.h>

namespace {
    // Reads back the NUL separated blob that `txt` points at.
    std::vector<std::string> unpack(const char* txt, int count) {
        std::vector<std::string> out;
        const char* p = txt;
        for (int i = 0; i < count; i++) {
            out.emplace_back(p);
            p += out.back().size() + 1;
        }
        return out;
    }
}

TEST_CASE("OptionList starts empty", "[utils][optionlist]") {
    OptionList<std::string, int> list;
    REQUIRE(list.size() == 0);
    REQUIRE(list.empty());
    REQUIRE(list.txt != nullptr);
    REQUIRE(std::string(list.txt).empty());
}

TEST_CASE("OptionList define stores a key, name and value", "[utils][optionlist]") {
    OptionList<std::string, int> list;
    list.define("a", "Alpha", 1);
    list.define("b", "Beta", 2);

    REQUIRE(list.size() == 2);
    REQUIRE_FALSE(list.empty());
    REQUIRE(list.key(0) == "a");
    REQUIRE(list.name(0) == "Alpha");
    REQUIRE(list.value(0) == 1);
    REQUIRE(list.value(1) == 2);
}

TEST_CASE("OptionList two-argument define uses the name as the key", "[utils][optionlist]") {
    OptionList<std::string, int> list;
    list.define("Alpha", 1);
    REQUIRE(list.key(0) == "Alpha");
    REQUIRE(list.name(0) == "Alpha");
}

TEST_CASE("OptionList rejects duplicate keys, names and values", "[utils][optionlist]") {
    OptionList<std::string, int> list;
    list.define("a", "Alpha", 1);

    REQUIRE_THROWS_AS(list.define("a", "Other", 2), std::runtime_error);
    REQUIRE_THROWS_AS(list.define("b", "Alpha", 2), std::runtime_error);
    REQUIRE_THROWS_AS(list.define("b", "Other", 1), std::runtime_error);
    REQUIRE(list.size() == 1);
}

TEST_CASE("OptionList lookups by key, name and value", "[utils][optionlist]") {
    OptionList<int, std::string> list;
    list.define(10, "Ten", "ten");
    list.define(20, "Twenty", "twenty");

    REQUIRE(list.keyExists(10));
    REQUIRE_FALSE(list.keyExists(30));
    REQUIRE(list.nameExists("Twenty"));
    REQUIRE(list.valueExists("ten"));

    REQUIRE(list.keyId(20) == 1);
    REQUIRE(list.nameId("Ten") == 0);
    REQUIRE(list.valueId("twenty") == 1);
}

TEST_CASE("OptionList lookups throw when the entry is missing", "[utils][optionlist]") {
    OptionList<int, int> list;
    list.define(1, "one", 100);

    REQUIRE_THROWS_AS(list.keyId(2), std::runtime_error);
    REQUIRE_THROWS_AS(list.nameId("two"), std::runtime_error);
    REQUIRE_THROWS_AS(list.valueId(200), std::runtime_error);
}

TEST_CASE("OptionList undefine removes by index", "[utils][optionlist]") {
    OptionList<std::string, int> list;
    list.define("a", "Alpha", 1);
    list.define("b", "Beta", 2);
    list.define("c", "Gamma", 3);

    list.undefine(1);
    REQUIRE(list.size() == 2);
    REQUIRE(list.key(0) == "a");
    REQUIRE(list.key(1) == "c");
    REQUIRE_FALSE(list.nameExists("Beta"));
}

TEST_CASE("OptionList undefine by key, name and value", "[utils][optionlist]") {
    OptionList<std::string, int> list;
    list.define("a", "Alpha", 1);
    list.define("b", "Beta", 2);
    list.define("c", "Gamma", 3);

    list.undefineKey("a");
    REQUIRE(list.size() == 2);
    list.undefineName("Beta");
    REQUIRE(list.size() == 1);
    list.undefineValue(3);
    REQUIRE(list.size() == 0);
    REQUIRE(list.empty());
}

TEST_CASE("OptionList clear empties everything including the text blob", "[utils][optionlist]") {
    OptionList<std::string, int> list;
    list.define("a", "Alpha", 1);
    list.clear();

    REQUIRE(list.empty());
    REQUIRE(std::string(list.txt).empty());
}

TEST_CASE("OptionList txt is a NUL separated list of names", "[utils][optionlist]") {
    OptionList<std::string, int> list;
    list.define("a", "Alpha", 1);
    list.define("b", "Beta", 2);
    list.define("c", "Gamma", 3);

    auto names = unpack(list.txt, 3);
    REQUIRE(names == std::vector<std::string>{ "Alpha", "Beta", "Gamma" });
}

TEST_CASE("OptionList txt is rebuilt after every mutation", "[utils][optionlist]") {
    // The pointer is into a std::string member, so it can move; anything that
    // caches it across a mutation is holding a dangling pointer.
    OptionList<std::string, int> list;
    list.define("a", "Alpha", 1);
    list.define("b", "Beta", 2);
    list.undefineKey("a");

    auto names = unpack(list.txt, 1);
    REQUIRE(names == std::vector<std::string>{ "Beta" });

    list.define("c", "Gamma", 3);
    names = unpack(list.txt, 2);
    REQUIRE(names == std::vector<std::string>{ "Beta", "Gamma" });
}

TEST_CASE("OptionList works with a double value type", "[utils][optionlist]") {
    OptionList<std::string, double> list;
    list.define("48k", "48000 Hz", 48000.0);
    list.define("96k", "96000 Hz", 96000.0);

    REQUIRE(list.valueId(96000.0) == 1);
    REQUIRE(list.value(0) == 48000.0);
}
