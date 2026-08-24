// utils/cty.h: Maidenhead locator conversion, great-circle bearing/distance and
// the callsign prefix lookup.
//
// loadAllCty() reads the cty.dat files out of the resources directory and needs
// a configured core, so these tests build a small CTY table by hand instead.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include <utils/cty.h>

using Catch::Approx;
using utils::BearingDistance;
using utils::CTY;
using utils::LatLng;

namespace {
    constexpr double PI = 3.14159265358979323846;
    double toDeg(double rad) { return rad * 180.0 / PI; }
}

// ------------------------------------------------------------------ LatLng

TEST_CASE("LatLng::isValid rejects out of range coordinates", "[utils][cty]") {
    REQUIRE(LatLng{ 0.0, 0.0 }.isValid());
    REQUIRE(LatLng{ 90.0, 180.0 }.isValid());
    REQUIRE(LatLng{ -90.0, -180.0 }.isValid());

    REQUIRE_FALSE(LatLng{ 91.0, 0.0 }.isValid());
    REQUIRE_FALSE(LatLng{ 0.0, 181.0 }.isValid());
    REQUIRE_FALSE(LatLng::invalid().isValid());
}

// ------------------------------------------------------------ gridToLatLng

TEST_CASE("gridToLatLng converts a six character locator", "[utils][cty][grid]") {
    // JN58td is the canonical Maidenhead example (Munich): the centre of the
    // subsquare is 48 deg 08' 45" N, 11 deg 37' 30" E.
    auto ll = utils::gridToLatLng("JN58td");
    REQUIRE(ll.isValid());
    REQUIRE(ll.lat == Approx(48.1458).margin(0.01));
    REQUIRE(ll.lon == Approx(11.625).margin(0.01));
}

TEST_CASE("gridToLatLng handles a four character locator", "[utils][cty][grid]") {
    // A four character locator is padded to the centre of the square.
    auto four = utils::gridToLatLng("JN58");
    auto six = utils::gridToLatLng("JN58ll");
    REQUIRE(four.isValid());
    REQUIRE(four.lat == Approx(six.lat));
    REQUIRE(four.lon == Approx(six.lon));
}

TEST_CASE("gridToLatLng truncates longer locators", "[utils][cty][grid]") {
    auto six = utils::gridToLatLng("KP20le");
    auto eight = utils::gridToLatLng("KP20le12");
    REQUIRE(eight.lat == Approx(six.lat));
    REQUIRE(eight.lon == Approx(six.lon));
}

TEST_CASE("gridToLatLng covers the corners of the grid", "[utils][cty][grid]") {
    auto aa = utils::gridToLatLng("AA00aa");
    REQUIRE(aa.isValid());
    REQUIRE(aa.lat == Approx(-90.0).margin(0.1));
    REQUIRE(aa.lon == Approx(-180.0).margin(0.2));

    auto rr = utils::gridToLatLng("RR99xx");
    REQUIRE(rr.isValid());
    REQUIRE(rr.lat == Approx(90.0).margin(0.1));
    REQUIRE(rr.lon == Approx(180.0).margin(0.2));
}

TEST_CASE("gridToLatLng returns the invalid sentinel for bad input", "[utils][cty][grid]") {
    REQUIRE_FALSE(utils::gridToLatLng("").isValid());
    REQUIRE_FALSE(utils::gridToLatLng("XX").isValid());
    REQUIRE_FALSE(utils::gridToLatLng("ZZ99zz").isValid()); // field letter out of range
    REQUIRE_FALSE(utils::gridToLatLng("JN5").isValid());
    REQUIRE_FALSE(utils::gridToLatLng("J158td").isValid()); // digit where a letter belongs
}

// --------------------------------------------------------- bearingDistance

TEST_CASE("bearingDistance is zero between identical points", "[utils][cty][bearing]") {
    LatLng p{ 60.17, 24.94 };
    auto bd = utils::bearingDistance(p, p);
    REQUIRE(bd.distance == Approx(0.0).margin(1e-6));
}

TEST_CASE("bearingDistance points due north and due east", "[utils][cty][bearing]") {
    LatLng origin{ 0.0, 0.0 };

    auto north = utils::bearingDistance(origin, LatLng{ 10.0, 0.0 });
    REQUIRE(toDeg(north.bearing) == Approx(0.0).margin(0.01));

    auto east = utils::bearingDistance(origin, LatLng{ 0.0, 10.0 });
    REQUIRE(toDeg(east.bearing) == Approx(90.0).margin(0.01));

    auto south = utils::bearingDistance(origin, LatLng{ -10.0, 0.0 });
    REQUIRE(toDeg(south.bearing) == Approx(180.0).margin(0.01));

    auto west = utils::bearingDistance(origin, LatLng{ 0.0, -10.0 });
    REQUIRE(toDeg(west.bearing) == Approx(270.0).margin(0.01));
}

