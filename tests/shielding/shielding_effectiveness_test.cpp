// tests/shielding/shielding_effectiveness_test.cpp
#include <catch2/catch_approx.hpp>   // Catch::Approx (float compare)
//
// Catch2 v3 tests for the emc::shielding Near-Field and Plane-Wave SE calculators
// (include/emc/shielding/shielding_effectiveness.hpp). All expected values are
// hand-computed inline from the closed forms in the implementation guide:
//
//   skin depth   delta = 1 / sqrt(| pi^2 * 4e-7 * mu_r * sigma * f |)
//   absorption   AL    = 8.7 * t / delta                               [dB]
//   Ns           Ns    = sqrt(2 * pi^2 * 4e-7 * mu_r * f / sigma)
//   reflection   RL    = 20 * log10( Zw / (4 * Ns) )                    [dB]
//                Zw    = 1/(2*pi*f*eps0*r)  (Electric, near field)
//                Zw    = 2*pi*f*mu0*r       (Magnetic, near field)
//                Zw    = 377                (Plane wave, far field)
//   total SE           = AL + RL                                        [dB]
//
// Reference point (copper, sigma = 5.80e7 S/m, mu_r = 1, t = 1 mm, f = 1 MHz):
//   delta = 6.608549e-5 m   AL = 131.647652 dB
//   Ns    = 3.689613e-4     RL(plane,377) = 108.146010 dB   SE = 239.793662 dB
//
// emc::units::Decibel exposes a plain double `.value`, so dB outputs are compared
// with Catch::Approx; the mp-units quantity helper emc::test::approx is used for
// genuine quantities (none needed here, but the header is included for parity).

#include <catch2/catch_test_macros.hpp>

#include <cmath>   // std::log10, std::sqrt, std::abs

#include <emc/shielding/shielding_effectiveness.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, MHz, S, m, mm, ...

using emc::shielding::NearFieldSeInput;
using emc::shielding::PlaneWaveSeInput;
using emc::shielding::FieldType;

namespace {
// Conductivity literal reused across cases: copper at 5.80e7 S/m.
constexpr auto kCopperSigma = 5.80e7 * (S / m);
} // namespace

