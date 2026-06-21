// tests/grounding/microstrip_current_test.cpp
//
// Catch2 v3 tests for emc::grounding::microstrip_current_distribution.
//
// Ground-plane return-current density follows the Lorentzian (Cauchy) profile
//     J(x) = (I0 / (pi*w)) * 1 / (1 + (x/h)^2)        [A/m]
// Expected values are hand-computed against this textbook closed form (no CSV).
#include <catch2/catch_test_macros.hpp>

#include <cmath>     // std::pow for the independent SI reference computation

#include <mp-units/systems/si.h>

#include <emc/grounding/microstrip_current.hpp>
#include <emc/core/constants.hpp>

#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // A, m, mm, mA, ...

using emc::ErrorCode;
using emc::grounding::MicrostripCurrentInput;
using emc::grounding::microstrip_current_distribution;

namespace {
// Independent SI oracle: re-derives J from raw SI numbers WITHOUT calling the
// library, so it is a true cross-check of the unit-mapping + formula path.
[[nodiscard]] double j_reference_A_per_m(double i0_A, double x_m, double h_m, double w_m) {
    const double ratio = x_m / h_m;
    return (i0_A / (emc::constants::pi * w_m)) * (1.0 / (1.0 + ratio * ratio));
}
} // namespace

// (a) KNOWN-VALUE — one fully hand-computed literal so a wrong mA/mm unit factor
//     or a dropped pi is caught independently of the oracle.
//       I0 = 5 mA, x = 25.6 mm, h = 57.4 mm, w = 64.9 mm
//         ratio = 25.6 / 57.4         = 0.445993031...
//         shape = 1 / (1 + ratio^2)   = 0.834091116...
//         peak  = 5e-3 / (pi*0.0649)  = 0.024523104... A/m
//         J     = peak * shape        = 0.020454502948 A/m
TEST_CASE("microstrip current: hand-computed known value", "[grounding]") {
    const auto out = microstrip_current_distribution({
        .source_current = 5.0  * mA,
        .trace_width    = 64.9 * mm,
        .height         = 57.4 * mm,
        .position       = 25.6 * mm,
    });
    REQUIRE(out.has_value());
    REQUIRE(emc::test::approx(out->current_density, 0.020454502948433106 * (A / m), 1e-9));
}

// (a') A second known value cross-checked against the independent SI oracle,
//      exercising the full quantity round-trip with different magnitudes.
TEST_CASE("microstrip current: oracle cross-check", "[grounding]") {
    const MicrostripCurrentInput in{
        .source_current = 1.0 * mA,
        .trace_width    = 2.8 * mm,
        .height         = 1.6 * mm,
        .position       = 2.0 * mm,
    };
    const auto out = microstrip_current_distribution(in);
    REQUIRE(out.has_value());

    const double expected = j_reference_A_per_m(1.0e-3, 2.0e-3, 1.6e-3, 2.8e-3);
    REQUIRE(emc::test::approx(out->current_density, expected * (A / m), 1e-9));
}

// (b) PROPERTIES — formula-independent physical invariants of the Lorentzian:
//     even symmetry J(+x)==J(-x), peak at x=0, half-power at x=h, and linearity in I0.
TEST_CASE("microstrip current: physical properties", "[grounding][property]") {
    const auto base = MicrostripCurrentInput{
        .source_current = 10.0 * mA,
        .trace_width    = 3.0  * mm,
        .height         = 1.6  * mm,
        .position       = 0.0  * mm,
    };

    SECTION("peak is at x = 0 and equals I0/(pi*w), independent of h") {
        const auto at0 = microstrip_current_distribution(base);
        REQUIRE(at0.has_value());
        const double peak = 10.0e-3 / (emc::constants::pi * 3.0e-3);  // 1.0610329539... A/m
        REQUIRE(emc::test::approx(at0->current_density, peak * (A / m), 1e-9));
    }

    SECTION("even symmetry: J(+x) == J(-x)") {
        auto plus  = base; plus.position  =  4.0 * mm;
        auto minus = base; minus.position = -4.0 * mm;
        const auto jp = microstrip_current_distribution(plus);
        const auto jm = microstrip_current_distribution(minus);
        REQUIRE(jp.has_value());
        REQUIRE(jm.has_value());
        REQUIRE(emc::test::approx(jp->current_density, jm->current_density, 1e-12));
    }

    SECTION("half-power at x = h: J(h)/J(0) == 1/2") {
        auto at_h = base; at_h.position = at_h.height;   // x == h -> shape = 1/(1+1) = 1/2
        const auto j0 = microstrip_current_distribution(base);
        const auto jh = microstrip_current_distribution(at_h);
        REQUIRE(j0.has_value());
        REQUIRE(jh.has_value());
        const double ratio =
            jh->current_density.numerical_value_in(A / m) /
            j0->current_density.numerical_value_in(A / m);
        REQUIRE(std::abs(ratio - 0.5) <= 1e-9);
    }

    SECTION("monotonic decay away from the trace") {
        auto near = base; near.position = 1.0 * mm;
        auto far  = base; far.position  = 8.0 * mm;
        const auto jn = microstrip_current_distribution(near);
        const auto jf = microstrip_current_distribution(far);
        REQUIRE(jn.has_value());
        REQUIRE(jf.has_value());
        REQUIRE(jn->current_density.numerical_value_in(A / m) >
                jf->current_density.numerical_value_in(A / m));
    }

    SECTION("J scales linearly with I0 (doubling I0 doubles J)") {
        auto x1 = base; x1.source_current = 10.0 * mA; x1.position = 2.0 * mm;
        auto x2 = base; x2.source_current = 20.0 * mA; x2.position = 2.0 * mm;
        const auto a = microstrip_current_distribution(x1);
        const auto b = microstrip_current_distribution(x2);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        const double doubled = 2.0 * a->current_density.numerical_value_in(A / m);
        REQUIRE(emc::test::approx(b->current_density, doubled * (A / m), 1e-9));
    }
}

// (c) VALIDATION / EDGE — out-of-domain inputs are typed errors, not inf.
TEST_CASE("microstrip current: validation errors", "[grounding][error]") {
    const auto ok = MicrostripCurrentInput{
        .source_current = 5.0 * mA,
        .trace_width    = 2.8 * mm,
        .height         = 1.6 * mm,
        .position       = 2.0 * mm,
    };

    SECTION("zero width -> OutOfRange (require_positive), not inf") {
        auto in = ok; in.trace_width = 0.0 * mm;
        const auto r = microstrip_current_distribution(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "trace_width");
    }

    SECTION("negative width -> OutOfRange") {
        auto in = ok; in.trace_width = -1.0 * mm;
        const auto r = microstrip_current_distribution(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }

    SECTION("zero height -> DivisionByZero") {
        auto in = ok; in.height = 0.0 * mm;
        const auto r = microstrip_current_distribution(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
        REQUIRE(r.error().field == "height");
    }
}

// (d) Concept conformance is a compile-time contract; re-checked in this TU.
static_assert(emc::ValidatedCalculator<emc::grounding::MicrostripCurrentDistribution>,
              "MicrostripCurrentDistribution must satisfy ValidatedCalculator");
