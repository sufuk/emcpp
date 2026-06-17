# Implementation Guide — Testing (`emc::testing`) 📡

> Complete C++23 code (public header, compiled `.cpp`, example usage, and Catch2 v3 tests) for the
> calculators in the **Testing** category of `emc`.

This guide covers one calculator:

| Calculator | Namespace | Header | Impl | Tests |
|---|---|---|---|---|
| Noise Figure of an RF Receiver (Friis cascade, N-stage) | `emc::testing` | `include/emc/testing/noise_figure.hpp` | `src/testing/noise_figure.cpp` | `tests/testing/noise_figure_test.cpp` |

The defining modeling decision is to express the receiver as an **N-stage cascade taken as
`std::span<const Stage>`**, where `Stage = { emc::units::Decibel nf; emc::units::Decibel gain; }`. A span
imposes no fixed stage ceiling and reads cleanly with a brace-init list of stages.

This guide assumes the foundation surface from [`00-foundation-code.md`](00-foundation-code.md):
`emc::Result<T>`, `emc::Error`/`emc::ErrorCode`, `emc::in_range`/`emc::require_positive`, the
`emc::units::Decibel`/`emc::units::Power` log wrappers with their explicit `to_ratio`/`to_decibel`
conversions, the `emc::Calculator` concept, and the `emc::test::approx` test helper. Names are used
**exactly** as pinned there.

---

## Noise Figure of an RF Receiver (Friis cascade, N-stage)

### 1. Overview

Given a receiver chain of cascaded stages, each with a **noise figure** `NF_i` (dB) and a **gain**
`G_i` (dB), this calculator returns:

- the **cascade noise figure** `NF = 10·log₁₀(F)` (dB), and
- the **total gain** `G_total = Σ G_i` (dB),

where the linear cascade noise factor follows **Friis's formula**:

```text
F = F₁ + (F₂ − 1)/G₁ + (F₃ − 1)/(G₁·G₂) + … + (F_n − 1)/(G₁·G₂·…·G_{n−1})
```

with each linear factor `F_i = 10^(NF_i/10)` and each linear gain `g_j = 10^(G_j/10)`. Only the gains
of the **preceding** stages divide a given stage's excess noise; the last stage's gain never enters
`F` (it only contributes to `G_total`).

> [!NOTE]
> The cascade noise figure is order-dependent — a low-noise, high-gain first stage suppresses the
> noise contribution of everything downstream. The total gain is just the dB sum and is
> order-independent.

### 2. Public header — `include/emc/testing/noise_figure.hpp`

```c++
// include/emc/testing/noise_figure.hpp
#pragma once

#include <expected>
#include <span>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>   // emc::units::Decibel

namespace emc::testing {

// ---------------------------------------------------------------------------
//  Stage — one element of the receiver cascade. Both fields are logarithmic
//  (dB) values, modeled with the pinned emc::units::Decibel wrapper (NOT a
//  linear mp-units unit; see foundation §3 / plan doc 03 §9).
//
//  A whole cascade is just a std::span<const Stage>, so 2/3/N stages cost the
//  same.
// ---------------------------------------------------------------------------
struct Stage {
    emc::units::Decibel nf{};     // per-stage noise figure  NF_i [dB]
    emc::units::Decibel gain{};   // per-stage gain          G_i  [dB]
};

// ---------------------------------------------------------------------------
//  Input — the whole cascade as a non-owning span of stages. The span makes
//  the calculator agnostic to how the stages are stored (array, vector, …);
//  the caller owns the storage.
// ---------------------------------------------------------------------------
struct NoiseFigureInput {
    std::span<const Stage> stages{};   // ordered source/antenna .. last stage
};

// ---------------------------------------------------------------------------
//  Result — both outputs, each a dB value.
// ---------------------------------------------------------------------------
struct NoiseFigureResult {
    emc::units::Decibel noise_figure{};   // cascade NF = 10*log10(F)   [dB]
    emc::units::Decibel total_gain{};     // G_total = sum(G_i)         [dB]
};

// ---------------------------------------------------------------------------
//  validate() — structural checks the math relies on: keep std::log10 in its
//  domain (F > 0, which holds for >= 1 stage) and reject the meaningless empty
//  cascade.
// ---------------------------------------------------------------------------
[[nodiscard]] std::expected<void, emc::Error> validate(const NoiseFigureInput& in);

// ---------------------------------------------------------------------------
//  calculate() — Friis cascade. Forward-only.
// ---------------------------------------------------------------------------
[[nodiscard]] emc::Result<NoiseFigureResult> calculate(const NoiseFigureInput& in);

// ---------------------------------------------------------------------------
//  Calculator-concept binding: the (Input, Result, calculate, validate) triple
//  as a tag type, checked at compile time (foundation §5).
// ---------------------------------------------------------------------------
struct NoiseFigure {
    using Input  = NoiseFigureInput;
    using Result = NoiseFigureResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::testing::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::testing::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<NoiseFigure>);

} // namespace emc::testing
```

