// tests/component/harmonic_trap_test.cpp
//
// Catch2 v3 tests for emc::component::calculate(HarmonicTrapInput) — the
// trapezoidal-pulse-train spectrum (f0, f, A_h, A_e).
//
// Expected values are hand-computed / textbook (the established two-sinc
// trapezoidal-spectrum closed form), written inline. No CSV/golden fixtures.
#include <cmath>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <emc/component/harmonic_trap.hpp>

#include <mp-units/systems/si.h>

#include "support/approx.hpp"   // emc::test::approx

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // s, ns, V, Hz, MHz
using emc::ErrorCode;
using namespace emc::component;

namespace {

// Textbook re-derivation of the trapezoidal-spectrum closed form, in coherent
// SI units (seconds, volts, Hz). Independent of calculate(): it lets the parity
// test prove the ported branch logic (both envelope breakpoints) reproduces the
// established formula exactly.
struct Ref { double f0, f, ah, ae; };
Ref expected_ref(double n, double Am, double trS, double Ts, double DC) {
    const double pi = std::numbers::pi_v<double>;
    const double s2 = std::numbers::sqrt2_v<double>;
    const double dc = DC / 100.0;
    const double Fo = 1.0 / Ts;
    const double F  = n * Fo;
    const double x1 = n * pi * dc;
    const double x2 = n * pi * (trS / Ts);
    const double Ah = s2 * (Am * dc) * std::abs(std::sin(x1) / x1)
                          * std::abs(std::sin(x2)) / x2;
    const double pitauinv = 1.0 / (pi * dc * Ts);
    const double pitrinv  = 1.0 / (pi * trS);
    double Ae = s2 * (Am * dc);
    if (F > pitauinv) Ae = (s2 * Am) / (n * pi);
    if (F > pitrinv)  Ae = Ae * pitrinv / F;
    return {Fo, F, Ah, Ae};
}

} // namespace

// (a) KNOWN-VALUE — a clean clock: 50 ns period, 5 ns edges, 50 % duty,
// 10 V peak, 3rd harmonic. Hand-computed entirely outside the implementation:
//   f0 = 1/50ns = 20 MHz ;  f = 3*20 = 60 MHz.
//   dc = 0.5 ;  x1 = 3*pi*0.5 = 1.5pi  -> sin(1.5pi) = -1, |sin/x1| = 1/(1.5pi).
//   x2 = 3*pi*(5/50) = 0.3pi ;  sin(0.3pi) = 0.809016994.
//   A_h = sqrt2 * (10*0.5) * (1/(1.5pi)) * (sin(0.3pi)/(0.3pi)).
// f = 60 MHz sits below both breakpoints (1/(pi*tau) = 1/(pi*25ns) ~ 12.7 MHz?
// actually tau = 0.5*50ns = 25ns so pitauinv ~ 12.7 MHz < 60 MHz, and
// pitrinv = 1/(pi*5ns) ~ 63.7 MHz > 60 MHz): so A_e = sqrt2*Am/(n*pi).
TEST_CASE("HarmonicTrap known clock value", "[component][harmonic_trap][known]") {
    const HarmonicTrapInput in{
        .harmonic = 3.0, .amplitude = 10.0 * V,
        .transition = 5.0 * ns, .period = 50.0 * ns, .duty_cycle = 50.0,
    };
    const auto r = calculate(in);
    REQUIRE(r.has_value());

    REQUIRE(emc::test::approx(r->fundamental_frequency, 20.0 * MHz, 1e-9));
    REQUIRE(emc::test::approx(r->harmonic_frequency,    60.0 * MHz, 1e-9));

    const double pi = std::numbers::pi_v<double>;
    const double s2 = std::numbers::sqrt2_v<double>;

    const double ah = s2 * 5.0 * (1.0 / (1.5 * pi))
                         * (std::sin(0.3 * pi) / (0.3 * pi));
    REQUIRE(emc::test::approx(r->harmonic_amplitude, ah * V, 1e-9));

    // f = 60 MHz > pitauinv (~12.73 MHz) and f < pitrinv (~63.66 MHz):
    // only the -20 dB/dec branch fires, so A_e = sqrt2 * Am / (n*pi).
    const double ae = (s2 * 10.0) / (3.0 * pi);
    REQUIRE(emc::test::approx(r->envelope_amplitude, ae * V, 1e-9));
}

