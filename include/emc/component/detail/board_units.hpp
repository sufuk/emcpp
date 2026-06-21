// include/emc/component/detail/board_units.hpp
#pragma once

#include <mp-units/systems/si.h>

namespace emc::component::detail {

using namespace mp_units;

// 1 mil = 1/1000 inch = 0.0254 mm — EXACT. Defined once so imperial trace dimensions are
// first-class typed quantities shared by all four board-impedance calculators; conversions
// to/from mm are *derived* by mp-units, not by a hand-coded factor.
//
// We register `mil` on the SI graph as a scaled metre: mag_ratio<254, 10'000'000> is
// 0.0000254 m = 0.0254 mm, so .in(mil) / .in(mm) round-trip exactly. One definition, zero
// unit branches anywhere in the calculators. (If a project-wide imperial unit set is later
// added to units.hpp, move `mil` there; it lives in detail for now because only these
// trace-impedance calculators use it.)
inline constexpr struct mil_ final
    : named_unit<"mil", mag_ratio<254, 10'000'000> * si::metre> {} mil;

// 1 inch = 25.4 mm — EXACT. The stripline/dual-stripline calculators display C0 in pF/inch and
// Tpd in ps/inch; defining the inch here keeps the storage-unit relabel self-contained (no
// imperial-system header needed) while staying exact. mag_ratio<254, 10'000> == 0.0254 m == 25.4 mm.
inline constexpr struct inch_ final
    : named_unit<"in", mag_ratio<254, 10'000> * si::metre> {} inch;

}  // namespace emc::component::detail