### 3. Implementation — `src/testing/noise_figure.cpp`

```c++
// src/testing/noise_figure.cpp
#include <emc/testing/noise_figure.hpp>

#include <cmath>   // std::log10  (constexpr in C++23)

#include <emc/core/units.hpp>   // to_ratio(Decibel) -> linear power ratio

namespace emc::testing {

// ---------------------------------------------------------------------------
//  validate()
//  The only true precondition for the Friis math is that the cascade is
//  non-empty: with >= 1 stage, F >= F_1 = 10^(NF_1/10) > 0, so std::log10(F)
//  is always in-domain. An empty cascade has no defined noise figure (log10 of
//  0), so we reject it explicitly rather than returning -inf.
// ---------------------------------------------------------------------------
std::expected<void, emc::Error> validate(const NoiseFigureInput& in) {
    if (in.stages.empty())
        return std::unexpected(emc::invalid_input(
            "receiver cascade must contain at least one stage", "stages"));
    return {};   // no per-stage range limits: any real NF/gain in dB is admissible
}

// ---------------------------------------------------------------------------
//  calculate()  — Friis cascade over a std::span of N stages.
//
//    F        = sum_i  numerator_i / denominator_i
//    numerator_0   = 10^(NF_1/10)            (full linear factor of stage 1)
//    numerator_i>0 = 10^(NF_i/10) - 1        (excess noise of later stages)
//    denominator_i = product_{j<i} 10^(G_j/10)   (preceding gains only)
//    NF       = 10 * log10(F)
//    G_total  = sum_i  G_i                   (added in dB)
// ---------------------------------------------------------------------------
emc::Result<NoiseFigureResult> calculate(const NoiseFigureInput& in) {
    if (auto ok = validate(in); !ok)
        return std::unexpected(ok.error());

    double F = 0.0;
    double total_gain_dB = 0.0;

    // Running product of the linear gains of all PRECEDING stages.
    // Starts at 1 (empty product) so stage 0 sees denominator == 1.
    double preceding_gain_product = 1.0;

    for (std::size_t i = 0; i < in.stages.size(); ++i) {
        const Stage& s = in.stages[i];

        // dB -> linear power ratio via the explicit, type-safe foundation helper.
        const double f_lin = emc::units::to_ratio(s.nf);     // 10^(NF_i/10)

        const double numerator = (i == 0) ? f_lin : (f_lin - 1.0);
        F += numerator / preceding_gain_product;

        // Fold THIS stage's gain into the running product for the NEXT stage,
        // and accumulate the dB sum for the total-gain output.
        preceding_gain_product *= emc::units::to_ratio(s.gain);   // 10^(G_i/10)
        total_gain_dB += s.gain.value;
    }

    // F >= 10^(NF_1/10) > 0 for any non-empty cascade, so log10 is in-domain;
    // validate() guarantees non-empty, so no DomainError branch is reachable.
    const double nf_dB = 10.0 * std::log10(F);

    return NoiseFigureResult{
        .noise_figure = emc::units::Decibel{nf_dB},
        .total_gain   = emc::units::Decibel{total_gain_dB},
    };
}

} // namespace emc::testing
```

> [!TIP]
> The running `preceding_gain_product` makes the cascade O(N): each preceding-gain product is built
> incrementally instead of recomputed from scratch per stage. A naive transcription of the formula is
> O(N²).

### 4. Modern C++ features used here — and why

- **`std::span<const Stage>` input** — the single highest-value design choice. EMC receiver chains have
  no fixed stage count, so a span lets one `calculate()` serve 1, 2, 3, or 100 stages while the caller
  picks the storage (array, vector, …). No stage-count ceiling, no stringly-typed count. (doc 06 §3,
  doc 02 — "span for noise cascades".)
