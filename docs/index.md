# emcpp

A modern **C++23 library of closed-form EMC (electromagnetic-compatibility) engineering calculators** — skin depth, microstrip/stripline impedance, shielding effectiveness, cable crosstalk, antenna near-fields, cavity resonances, the Friis link budget, and 50+ more — with **compile-time unit safety** powered by [mp-units](https://github.com/mpusz/mp-units).

```cpp
#include <emc/basic/skin_depth.hpp>

emc::basic::SkinDepthInput in{ .frequency = 1.0 * MHz, .material = emc::materials::Material::Copper };
auto result = emc::basic::calculate(in);   // -> emc::Result<SkinDepthResult>
// result->skin_depth == ~65.21 um   (wrong units simply don't compile)
```

## Why emcpp

- **Unit-safe by construction.** Every input and output is an mp-units quantity. Mixing units is a *compile error*, not a runtime surprise — call sites read like physics: `1.5 * mm`, `1000.0 * Hz`.
- **Errors are values, not exceptions.** Every `calculate()` returns `emc::Result<T>` (`std::expected<T, emc::Error>`) carrying a structured code, message, offending field, valid range, and source location.
- **Compile-time contracts.** Each calculator is bound to a `Calculator` / `ValidatedCalculator` concept and proven with `static_assert` — generic code, zero virtual dispatch.
- **Validated against trusted references.** Every calculator is checked against spreadsheet ground-truth — see [Validation](validation.md).

## Start here

<div class="grid cards" markdown>

- :material-rocket-launch: **[Getting started](getting-started.md)** — build, install, and consume emcpp from your own CMake project.
- :material-cog: **[Architecture](architecture.md)** — the `Input`/`Result` + concept model, units, and error handling.
- :material-calculator: **[Calculator catalog](calculators.md)** — every calculator, grouped by the 9 EMC domains.
- :material-check-decagram: **[Validation](validation.md)** — 49 calculators × ~100 reference points vs the trusted spreadsheets.
- :material-source-branch: **[Development & CI](development.md)** — toolchains, the CI matrix, and the clang port status.
- :material-api: **[API reference](api/index.html)** — the full Doxygen reference, generated from the headers.

</div>

!!! note "License"
    emcpp is licensed under the **Apache License 2.0** — use freely (including commercially), retaining the copyright, license, and `NOTICE`.