// ---------------------------------------------------------------------------
// (a) KNOWN VALUE — Plane-wave SE for a 1 mm copper sheet at 1 MHz.
//     Hand-computed: AL = 131.647652 dB, RL = 108.146010 dB, SE = 239.793662 dB.
// ---------------------------------------------------------------------------
TEST_CASE("plane-wave SE known value (copper, 1 mm, 1 MHz)", "[shielding][plane_wave]") {
    auto r = emc::shielding::calculate(PlaneWaveSeInput{
        .conductivity = kCopperSigma,
        .relative_permeability = 1.0,
        .thickness = 1.0 * mm,
        .frequency = 1.0 * MHz,
    });
    REQUIRE(r.has_value());
    REQUIRE(r->absorption_loss.value == Catch::Approx(131.647652).epsilon(1e-6));
    REQUIRE(r->reflection_loss.value == Catch::Approx(108.146010).epsilon(1e-6));
    REQUIRE(r->shielding.value       == Catch::Approx(239.793662).epsilon(1e-6));
    // Structural invariant: SE == AL + RL.
    REQUIRE(r->shielding.value ==
            Catch::Approx(r->absorption_loss.value + r->reflection_loss.value).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// (a') KNOWN VALUE — Near-field Electric branch, same sheet, distance r = 1 mm.
//      Zw(E) = 1/(2*pi*f*eps0*r) = 1.7975104e7 ohm,
//      RL = 20*log10(Zw/(4*Ns)) = 201.712611 dB, AL unchanged at 131.647652 dB.
// ---------------------------------------------------------------------------
TEST_CASE("near-field E-branch known value (copper, 1 mm, 1 MHz, r = 1 mm)",
          "[shielding][near_field]") {
    auto r = emc::shielding::calculate(NearFieldSeInput{
        .conductivity = kCopperSigma,
        .relative_permeability = 1.0,
        .thickness = 1.0 * mm,
        .distance  = 1.0 * mm,
        .frequency = 1.0 * MHz,
        .field     = FieldType::Electric,
    });
    REQUIRE(r.has_value());
    REQUIRE(r->absorption_loss.value == Catch::Approx(131.647652).epsilon(1e-6));
    REQUIRE(r->reflection_loss.value == Catch::Approx(201.712611).epsilon(1e-6));
    REQUIRE(r->shielding.value       == Catch::Approx(333.360263).epsilon(1e-6));
}

// ---------------------------------------------------------------------------
// (b) PROPERTY — cross-calculator agreement on shared physics.
//     Plane-Wave and Near-Field share the SAME skin-depth/Ns closed form routed
//     through emc::constants::pi, so the ABSORPTION loss (independent of the
//     field branch and of the 377-ohm impedance) must match bit-for-bit.
// ---------------------------------------------------------------------------
TEST_CASE("plane-wave and near-field absorption agree", "[shielding][property]") {
    auto pw = emc::shielding::calculate(PlaneWaveSeInput{
        .conductivity = kCopperSigma, .thickness = 2.0 * mm, .frequency = 5.0 * MHz});
    auto nf = emc::shielding::calculate(NearFieldSeInput{
        .conductivity = kCopperSigma, .thickness = 2.0 * mm, .distance = 1.0 * m,
        .frequency = 5.0 * MHz, .field = FieldType::Electric});
    REQUIRE(pw.has_value());
    REQUIRE(nf.has_value());
    REQUIRE(pw->absorption_loss.value ==
            Catch::Approx(nf->absorption_loss.value).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// (b') PROPERTY — absorption loss is linear in thickness.
//      AL = 8.7 * t / delta, so doubling t (everything else fixed) doubles AL.
// ---------------------------------------------------------------------------
TEST_CASE("absorption loss is linear in thickness", "[shielding][property]") {
    auto a = emc::shielding::calculate(PlaneWaveSeInput{
        .conductivity = kCopperSigma, .thickness = 1.0 * mm, .frequency = 1.0 * MHz});
    auto b = emc::shielding::calculate(PlaneWaveSeInput{
        .conductivity = kCopperSigma, .thickness = 2.0 * mm, .frequency = 1.0 * MHz});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(b->absorption_loss.value ==
            Catch::Approx(2.0 * a->absorption_loss.value).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// (b'') PROPERTY — near-field E branch reflects more than H branch.
//       The high-impedance (E) wave impedance exceeds the low-impedance (H) one
//       at the same geometry/frequency, so RL(E) > RL(H).
// ---------------------------------------------------------------------------
TEST_CASE("near-field E-branch reflects more than H-branch", "[shielding][property]") {
    auto e = emc::shielding::calculate(NearFieldSeInput{
        .conductivity = kCopperSigma, .thickness = 1.0 * mm, .distance = 0.1 * m,
        .frequency = 1.0 * MHz, .field = FieldType::Electric});
    auto h = emc::shielding::calculate(NearFieldSeInput{
        .conductivity = kCopperSigma, .thickness = 1.0 * mm, .distance = 0.1 * m,
        .frequency = 1.0 * MHz, .field = FieldType::Magnetic});
    REQUIRE(e.has_value());
    REQUIRE(h.has_value());
    REQUIRE(e->reflection_loss.value > h->reflection_loss.value);
}

// ---------------------------------------------------------------------------
// (c) VALIDATION / EDGE — non-positive inputs short-circuit to OutOfRange,
//     never reaching sqrt/log10 with a bad argument.
// ---------------------------------------------------------------------------
TEST_CASE("plane-wave SE rejects zero thickness", "[shielding][plane_wave][validation]") {
    auto r = emc::shielding::calculate(PlaneWaveSeInput{
        .conductivity = kCopperSigma, .thickness = 0.0 * mm, .frequency = 1.0 * MHz});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "thickness");
}

TEST_CASE("near-field SE rejects zero frequency", "[shielding][near_field][validation]") {
    auto r = emc::shielding::calculate(NearFieldSeInput{
        .conductivity = kCopperSigma, .thickness = 1.0 * mm, .distance = 1.0 * mm,
        .frequency = 0.0 * Hz, .field = FieldType::Electric});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "frequency");
}

// ---------------------------------------------------------------------------
// (c') VALIDATION — unknown material via the convenience overload short-circuits
//      to UnknownMaterial (the std::expected and_then material channel).
// ---------------------------------------------------------------------------
TEST_CASE("plane-wave SE rejects Custom material", "[shielding][plane_wave][validation]") {
    auto r = emc::shielding::plane_wave_se(emc::materials::Material::Custom, 1.0,
                                           1.0 * mm, 1.0 * MHz);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::UnknownMaterial);
}
