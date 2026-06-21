// src/shielding/cavity_resonance.cpp
#include <emc/shielding/cavity_resonance.hpp>

#include <algorithm>   // std::ranges::transform, std::ranges::min_element
#include <ranges>      // std::views::zip — pair the (m,n[,p]) table with its labels

#include <mp-units/math.h>          // mp_units::sqrt for quantities (used inside the header kernels)
#include <mp-units/systems/si.h>

namespace emc::shielding {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, Hz — pull-numeric-value units

// ===========================================================================
//  Rectangular enclosure
// ===========================================================================

std::expected<void, emc::Error> validate(const RectangularCavityInput& in) {
    // Pull each dimension out as a plain double in metres, then range-check; the
    // foundation validators report the offending field and (lo, hi) in that unit.
    if (auto r = emc::require_positive(in.length.numerical_value_in(m), "length"); !r) return r;
    if (auto r = emc::require_positive(in.width .numerical_value_in(m), "width");  !r) return r;
    if (auto r = emc::require_positive(in.height.numerical_value_in(m), "height"); !r) return r;
    // ε_r ≥ 1 is physical for a passive dielectric; an out-of-domain value is a typed
    // OutOfRange error rather than a silent inf/nan downstream.
    if (auto r = emc::in_range(in.eps_r, 1.0, 1.0e4, "eps_r"); !r) return r;
    return {};
}

emc::units::Frequency RectangularCavityResult::dominant() const {
    // min_element with a projection onto .frequency: the lowest resonance is the dominant.
    const auto it = std::ranges::min_element(
        modes, {}, [](const ModeFrequency& mf) { return mf.frequency; });
    return it->frequency;
}

emc::Result<RectangularCavityResult> rectangular_cavity_modes(const RectangularCavityInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    RectangularCavityResult out{};

    // ONE transform over the (m,n,p) table fills all 12 modes. zip pairs each index
    // triple with its label; the structured binding reads the pair cleanly.
    std::ranges::transform(
        std::views::zip(kRectangularModes, kRectangularLabels),
        out.modes.begin(),
        [&](const auto& pair) -> ModeFrequency {
            const auto& [mnp, label] = pair;
            return ModeFrequency{
                .label     = label,
                .frequency = detail::box_mode(in.eps_r,
                                              static_cast<double>(mnp[0]), in.length,
                                              static_cast<double>(mnp[1]), in.width,
                                              static_cast<double>(mnp[2]), in.height),
            };
        });

    return out;
}

// ===========================================================================
//  Cylindrical enclosure
// ===========================================================================

std::expected<void, emc::Error> validate(const CylindricalCavityInput& in) {
    if (auto r = emc::require_positive(in.length.numerical_value_in(m), "length"); !r) return r;
    if (auto r = emc::require_positive(in.radius.numerical_value_in(m), "radius"); !r) return r;
    if (auto r = emc::in_range(in.eps_r, 1.0, 1.0e4, "eps_r"); !r) return r;
    return {};
}

emc::units::Frequency CylindricalCavityResult::dominant() const {
    const auto it = std::ranges::min_element(
        modes, {}, [](const ModeFrequency& mf) { return mf.frequency; });
    return it->frequency;
}

emc::Result<CylindricalCavityResult> cylindrical_cavity_modes(const CylindricalCavityInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    CylindricalCavityResult out{};
    // One transform over the spec table fills all 21 ModeFrequency entries; the
    // family tag rides along in the table but the kernel only needs (χ, p).
    std::ranges::transform(
        kCylindricalModes, out.modes.begin(),
        [&](const CylModeSpec& s) -> ModeFrequency {
            return ModeFrequency{
                .label     = s.label,
                .frequency = detail::cyl_mode(in.eps_r, s.chi, in.radius,
                                              static_cast<double>(s.p), in.length),
            };
        });
    return out;
}

// ===========================================================================
//  Circuit-board planes
// ===========================================================================

std::expected<void, emc::Error> validate(const BoardPlaneInput& in) {
    const double l = in.length.numerical_value_in(m);
    const double w = in.width.numerical_value_in(m);
    const double s = in.separation.numerical_value_in(m);

    if (auto r = emc::require_positive(l, "length"); !r) return r;
    if (auto r = emc::require_positive(w, "width");  !r) return r;
    if (auto r = emc::require_positive(s, "separation"); !r) return r;

    // ε_r cannot be < 1.
    if (auto r = emc::in_range(in.eps_r, 1.0, 1.0e4, "eps_r"); !r) return r;

    // Thin-plane assumption s < l/10 and s < w/10, surfaced as DomainError (a model
    // assumption is violated, not a bare range), with the offending field named.
    if (s > 0.1 * l)
        return std::unexpected(emc::domain_error(
            "separation must be < length/10 (thin-plane assumption)", "separation"));
    if (s > 0.1 * w)
        return std::unexpected(emc::domain_error(
            "separation must be < width/10 (thin-plane assumption)", "separation"));
    return {};
}

emc::units::Frequency BoardPlaneResult::dominant() const {
    const auto it = std::ranges::min_element(
        modes, {}, [](const ModeFrequency& mf) { return mf.frequency; });
    return it->frequency;
}

emc::Result<BoardPlaneResult> circuit_board_plane_modes(const BoardPlaneInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    BoardPlaneResult out{};
    std::ranges::transform(
        std::views::zip(kBoardPlaneModes, kBoardPlaneLabels),
        out.modes.begin(),
        [&](const auto& pair) -> ModeFrequency {
            const auto& [mn, label] = pair;
            return ModeFrequency{
                .label     = label,
                // The board kernel is the box kernel with the height axis dropped:
                // passing p = 0 makes the (p/h)² term short-circuit to 0/m, so the
                // height argument (in.length here) is never used. No separate 2-D kernel.
                .frequency = detail::box_mode(in.eps_r,
                                              static_cast<double>(mn[0]), in.length,
                                              static_cast<double>(mn[1]), in.width,
                                              0.0, in.length /* unused; index 0 drops it */),
            };
        });
    return out;
}

} // namespace emc::shielding