// (b) PARITY against the textbook closed form for a spread of cases — proves the
// branch logic (flat top / -20 / -40 dB-per-decade) matches the established form.
TEST_CASE("HarmonicTrap reproduces the closed form", "[component][harmonic_trap][parity]") {
    struct C { double n, Am, tr, T, DC; };
    const auto c = GENERATE(values<C>({
        {3,   10.0,   5e-9,   50e-9,  50.0},   // clean clock (flat / -20 region)
        {71,  908.7,  8.7386e-9, 64.79e-9, 96.0}, // large n
        {256, 394.4,  1.5698e-9, 96.226e-9, 12.0}, // low duty, deep into -40 region
        {2,   1.0,    1e-9,   1e-6,   1.0},     // tiny duty, slow period
    }));
    const Ref ref = expected_ref(c.n, c.Am, c.tr, c.T, c.DC);
    const HarmonicTrapInput in{
        .harmonic = c.n, .amplitude = c.Am * V,
        .transition = c.tr * s, .period = c.T * s, .duty_cycle = c.DC,
    };
    const auto r = calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->fundamental_frequency, ref.f0 * Hz, 1e-9));
    REQUIRE(emc::test::approx(r->harmonic_frequency,    ref.f  * Hz, 1e-9));
    REQUIRE(emc::test::approx(r->harmonic_amplitude,    ref.ah * V,  1e-9));
    REQUIRE(emc::test::approx(r->envelope_amplitude,    ref.ae * V,  1e-9));
}

// (c) PROPERTY — the harmonic frequency is exactly n * fundamental, for every n,
// independent of all the other spectral math.
TEST_CASE("HarmonicTrap f = n*f0 identity", "[component][harmonic_trap][property]") {
    const double n = GENERATE(1.0, 2.0, 3.0, 7.0, 99.0, 1000.0);
    const HarmonicTrapInput in{
        .harmonic = n, .amplitude = 5.0 * V,
        .transition = 2.0 * ns, .period = 100.0 * ns, .duty_cycle = 40.0,
    };
    const auto r = calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(
        r->harmonic_frequency,
        n * r->fundamental_frequency.numerical_value_in(Hz) * Hz, 1e-12));
}

// (d) VALIDATION / EDGE — degenerate inputs return the RIGHT ErrorCode and field.
TEST_CASE("HarmonicTrap rejects degenerate inputs", "[component][harmonic_trap][validation]") {
    const HarmonicTrapInput base{
        .harmonic = 3.0, .amplitude = 10.0 * V,
        .transition = 5.0 * ns, .period = 50.0 * ns, .duty_cycle = 50.0,
    };

    SECTION("DC = 0 -> DivisionByZero on duty_cycle") {
        auto in = base; in.duty_cycle = 0.0;
        const auto r = calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
        REQUIRE(r.error().field == "duty_cycle");
    }
    SECTION("DC > 100 -> OutOfRange") {
        auto in = base; in.duty_cycle = 150.0;
        const auto r = calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "duty_cycle");
    }
    SECTION("period = 0 -> OutOfRange (would be f0 = 1/0)") {
        auto in = base; in.period = 0.0 * ns;
        const auto r = calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "period");
    }
    SECTION("t_r = 0 -> OutOfRange (would be pitrinv = inf)") {
        auto in = base; in.transition = 0.0 * ns;
        const auto r = calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "transition");
    }
    SECTION("harmonic = 0 -> OutOfRange") {
        auto in = base; in.harmonic = 0.0;
        const auto r = calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "harmonic");
    }
}
