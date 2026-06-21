// include/emc/core/constants.hpp
#pragma once

#include <numbers>     // std::numbers::pi gives one full-precision pi for the whole program
#include <cmath>       // std::sqrt etc. for runtime math in headers that include this one

#include <mp-units/math.h>          // mp_units::sqrt — constexpr AND portable. std::sqrt is only
                                    // constexpr as a libstdc++/GCC extension (not under libc++),
                                    // so the compile-time identity checks below use mp_units::sqrt.
#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>

namespace emc::constants {

using namespace mp_units;
using mp_units::si::unit_symbols::A;    // ampere
using mp_units::si::unit_symbols::F;    // farad
using mp_units::si::unit_symbols::H;    // henry
using mp_units::si::unit_symbols::J;    // joule
using mp_units::si::unit_symbols::m;    // metre
using mp_units::si::unit_symbols::s;    // second
using mp_units::si::unit_symbols::ohm;  // ohm

// pi (pure math).
// Kept as a plain double for raw angle / closed-form math where no unit is involved.
// inline constexpr: a compile-time constant with ONE shared definition across the whole
// program, so every file uses the exact same value (no per-file copies).
inline constexpr double pi = std::numbers::pi_v<double>;

// c : speed of light in vacuum.
// This is an SI EXACT defining constant: c = 299 792 458 m/s.
// mp-units quantity: the unit (m/s) lives in the type, so wrong-unit math will not compile.
inline constexpr quantity c = 299'792'458.0 * (m / s);

// mu0 : vacuum permeability.
// CODATA-2018 value. Very close to 4*pi*1e-7 but no longer EXACT in SI.
inline constexpr quantity mu0 = 1.256'637'062'12e-6 * (H / m);

// eps0 : vacuum permittivity.
// CODATA literal. The static_assert below proves it equals 1/(mu0*c^2).
inline constexpr quantity eps0 = 8.854'187'8128e-12 * (F / m);

// h : Planck constant.
// SI EXACT defining constant since the 2019 redefinition. h = 6.626 070 15e-34 J*s.
inline constexpr quantity h = 6.626'070'15e-34 * (J * s);

// z0 : impedance of free space = sqrt(mu0/eps0) = mu0*c ~ 376.730313668 ohm.
inline constexpr quantity z0 = 376.730'313'668 * ohm;

// e : elementary charge (used by ESD / lightning coupling calculators).
// EXACT since 2019. A coulomb is A*s.
inline constexpr quantity elementary_charge = 1.602'176'634e-19 * (A * s);

// Why inline constexpr and not just constexpr?
// A plain constexpr variable in a header has internal linkage by default, so each file
// that includes this header gets its own private copy with its own address (an ODR trap).
// inline gives ONE shared definition for the whole program, which is exactly what a
// "single source of truth" for constants needs. The short names c, mu0, eps0, h, z0, pi
// are the canonical contract every calculator relies on.

} // namespace emc::constants

namespace emc::constants::detail {

using namespace mp_units;

// Relative comparison helper. We avoid exact == on floating point because it is brittle.
// [[nodiscard]]: do not ignore the result; throwing away a comparison answer is a bug, so
//   the compiler warns.
// constexpr: can run at compile time, which lets us call it inside the static_asserts below.
// noexcept: promises not to throw; lets the compiler optimize and callers rely on it.
[[nodiscard]] constexpr bool close(double a, double b, double rel = 1e-6) noexcept {
    const double d = a - b;
    const double ad = d < 0 ? -d : d;
    const double am = a < 0 ? -a : a;
    return ad <= rel * (am == 0.0 ? 1.0 : am);
}

// The static_asserts below turn the core electromagnetic identities into build-time checks.
// Edit one literal above into an inconsistent set and the build fails instead of shipping
// a silently wrong constant.

// 1) c == 1 / sqrt(eps0 * mu0)
// mp_units::sqrt keeps the units through the root: sqrt(eps0*mu0) is s/m, so its
// reciprocal is m/s. (constexpr & portable — see the <mp-units/math.h> note above.)
inline constexpr double c_check =
    (1.0 / mp_units::sqrt(eps0 * mu0)).numerical_value_in(si::metre / si::second);
static_assert(close(c_check, c.numerical_value_in(si::metre / si::second)),
              "c must equal 1/sqrt(eps0*mu0)");

// 2) z0 == sqrt(mu0 / eps0)   (mu0/eps0 is ohm^2, so its root is ohm)
inline constexpr double z0_check =
    mp_units::sqrt(mu0 / eps0).numerical_value_in(si::ohm);
static_assert(close(z0_check, z0.numerical_value_in(si::ohm)),
              "z0 must equal sqrt(mu0/eps0)");

// 3) z0 == mu0 * c   (the same impedance, expressed a different way)
static_assert(close((mu0 * c).numerical_value_in(si::ohm),
                    z0.numerical_value_in(si::ohm)),
              "z0 must equal mu0*c");

// 4) Our mu0 stays within 0.01% of 4*pi*1e-7 (the classical low-frequency value).
static_assert(close(mu0.numerical_value_in(si::henry / si::metre), 4.0 * pi * 1e-7, 1e-4),
              "mu0 should be close to 4*pi*1e-7");

// 5) pi really is the full-precision pi.
static_assert(pi > 3.14159 && pi < 3.14160, "pi must be std::numbers::pi");

} // namespace emc::constants::detail