- **Aggregate `Stage` / `NoiseFigureInput` + designated initializers** — call sites read
  `Stage{.nf = Decibel{1.0}, .gain = Decibel{20.0}}`; the field name *is* the documentation of which dB
  value is which, so adjacent same-typed values can't be transposed by accident.
- **Typed `emc::units::Decibel` wrapper + explicit `to_ratio()`** — the inputs and outputs are
  genuinely logarithmic. The foundation models dB as a typed wrapper (never a linear mp-units unit), so
  the `dB → linear` conversion `10^(x/10)` happens **only** at the explicit `to_ratio` boundary; you
  cannot accidentally add a `Decibel` to a linear `Power`, nor use a dB value as if it were linear.
- **`std::expected<void, Error>` + `emc::ErrorCode`** — calculator inputs have physical domains, so an
  out-of-domain input (here, an empty cascade) is reported as a recoverable, typed `InvalidInput` value
  the caller must handle, rather than producing `-inf`/`nan` downstream.
- **`[[nodiscard]]` on `validate`/`calculate`** — discarding the result (and thus a possible error) is
  a compiler warning.
- **`constexpr <cmath>` (`std::log10`)** — C++23 makes the log math constant-foldable; combined with
  the closed-form loop, a fixed cascade can be evaluated at compile time (see the constexpr test).
- **`const&` iteration over the span** — `const Stage& s = in.stages[i]` keeps the running
  `preceding_gain_product` accumulation readable while staying O(N).

> [!NOTE]
> Forward-looking (C++26): once `std::span` gains richer range adaptors, the running product could be
> expressed as `views::enumerate` (already C++23) plus an inclusive-scan over gains. We keep the
> explicit loop here for the clearest mapping to the formula and to stay copy-paste-buildable on a
> C++23 toolchain.

### 5. Example usage

```c++
#include <array>
#include <print>

#include <emc/testing/noise_figure.hpp>

int main() {
    using emc::units::Decibel;
    using namespace emc::testing;

    // A 3-stage front end: LNA (low NF, high gain), mixer, IF amp.
    constexpr std::array<Stage, 3> chain{{
        {.nf = Decibel{1.0}, .gain = Decibel{20.0}},   // LNA
        {.nf = Decibel{6.0}, .gain = Decibel{-3.0}},   // passive mixer (loss)
        {.nf = Decibel{8.0}, .gain = Decibel{15.0}},   // IF amplifier
    }};

    const auto r = calculate(NoiseFigureInput{.stages = chain});

    if (!r) {
        // Error path: structured, inspectable, typed.
        std::println("noise-figure error [{}]: {}",
                     emc::to_string(r.error().code), r.error().message);
        return 1;
    }

    // Pull each dB result out of its typed wrapper for display.
    std::println("cascade NF  = {:.3f} dB", r->noise_figure.value);
    std::println("total gain  = {:.3f} dB", r->total_gain.value);
    // For this chain: NF ~= 1.45 dB, total gain = 32 dB.
}
```

> [!TIP]
> The result type is framework-agnostic: a generic front end formats the typed `Decibel` fields with
> `std::print`/`std::format` (or hands them to any UI layer) with no toolkit-specific symbols in the
> library.

### 6. Unit tests — `tests/testing/noise_figure_test.cpp`