TEST_CASE("bearingDistance matches a known great-circle distance", "[utils][cty][bearing]") {
    // One degree of latitude is about 111.2 km on a 6371 km sphere.
    auto bd = utils::bearingDistance(LatLng{ 0.0, 0.0 }, LatLng{ 1.0, 0.0 });
    REQUIRE(bd.distance == Approx(111.19).margin(0.5));

    // Quarter of the way around the equator.
    auto quarter = utils::bearingDistance(LatLng{ 0.0, 0.0 }, LatLng{ 0.0, 90.0 });
    REQUIRE(quarter.distance == Approx(6371.0 * PI / 2.0).margin(1.0));

    // Helsinki to Tokyo, roughly 7800 km.
    auto hel = LatLng{ 60.17, 24.94 };
    auto nrt = LatLng{ 35.68, 139.69 };
    auto path = utils::bearingDistance(hel, nrt);
    REQUIRE(path.distance == Approx(7800.0).margin(150.0));
}

TEST_CASE("bearingDistance is symmetric in distance", "[utils][cty][bearing]") {
    LatLng a{ 51.5, -0.13 };
    LatLng b{ -33.87, 151.21 };
    REQUIRE(utils::bearingDistance(a, b).distance ==
            Approx(utils::bearingDistance(b, a).distance).margin(1e-6));
}

TEST_CASE("bearingDistance always returns a bearing in [0, 2pi)", "[utils][cty][bearing]") {
    LatLng origin{ 10.0, 20.0 };
    for (double lat = -80.0; lat <= 80.0; lat += 20.0) {
        for (double lon = -170.0; lon <= 170.0; lon += 40.0) {
            INFO("target " << lat << "," << lon);
            auto bd = utils::bearingDistance(origin, LatLng{ lat, lon });
            REQUIRE(bd.bearing >= 0.0);
            REQUIRE(bd.bearing < 2.0 * PI + 1e-9);
            REQUIRE(bd.distance >= 0.0);
            REQUIRE(bd.distance <= 6371.0 * PI + 1.0);
        }
    }
}

// ------------------------------------------------------------ findCallsign

namespace {
    CTY buildTable() {
        CTY cty;

        CTY::DXCC finland;
        finland.name = "Finland";
        finland.continent = "EU";
        finland.ll = { 61.0, 25.0 };
        finland.prefixes.push_back(CTY::Callsign{ false, {}, "", "OH", "" });
        finland.prefixes.push_back(CTY::Callsign{ false, {}, "", "OH0", "" });
        finland.prefixes.push_back(CTY::Callsign{ true, {}, "", "OH2ABC", "" });
        cty.dxcc.push_back(finland);

        CTY::DXCC sweden;
        sweden.name = "Sweden";
        sweden.continent = "EU";
        sweden.ll = { 59.0, 18.0 };
        sweden.prefixes.push_back(CTY::Callsign{ false, {}, "", "SM", "" });
        cty.dxcc.push_back(sweden);

        return cty;
    }
}

TEST_CASE("findCallsign matches by prefix", "[utils][cty][callsign]") {
    auto cty = buildTable();

    auto sm = cty.findCallsign("SM5XYZ");
    REQUIRE(sm.value == "SM");
    REQUIRE(sm.dxccname == "Sweden");
    REQUIRE(sm.continent == "EU");
    REQUIRE(sm.ll.lat == Approx(59.0));
}

TEST_CASE("findCallsign prefers the longest prefix", "[utils][cty][callsign]") {
    // OH0 must beat OH for an Aland callsign, even though both match.
    auto cty = buildTable();
    auto oh0 = cty.findCallsign("OH0XX");
    REQUIRE(oh0.value == "OH0");
}

TEST_CASE("findCallsign prefers an exact match over any prefix", "[utils][cty][callsign]") {
    auto cty = buildTable();
    auto exact = cty.findCallsign("OH2ABC");
    REQUIRE(exact.exact);
    REQUIRE(exact.value == "OH2ABC");
    REQUIRE(exact.dxccname == "Finland");
}

TEST_CASE("findCallsign returns an empty result for an unknown prefix",
          "[utils][cty][callsign]") {
    auto cty = buildTable();
    auto unknown = cty.findCallsign("ZZ9ZZZ");
    REQUIRE(unknown.value.empty());
    REQUIRE(unknown.dxccname.empty());
}

TEST_CASE("findCallsign on an empty table is harmless", "[utils][cty][callsign]") {
    CTY empty;
    auto r = empty.findCallsign("OH2ABC");
    REQUIRE(r.value.empty());
}
