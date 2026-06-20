// examples/skin_depth.cpp — a standing smoke test of the public surface
#include <print>

#include <mp-units/systems/si.h>

#include <emc/basic/skin_depth.hpp>
#include <emc/core/materials.hpp>

int main() {
    using namespace mp_units;
    using namespace mp_units::si::unit_symbols;

    const emc::basic::SkinDepthInput in{
        .frequency = 1.0 * MHz,
        .material  = emc::materials::Material::Copper,
    };

    if (const auto r = emc::basic::calculate(in)) {
        std::println("skin depth @ 1 MHz (Cu) = {}", r->skin_depth.in(um));
        return 0;
    } else {
        std::println(stderr, "error: {}", r.error().what());
        return 1;
    }
}