```c++
// tests/testing/noise_figure_test.cpp
#include <array>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>

#include <emc/testing/noise_figure.hpp>

#include "support/approx.hpp"

using emc::units::Decibel;
using namespace emc::testing;

namespace {

// Local, dependency-free reference implementation of the Friis cascade in dB,
// written straight from the textbook formula, so the known-value tests do not
// merely re-run the production code.
double friis_nf_dB(std::span<const Stage> stages) {
    double F = 0.0, prod = 1.0;
    for (std::size_t i = 0; i < stages.size(); ++i) {
        const double f = std::pow(10.0, stages[i].nf.value / 10.0);
        F += (i == 0 ? f : f - 1.0) / prod;
        prod *= std::pow(10.0, stages[i].gain.value / 10.0);
    }
    return 10.0 * std::log10(F);
}

// Compare two dB scalars with a relative tolerance (the Decibel wrapper is a
// plain double, so we compare .value directly).
bool db_close(Decibel a, Decibel b, double rel = 1e-6) {
    const double diff = std::abs(a.value - b.value);
    return diff <= 1e-12 || diff <= rel * std::max(std::abs(a.value), std::abs(b.value));
}

} // namespace

// ---------------------------------------------------------------------------
// (a) REFERENCE-VALUE TEST — production output must match an independent
//     reference implementation of the Friis formula over a 3-stage chain.
//     Guards: end-to-end fidelity of the cascade math.
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure matches an independent Friis reference", "[testing][noise_figure]") {
    const std::array<Stage, 3> stages{{
        {.nf = Decibel{86.86}, .gain = Decibel{2.47}},
        {.nf = Decibel{88.37}, .gain = Decibel{4.30}},
        {.nf = Decibel{50.70}, .gain = Decibel{2.80}},
    }};

    const auto r = calculate(NoiseFigureInput{.stages = stages});
    REQUIRE(r.has_value());

    const double expected_nf   = friis_nf_dB(stages);
    const double expected_gain = 2.47 + 4.30 + 2.80;

    REQUIRE(db_close(r->noise_figure, Decibel{expected_nf},   1e-9));
    REQUIRE(db_close(r->total_gain,   Decibel{expected_gain}, 1e-9));
}

// ---------------------------------------------------------------------------
// (b) HAND-COMPUTED KNOWN-VALUE TEST — textbook two-stage example.
//     LNA: NF=1 dB, G=20 dB ; 2nd: NF=10 dB, G=10 dB.
//     F1 = 10^0.1 = 1.258925 ; g1 = 10^2 = 100
//     F2 = 10^1.0 = 10        ; F = 1.258925 + (10-1)/100 = 1.348925
//     NF = 10*log10(1.348925) = 1.30016 dB ; G_total = 30 dB.
//     Guards: the formula itself, with numbers a human can verify by hand.
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure two-stage textbook value", "[testing][noise_figure]") {
    const std::array<Stage, 2> stages{{
        {.nf = Decibel{1.0},  .gain = Decibel{20.0}},
        {.nf = Decibel{10.0}, .gain = Decibel{10.0}},
    }};
    const auto r = calculate(NoiseFigureInput{.stages = stages});
    REQUIRE(r.has_value());
    REQUIRE(db_close(r->noise_figure, Decibel{1.300160}, 1e-5));
    REQUIRE(db_close(r->total_gain,   Decibel{30.0},     1e-12));
}

// ---------------------------------------------------------------------------
// (c) PROPERTY TESTS
//   c1. Single stage: cascade NF == that stage's NF, gain == that stage's gain.
//   c2. Dominance: a high-gain first stage makes the cascade NF approach NF_1
//       (the whole point of an LNA). Adding a noisy 2nd stage barely moves NF.
//   c3. Total gain is order-independent (a plain sum), but NF is NOT —
//       swapping a low-NF/high-gain stage to the back must raise the cascade NF.
//   Guards: the qualitative physics the calculator exists to express.
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure single stage is the identity", "[testing][noise_figure][property]") {
    const std::array<Stage, 1> one{{ {.nf = Decibel{3.5}, .gain = Decibel{12.0}} }};
    const auto r = calculate(NoiseFigureInput{.stages = one});
    REQUIRE(r.has_value());
    REQUIRE(db_close(r->noise_figure, Decibel{3.5}));   // NF == NF_1 exactly
    REQUIRE(db_close(r->total_gain,   Decibel{12.0}));
}

TEST_CASE("NoiseFigure: high first-stage gain dominates", "[testing][noise_figure][property]") {
    const std::array<Stage, 2> good{{
        {.nf = Decibel{1.0},  .gain = Decibel{40.0}},   // big LNA gain
        {.nf = Decibel{15.0}, .gain = Decibel{5.0}},    // very noisy 2nd stage
    }};
    const auto r = calculate(NoiseFigureInput{.stages = good});
    REQUIRE(r.has_value());
    // With 40 dB of first-stage gain, (F2-1)/G1 is tiny: NF stays within
    // ~0.01 dB of the 1 dB first-stage figure.
    REQUIRE(r->noise_figure.value < 1.01);
    REQUIRE(r->noise_figure.value > 1.00);
}

TEST_CASE("NoiseFigure: stage order changes the cascade NF", "[testing][noise_figure][property]") {
    const std::array<Stage, 2> lna_first{{
        {.nf = Decibel{1.0},  .gain = Decibel{20.0}},
        {.nf = Decibel{10.0}, .gain = Decibel{10.0}},
    }};
    const std::array<Stage, 2> lna_last{{
        {.nf = Decibel{10.0}, .gain = Decibel{10.0}},
        {.nf = Decibel{1.0},  .gain = Decibel{20.0}},
    }};
    const auto a = calculate(NoiseFigureInput{.stages = lna_first});
    const auto b = calculate(NoiseFigureInput{.stages = lna_last});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    // LNA-first must be the lower-noise arrangement.
    REQUIRE(a->noise_figure.value < b->noise_figure.value);
    // Total gain is order-independent (plain sum).
    REQUIRE(db_close(a->total_gain, b->total_gain, 1e-12));
}

// ---------------------------------------------------------------------------
// (d) VALIDATION / EDGE TEST — empty cascade returns the RIGHT ErrorCode.
//     Guards: the one structural precondition (log10 of 0).
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure rejects an empty cascade", "[testing][noise_figure][error]") {
    const auto r = calculate(NoiseFigureInput{.stages = {}});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::InvalidInput);
    REQUIRE(r.error().field == "stages");
}

// ---------------------------------------------------------------------------
// (e) CONSTEXPR / COMPILE-TIME SMOKE TEST.
//     calculate() is constexpr-friendly (only constexpr <cmath> + a span loop).
//     If the toolchain's std::expected/std::log10 are constexpr-complete this
//     folds at compile time; the runtime REQUIRE pins the value regardless.
//     Guards: the math stays a pure, side-effect-free closed form.
// ---------------------------------------------------------------------------
TEST_CASE("NoiseFigure two-stage value is stable", "[testing][noise_figure][constexpr]") {
    static constexpr std::array<Stage, 2> stages{{
        {.nf = Decibel{1.0},  .gain = Decibel{20.0}},
        {.nf = Decibel{10.0}, .gain = Decibel{10.0}},
    }};
    const auto r = calculate(NoiseFigureInput{.stages = stages});
    REQUIRE(r.has_value());
    REQUIRE(db_close(r->noise_figure, Decibel{1.300160}, 1e-5));
}
```

