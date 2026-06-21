// tests/shielding/aperture_test.cpp
#include <catch2/catch_approx.hpp>   // Catch::Approx (float compare)
//
// Catch2 v3 tests for emc::shielding aperture absorption (cutoff) loss.
//   (a) hand-computed KNOWN-VALUE tests for slot and round geometries,
//   (b) PROPERTY: unit-independence (same physical slot in mm gives the same AL)
//       plus linearity of AL in depth,
//   (c) VALIDATION / EDGE: zero width (slot) / zero diameter (round) -> OutOfRange.
//
// Closed form (constants presuppose INCHES):
//   slot  (rectangular):  AL = 27.3 * depth / width
//   round (circular):     AL = 32   * depth / diameter
//
// AL is an emc::units::Decibel (a plain { double value; } log wrapper, NOT an
// mp-units linear quantity), so the dB output is compared via its `.value`
// against a hand-computed double, not emc::test::approx.
#include <catch2/catch_test_macros.hpp>

#include <emc/shielding/aperture.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/international.h>   // international::inch
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;        // mm, m, ...
using mp_units::international::unit_symbols::in;    // inch

using emc::ErrorCode;
using emc::shielding::ApertureInput;
using emc::shielding::ApertureShape;

// (a) KNOWN VALUE — slot.  d = 1 in, w = 0.5 in -> AL = 27.3 * 1 / 0.5 = 54.6 dB.
TEST_CASE("aperture slot known value", "[shielding][aperture]") {
    auto r = emc::shielding::calculate(ApertureInput{
        .depth = 1.0 * in,
        .width = 0.5 * in,
        .shape = ApertureShape::Slot,
    });
    REQUIRE(r.has_value());
    REQUIRE(r->absorption_loss.value == Catch::Approx(54.6).epsilon(1e-9));
}

// (a) KNOWN VALUE — round.  d = 0.5 in, D = 0.1 in -> AL = 32 * 0.5 / 0.1 = 160.0 dB.
TEST_CASE("aperture round known value", "[shielding][aperture]") {
    auto r = emc::shielding::calculate(ApertureInput{
        .depth    = 0.5 * in,
        .diameter = 0.1 * in,
        .shape    = ApertureShape::Round,
    });
    REQUIRE(r.has_value());
    REQUIRE(r->absorption_loss.value == Catch::Approx(160.0).epsilon(1e-9));
}

// (a) KNOWN VALUE — the guide's worked example: 0.5-in-deep, 0.1-in-wide slot.
//     AL = 27.3 * 0.5 / 0.1 = 136.5 dB.
TEST_CASE("aperture slot example value", "[shielding][aperture]") {
    auto r = emc::shielding::calculate(ApertureInput{
        .depth = 0.5 * in,
        .width = 0.1 * in,
        .shape = ApertureShape::Slot,
    });
    REQUIRE(r.has_value());
    REQUIRE(r->absorption_loss.value == Catch::Approx(136.5).epsilon(1e-9));
}

// (b) PROPERTY — unit independence: the SAME physical slot (1 in deep, 0.5 in wide)
//     expressed entirely in millimetres must yield the SAME AL. 1 in == 25.4 mm.
TEST_CASE("aperture is unit-independent", "[shielding][aperture][property]") {
    auto a = emc::shielding::calculate(ApertureInput{
        .depth = 1.0 * in, .width = 0.5 * in, .shape = ApertureShape::Slot});
    auto b = emc::shielding::calculate(ApertureInput{
        .depth = 25.4 * mm, .width = 12.7 * mm, .shape = ApertureShape::Slot});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->absorption_loss.value ==
            Catch::Approx(b->absorption_loss.value).epsilon(1e-9));
}

// (b) PROPERTY — linearity in depth: AL is proportional to the aperture depth,
//     so doubling d (slot geometry fixed) exactly doubles the absorption loss.
TEST_CASE("aperture loss is linear in depth", "[shielding][aperture][property]") {
    auto single = emc::shielding::calculate(ApertureInput{
        .depth = 0.5 * in, .width = 0.1 * in, .shape = ApertureShape::Slot});
    auto twice  = emc::shielding::calculate(ApertureInput{
        .depth = 1.0 * in, .width = 0.1 * in, .shape = ApertureShape::Slot});
    REQUIRE(single.has_value());
    REQUIRE(twice.has_value());
    REQUIRE(twice->absorption_loss.value ==
            Catch::Approx(2.0 * single->absorption_loss.value).epsilon(1e-9));
}

// (c) VALIDATION / EDGE — zero slot width -> OutOfRange (from require_positive),
//     reported on field "width"; never produces inf.
TEST_CASE("aperture rejects zero width", "[shielding][aperture][validation]") {
    auto r = emc::shielding::calculate(ApertureInput{
        .depth = 1.0 * in, .width = 0.0 * in, .shape = ApertureShape::Slot});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "width");
}

// (c) VALIDATION / EDGE — zero round diameter -> OutOfRange on field "diameter".
TEST_CASE("aperture rejects zero diameter (round)", "[shielding][aperture][validation]") {
    auto r = emc::shielding::calculate(ApertureInput{
        .depth = 1.0 * in, .diameter = 0.0 * in, .shape = ApertureShape::Round});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "diameter");
}

// (c) VALIDATION / EDGE — non-positive depth -> OutOfRange on field "depth".
TEST_CASE("aperture rejects zero depth", "[shielding][aperture][validation]") {
    auto r = emc::shielding::calculate(ApertureInput{
        .depth = 0.0 * in, .width = 0.5 * in, .shape = ApertureShape::Slot});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "depth");
}
