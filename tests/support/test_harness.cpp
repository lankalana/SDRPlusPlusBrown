#include <catch2/catch_test_macros.hpp>

#include <fstream>

#include "support/tmp_dir.h"

TEST_CASE("test harness scratch directory is usable", "[harness]") {
    sdrpp_test::ScopedTmpFile file("harness");
    REQUIRE_FALSE(file.exists());
    {
        std::ofstream out(file.path(), std::ios::binary);
        REQUIRE(out.good());
        out << "ok";
    }
    REQUIRE(file.exists());
    REQUIRE(file.size() == 2);
}