> **What each test guards, in one line each:**
> (a) production output vs an independent Friis reference; (b) the formula against a hand-checked
> number; (c1) the single-stage identity; (c2) the LNA dominance the design exists for; (c3)
> order-sensitivity of NF vs order-independence of total gain; (d) the empty-cascade guard; (e) value
> stability / constexpr-friendliness.

### 7. Design notes

- **Validation is minimal by design.** The only structural precondition for the Friis math is a
  non-empty cascade: with zero stages `F == 0` and `std::log10(F)` is `-inf`. We add exactly one
  guard — non-empty cascade → `ErrorCode::InvalidInput` — and nothing more, because any real NF/gain in
  dB (including negative gain for lossy stages) is physically admissible, so we deliberately impose
  **no** numeric range limits.
- **`10·std::log10(F)` for the cascade NF.** Using `std::log10` gives full `ln(10)` precision directly,
  with no separately-rounded constant in the arithmetic path.
- **Single conversion helper.** Both the numerator and denominator conversions go through the one
  unambiguous `to_ratio(Decibel)` helper (`10^(x/10.0)`), so there is no risk of integer-truncation in
  the divisor.
- **O(N) by construction.** The running `preceding_gain_product` builds each preceding-gain product
  incrementally instead of recomputing it per stage, giving linear time.
- **Stage count is just `stages.size()`.** There is no separate count parameter to keep in sync with
  the storage; adding or removing a stage is a one-line change at the call site.
- **Pure dB arithmetic.** This calculator pulls in only `emc::units::Decibel` / `to_ratio` and the
  error channel — no physical constants or material tables.

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — `emc::Result`, `emc::Error`/`ErrorCode`,
  `emc::invalid_input`, the `emc::units::Decibel`/`to_ratio` log wrappers, the `emc::Calculator`/
  `ValidatedCalculator` concepts, and the `emc::test::approx` helper.
- [`../03-quantities-and-units-mp-units.md`](../03-quantities-and-units-mp-units.md) §9 — why dB/dBm
  are typed log wrappers (`Decibel`/`Dbm`) and never linear mp-units units.
- [`../06-calculator-design-pattern.md`](../06-calculator-design-pattern.md) — the Input/Result/
  `calculate`/`validate` triple and the `std::span` cascade modeling.
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the test harness and
  tolerance policy.
