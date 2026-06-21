// tests/core/materials_test.cpp
//
// Unit tests for emc::materials — the conductor property database.
// Expected values are textbook / handbook figures written inline (no CSV).
//   * Known-value:  properties(Copper) row matches the pinned table values.
//   * Property:     resistivity is the exact reciprocal of conductivity (rho == 1/sigma),
//                   and the named constant emc::materials::copper aliases the looked-up row.
//   * Validation:   properties(Custom) fails with ErrorCode::UnknownMaterial.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>   // Catch::Approx for the bare-double mu_r / eps_r checks

#include <mp-units/systems/si.h>

#include <emc/core/materials.hpp>
#include <emc/core/units.hpp>

#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // S, m, ohm, ...

// ---------------------------------------------------------------------------
//  (a) KNOWN-VALUE: Copper's pinned row.
//      Handbook (IACS-consistent): sigma = 5.96e7 S/m, mu_r = 0.999991,
//      eps_r = 1.0, and rho = 1/sigma = 1.6778523489932886e-8 ohm*m.
// ---------------------------------------------------------------------------
TEST_CASE("materials: properties(Copper) returns the pinned handbook row", "[materials][known]") {
    const auto r = emc::materials::properties(emc::materials::Material::Copper);
    REQUIRE(r.has_value());

    // sigma = 5.96e7 S/m
    REQUIRE(emc::test::approx(r->conductivity, 5.96e7 * (S / m), 1e-9));

    // rho = 1/sigma = 1.677852348993...e-8 ohm*m
    REQUIRE(emc::test::approx(r->resistivity, (1.0 / 5.96e7) * (ohm * m), 1e-9));

    // mu_r and eps_r are plain dimensionless doubles in the pinned API.
    REQUIRE(r->relative_permeability == Catch::Approx(0.999991).epsilon(1e-9));
    REQUIRE(r->relative_permittivity == Catch::Approx(1.0).epsilon(1e-9));
}

// ---------------------------------------------------------------------------
//  (b) PROPERTY / SELF-CONSISTENCY:
//      For every built-in material rho * sigma == 1 (resistivity is the cached
//      reciprocal of conductivity), and the named constant aliases the row that
//      properties() hands back for the same enum.
// ---------------------------------------------------------------------------
TEST_CASE("materials: resistivity is the reciprocal of conductivity", "[materials][property]") {
    using emc::materials::Material;
    const auto first = static_cast<std::size_t>(Material::Copper);
    const auto last  = static_cast<std::size_t>(Material::Count);

    for (std::size_t i = first; i < last; ++i) {
        const auto r = emc::materials::properties(static_cast<Material>(i));
        REQUIRE(r.has_value());

        const double sigma = r->conductivity.numerical_value_in(si::siemens / si::metre);
        const double rho   = r->resistivity.numerical_value_in(si::ohm * si::metre);

        REQUIRE(sigma > 0.0);
        REQUIRE(rho > 0.0);
        // rho == 1/sigma  =>  rho * sigma == 1
        REQUIRE((rho * sigma) == Catch::Approx(1.0).epsilon(1e-12));
    }
}

TEST_CASE("materials: named constant nickel matches the Nickel row", "[materials][property]") {
    // Nickel is the magnetic conductor: mu_r == 600 by the pinned table.
    REQUIRE(emc::materials::nickel.relative_permeability == 600.0);

    const auto r = emc::materials::properties(emc::materials::Material::Nickel);
    REQUIRE(r.has_value());
    REQUIRE(r->relative_permeability == emc::materials::nickel.relative_permeability);
    REQUIRE(emc::test::approx(r->conductivity, emc::materials::nickel.conductivity, 1e-12));
    REQUIRE(emc::test::approx(r->resistivity, emc::materials::nickel.resistivity, 1e-12));
}

// ---------------------------------------------------------------------------
//  (c) VALIDATION / EDGE: Custom has no table row, so it is a typed error,
//      not a magic sentinel. Code must be ErrorCode::UnknownMaterial.
// ---------------------------------------------------------------------------
TEST_CASE("materials: properties(Custom) fails with UnknownMaterial", "[materials][edge]") {
    const auto r = emc::materials::properties(emc::materials::Material::Custom);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::UnknownMaterial);
    REQUIRE(r.error().field == "material");
}
