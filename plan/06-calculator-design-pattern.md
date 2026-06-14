# The Calculator Design Pattern

> The single, repeatable shape every one of the ~52 NinjaEMC calculators takes in the `emc`
> library: a typed `Input` aggregate, a typed `Result`, a `[[nodiscard]] std::expected<Result, Error>
> calculate(const Input&)` free function, an optional `validate()`, and a `Calculator` concept that
> binds the triple into a compile-checked contract. This document is the template you copy 52 times.

This is the **foundational** document of the plan. Docs
[07-calculator-inventory.md](07-calculator-inventory.md) (the work-list) and
[10-migration-roadmap.md](10-migration-roadmap.md) (the sequencing) both assume the pattern defined
here and refer back to the checklist in section 7. Read this before porting any calculator.

---

## 0. Why a pattern at all? (the problem we are escaping)

In the old app there is **no pattern**. Every formula lives inside a Qt `clicked` lambda in a widget
constructor. The math, the unit conversion, the material lookup, the validation, and the I/O are all
welded into one anonymous closure. Concretely, in
`src/BasicCalculations/SkinDepth/SkinDepthWidget.cpp` the entire skin-depth calculation is:

```cpp
// BEFORE — SkinDepthWidget.cpp, lines 64-72 (the whole "calculator")
connect(ui->solveButton, &RichButton::clicked, [this]() {
    qreal frequency = ui->frequency_spinbox->value() * ui->frequencyUnitBox->currentData().toReal();
    qreal conductivity = ui->conductivity_spinbox->value();
    qreal relativePermeability = ui->ur->value() * mu0;       // mu0 re-defined in this header
    qreal skinDepth = qSqrt(1 / (M_PI * frequency * relativePermeability * conductivity));
    ui->skinDepth->setValue(skinDepth / ui->skinDepth_unit->currentData().toReal());
});
```

Everything that is wrong with the codebase is visible in those five lines:

| Problem | Evidence in the snippet | Where the pattern fixes it |
|---|---|---|
| Math is not callable without a widget | the formula is inside a `clicked` lambda | free function `calculate()` |
| Inputs are read positionally from UI | `ui->frequency_spinbox->value()` | named `Input` aggregate fields |
| Units are bare `double` factors | `* ...currentData().toReal()` | mp-units typed quantities |
| `mu0` re-defined per file (8+ files) | `* mu0` | `emc::constants::mu_0` (one source) |
| Materials hardcoded inline | conductivity from a combobox `if/else` | `emc::materials` database |
| No error channel | silent garbage if `frequency == 0` | `std::expected<Result, Error>` |
| Untestable without driving the GUI | `ui->solveButton->clicked()` in tests | pure function + golden CSV |

The pattern below makes each calculator a **pure, typed, testable, reusable** unit. The Qt widget
becomes a dumb adapter: read fields → fill `Input` → call `calculate()` → render `Result` or show
the error. That is the entire goal of the rewrite, expressed as a code shape.

---

## 1. The canonical anatomy

Every calculator is **five named things** living in one category namespace (e.g. `emc::basic`,
`emc::component`, `emc::shielding`). For a calculator named `Foo`:

```
struct FooInput   { /* mp-units-typed fields, sensible defaults */ };
struct FooResult  { /* mp-units-typed fields */ };
[[nodiscard]] std::expected<void,   Error> validate(const FooInput&);   // (d) optional
[[nodiscard]] std::expected<FooResult, Error> calculate(const FooInput&); // (c) required
// (e) the (Input, Result, calculate) triple satisfies the Calculator concept
```

Header lives in `include/emc/<category>/foo.hpp`; the body of `calculate`/`validate` lives in
`src/<category>/foo.cpp`. (Library shape, install/export: see
[08-build-system-cmake.md](08-build-system-cmake.md). It is a compiled lib, not header-only.)

### (a) The `Input` aggregate struct

```cpp
// include/emc/basic/skin_depth.hpp
#pragma once
#include <emc/units.hpp>       // emc::units::* (mp-units vocabulary)  -> doc 03
#include <emc/error.hpp>       // emc::Error, emc::ErrorCode           -> doc 05
#include <emc/materials.hpp>   // emc::materials::Material             -> doc 04
#include <expected>

namespace emc::basic {

using namespace mp_units;                 // for the [unit] literal syntax in this header
using mp_units::si::unit_symbols::Hz;     // brought in for member-default literals

struct SkinDepthInput {
    quantity<isq::frequency[si::hertz]>                 frequency;
    quantity<isq::electrical_conductivity[si::siemens / si::metre]> conductivity;
    quantity<one>                                       relative_permeability{1 * one};
};

} // namespace emc::basic
```

Rules for `Input`:

* **It is an aggregate** (no user-declared constructors, no private members). That unlocks
  *designated initializers* at the call site (section 2) and *aggregate `==`*/structured bindings.
* **Every dimensional field is an mp-units `quantity`**, never a bare `double`. The dimension is part
  of the type, so `frequency = 3 * mm` does not compile. This is the mechanism that deletes the 541
  hand-wired `addItem(unit, factor)` conversions and the magic `* 39.37` constants (doc 03).
* **Sensible defaults** go on members that have an obvious neutral value. `relative_permeability{1}`
  is the non-magnetic default; a designer who omits it gets the common case. Required physical inputs
  (here `frequency`, `conductivity`) get *no* default so the compiler forces the caller to supply
  them — there is no silent zero.

> **Why an aggregate struct of named quantities, not function parameters?**
> The old code reads inputs positionally and untyped. With six `qreal` parameters
> `calc(double, double, double, double, double, double)` nobody can tell at the call site which is
> width and which is height — exactly the bug class that the MicrostripTrace 8-branch trees invite. A
> named aggregate makes every call self-documenting and makes adding a field a *source-compatible*
> change (old call sites still compile; the new field takes its default).

### (b) The `Result` struct

```cpp
struct SkinDepthResult {
    quantity<isq::length[si::metre]> skin_depth;
};
```

Rules for `Result`:

* Also an aggregate of mp-units quantities. The caller chooses the display unit at render time
  (`r.skin_depth.in(si::micro<si::metre>)`), so the library never bakes in a presentation unit — this
  is what replaces `ui->skinDepth_unit->currentData()` scaling.
* For **multi-output** calculators the Result simply has more fields (section 4).

### (c) `calculate` — the one required function

```cpp
[[nodiscard]] std::expected<SkinDepthResult, Error> calculate(const SkinDepthInput& in);
```

* Returns `std::expected<Result, Error>`: success carries the typed `Result`, failure carries an
  `emc::Error` (an `ErrorCode` plus context). This is the single error channel for the whole library;
  it replaces the scattered `QMessageBox::warning(...)` calls and the `return EXIT_FAILURE` sentinel
  in `StandardGaugeWireWidget.cpp`. See [05-error-handling-and-validation.md](05-error-handling-and-validation.md).
* `[[nodiscard]]` — ignoring the result is almost always a bug, because the result *is* the answer
  and *is* the error. The attribute makes "computed a skin depth and threw it away" a compiler
  warning.
* It is a **free function**, not a member. There is no object to construct, no state to manage, no
  vtable. (See section 5 on purity.)

### (d) `validate` — optional range checks, separable from the math

```cpp
[[nodiscard]] std::expected<void, Error> validate(const SkinDepthInput& in);
```

`validate` answers "are these inputs in the physically meaningful domain?" and returns
`expected<void, Error>` — `{}` on success, an `Error` on the first violation. It is *optional*: trivial
calculators may omit it and let `calculate` guard the few things it must. When present, `calculate`
calls `validate` first:

```cpp
auto calculate(const SkinDepthInput& in) -> std::expected<SkinDepthResult, Error> {
    if (auto ok = validate(in); !ok) return std::unexpected(ok.error());
    /* ...math... */
}
```

Keeping `validate` separate lets a UI **pre-flight** the inputs (grey out the Solve button, show an
inline hint) by calling `validate` alone, without committing to the calculation — something the old
inline `QMessageBox` checks, fused into the formula, could never offer.

### (e) The `Calculator` concept — the contract, enforced at compile time

The concept lives once, in `include/emc/calculator.hpp`, and binds the three pieces into a checkable
shape:

```cpp
// include/emc/calculator.hpp
#pragma once
#include <emc/error.hpp>
#include <concepts>
#include <expected>

namespace emc {

// A type that names a calculator's Input and Result and provides calculate(Input)->expected<Result>.
template <class C>
concept Calculator =
    requires {
        typename C::Input;
        typename C::Result;
    } &&
    requires (const typename C::Input& in) {
        // calculate must exist, take the Input by const ref, and return exactly expected<Result, Error>.
        { C::calculate(in) } -> std::same_as<std::expected<typename C::Result, Error>>;
    };

// Stronger variant: also requires a validate(Input) -> expected<void, Error>.
template <class C>
concept ValidatedCalculator =
    Calculator<C> &&
    requires (const typename C::Input& in) {
        { C::validate(in) } -> std::same_as<std::expected<void, Error>>;
    };

} // namespace emc
```

Because our calculators are **free functions in a namespace**, not classes, we bind them to the
concept with a tiny **trait/tag type** per calculator. This is a thin, zero-overhead descriptor — it
holds no data and is never instantiated at runtime; it exists only so generic code (test harnesses,
batch runners, future reflection) can name the triple as one entity:

```cpp
// at the bottom of include/emc/basic/skin_depth.hpp
namespace emc::basic {

struct SkinDepth {                       // tag: the calculator "as a type"
    using Input  = SkinDepthInput;
    using Result = SkinDepthResult;
    static std::expected<Result, Error> calculate(const Input& in) { return emc::basic::calculate(in); }
    static std::expected<void,   Error> validate (const Input& in) { return emc::basic::validate(in);  }
};
static_assert(emc::ValidatedCalculator<SkinDepth>); // contract checked at compile time, in the header

} // namespace emc::basic
```

> **Why a concept instead of a virtual base class / inheritance?**
> The old code has no abstraction at all, but the *naive* OO fix would be `class Calculator { virtual
> Result solve() = 0; };` — which forces heap allocation, a vtable, type erasure of the input/result,
> and runtime dispatch for math that is pure and known at the call site. The `Calculator` *concept*
> gives us the same "all calculators share a shape" guarantee **with zero runtime cost** and **better
> diagnostics**: a calculator whose `calculate` returns the wrong type fails the `static_assert` *in
> its own header* with a readable message, not at some distant generic call. It also lets us write one
> generic golden-vector runner (doc 09) that works for all 52 calculators:
> `template <emc::Calculator C> void run_golden(...)`.

> **Why `static_assert(Calculator<...>)` in the header?**
> It turns the contract into a build-time tripwire. If someone edits `SkinDepthResult` and forgets to
> update the tag, or changes `calculate`'s return type, the library *fails to compile at the point of
> the mistake*. This is the modern replacement for "hope the developer followed the convention."

---

## 2. Call-site ergonomics: designated initializers + unit literals

Because `Input` is an aggregate of mp-units quantities, calls read like the formula's variable list,
in any order, with units welded on:

```cpp
#include <emc/basic/skin_depth.hpp>
using namespace mp_units;
using namespace mp_units::si::unit_symbols;     // Hz, m, S, ...
using mp_units::si::unit_symbols::MHz;

auto r = emc::basic::calculate({
    .frequency            = 27 * MHz,
    .conductivity         = 1.4493e7 * (si::siemens / si::metre),
    .relative_permeability = 600 * one,          // Nickel
});
if (!r) { /* handle r.error() */ }
std::print("delta = {}\n", r->skin_depth.in(si::micro<si::metre>));
```

Contrast with the old positional, unit-erased reads:

```cpp
// BEFORE: which value is which? what unit? nobody can tell from the call.
qreal frequency = ui->frequency_spinbox->value() * ui->frequencyUnitBox->currentData().toReal();
qreal conductivity = ui->conductivity_spinbox->value();
qreal relativePermeability = ui->ur->value() * mu0;
```

Ergonomic wins, each tied to a real pain point:

* **Self-documenting** — `.frequency = 27 * MHz` names the field *and* the unit. The reader never has
  to know combobox ordering or which `currentData()` factor applies.
* **Order-independent** — designated initializers can be written in any order the author finds
  readable (C++20+; with our aggregates they may appear in declaration order — keep them in struct
  order to satisfy the standard).
* **Unit-safe by construction** — `27 * mm` for `frequency` is a *compile error*, not a wrong number
  silently fed into `qSqrt`. This is the literal mechanism that deletes the `* 39.37` scattered
  through `MicrostripTraceWidget.cpp` (doc 03).
* **Defaults disappear when unwanted** — a copper call can omit `relative_permeability` (defaults to
  `1 * one`); a nickel call supplies it. No overload explosion.

The Qt widget's job shrinks to a mechanical adapter (full example in section 6.A).

---

## 3. The bidirectional / solve-for-X pattern

`MicrostripTraceWidget` can solve for any one of **Z0, H, T, or W** given the other three. In the old
code this is four near-identical methods (`microstrip`, `calH`, `calT`, `calW`), each ~80 lines,
each containing the **same 8-branch nested `if` tree** that exists *only* to apply mm-vs-mils unit
conversions. The actual math in each is a single rearranged equation:

```text
Z0 = 87 * ln( 5.98*H / (0.8*W + T) ) / sqrt(eps + 1.41)        // solve Z0  (microstrip)
H  = exp( Z0*sqrt(eps+1.41)/87 ) * (0.8*W + T) / 5.98          // solve H    (calH)
T  = 5.98*H / exp( Z0*sqrt(eps+1.41)/87 ) - 0.8*W              // solve T    (calT)
W  = ( 5.98*H / exp( Z0*sqrt(eps+1.41)/87 ) - T ) / 0.8        // solve W    (calW)
```

Every `if (ui->hunitRadioMMButton->isChecked()) ... else ... * 39.37` branch is pure unit plumbing.
With mp-units, **all of it vanishes**: the quantities carry their units, conversions are implicit and
compile-checked, and `* 39.37` never appears.

### Recommended design: four typed solver functions (not an enum dispatch)

Provide one function per unknown, each with its own typed `Input`/`Result`:

```cpp
// include/emc/component/microstrip_trace.hpp
namespace emc::component {

using namespace mp_units;
using mp_units::quantity;

// --- Solve Z0 (and the derived C0, Tpd) from the geometry -------------------
struct MicrostripImpedanceInput {
    quantity<isq::height[si::metre]>    h;                 // substrate height
    quantity<isq::thickness[si::metre]> t;                 // trace thickness
    quantity<isq::width[si::metre]>     w;                 // trace width
    quantity<one>                       relative_permittivity;
};
struct MicrostripImpedanceResult {
    quantity<isq::resistance[si::ohm]>                   z0;
    quantity<isq::capacitance[si::pico<si::farad> / si::metre]> c0;   // per length
    quantity<isq::time[si::pico<si::second>] / si::metre>       tpd;  // per length
};
[[nodiscard]] std::expected<void, Error>
    validate(const MicrostripImpedanceInput&);
[[nodiscard]] std::expected<MicrostripImpedanceResult, Error>
    solve_impedance(const MicrostripImpedanceInput&);

// --- Solve H from Z0 + the other geometry -----------------------------------
struct MicrostripHeightInput {
    quantity<isq::resistance[si::ohm]>  z0;
    quantity<isq::thickness[si::metre]> t;
    quantity<isq::width[si::metre]>     w;
    quantity<one>                       relative_permittivity;
};
struct MicrostripHeightResult { quantity<isq::height[si::metre]> h; };
[[nodiscard]] std::expected<MicrostripHeightResult, Error>
    solve_height(const MicrostripHeightInput&);

// ... solve_thickness(MicrostripThicknessInput) -> {t}
// ... solve_width(MicrostripWidthInput)        -> {w}

} // namespace emc::component
```

> **Why four typed functions and not one `enum SolveFor` + a single entry point?**
> An `enum SolveFor { Z0, H, T, W }` plus a god-struct that has *every* field optional (because three
> are known and one is the unknown) re-introduces exactly the problem we are deleting: the caller can
> ask to solve for `H` while leaving `z0` unset, or solve for `Z0` while leaving `w` unset, and
> nothing catches it until runtime. With four typed functions, **the unknown is simply absent from the
> input type and present in the result type.** `solve_height`'s `Input` has no `h` field — you
> *cannot* forget to provide a known, and you *cannot* accidentally provide the unknown. The compiler
> enforces the four well-formed problems. This also gives each solver its own honest `[[nodiscard]]`
> result and its own precise `validate`. The enum approach trades compile-time safety for a single
> name; we prefer safety. (If a UI wants a single dispatch point, it writes a 4-line `switch` over
> *its own* radio-button state — that adapter belongs in the UI, not the library.)

The mp-units payoff, spelled out. The old `calH` is 86 lines because of the unit tree; the library
body is the bare equation:

```cpp
// src/component/microstrip_trace.cpp
auto solve_height(const MicrostripHeightInput& in)
    -> std::expected<MicrostripHeightResult, Error>
{
    using mp_units::si::unit_symbols::mm;
    const double eps = in.relative_permittivity.numerical_value_in(one);
    const double Z   = in.z0.numerical_value_in(si::ohm);
    // Geometry math is dimensionless ratios -> evaluate in a single coherent unit (mm), once.
    const double T   = in.t.numerical_value_in(mm);
    const double W   = in.w.numerical_value_in(mm);

    const double hln = (Z * std::sqrt(1.41 + eps)) / 87.0;
    const double H   = std::exp(hln) * (0.8 * W + T) / 5.98;   // result is in mm

    return MicrostripHeightResult{ .h = H * mm };               // unit reattached, caller converts
}
```

There is **no** `if (mm) ... else *39.37`. The caller may pass `h` in mils and `w` in mm; mp-units
reconciles them when it builds the `Input` and again when the caller reads `result.h.in(mils)`. The
four-method, thirty-two-branch tangle collapses to four short equations. (Numerical note: the
empirical microstrip formula is defined on ratios of lengths, so we evaluate it in one chosen
coherent unit — `mm` — to reproduce the original constants exactly. mp-units guarantees we *enter*
that unit correctly regardless of what the caller supplied.)

---

## 4. The multi-output pattern

`RectangularEnclosureWidget` returns **12 cavity-resonance frequencies**, one per (m, n, p) mode:
`f110, f101, f011, f111, f201, f120, f211, f210, f021, f220, f221, f121`. Each is the same equation

```text
f_mnp = (1.5e8 / sqrt(eps_r)) * sqrt( (m/L)^2 + (n/W)^2 + (p/H)^2 )
```

evaluated at a different `(m, n, p)`. The old code writes the formula out **twelve times** by hand
(lines 62-102), which is twelve chances to fumble a digit.

There are two ways to model "many outputs."

### Option A — named struct fields (good when the set is fixed and callers want names)

```cpp
struct CavityModeFreq { int m, n, p; quantity<isq::frequency[si::mega<si::hertz>]> f; };

struct RectangularEnclosureResult {
    std::array<CavityModeFreq, 12> modes;   // ordered exactly as the golden CSV columns
    // Convenience accessors so call sites can stay readable:
    [[nodiscard]] auto f110() const { return modes[0].f; }
    [[nodiscard]] auto f101() const { return modes[1].f; }
    // ...
};
```

### Option B — compute the modes with `std::ranges` over a `constexpr` mode table (recommended)

Define the 12 modes **once** as data, then compute all of them with **one** formula in a loop. The
formula appears a single time; adding a mode is adding a row, not copying an equation.

```cpp
// include/emc/shielding/rectangular_enclosure.hpp
namespace emc::shielding {

using namespace mp_units;

struct EnclosureInput {
    quantity<isq::length[si::metre]> length;     // L
    quantity<isq::width[si::metre]>  width;      // W
    quantity<isq::height[si::metre]> height;     // H
    quantity<one>                    relative_permittivity{1 * one};
};

struct Mode { int m, n, p; };                    // a resonance mode index triple

// The exact 12 modes, in the exact golden-CSV column order, declared ONCE:
inline constexpr std::array<Mode, 12> kModes{{
    {1,1,0},{1,0,1},{0,1,1},{1,1,1},{2,0,1},{1,2,0},
    {2,1,1},{2,1,0},{0,2,1},{2,2,0},{2,2,1},{1,2,1},
}};

struct ModeFreq { Mode mode; quantity<isq::frequency[si::mega<si::hertz>]> f; };
struct EnclosureResult { std::array<ModeFreq, kModes.size()> modes; };

[[nodiscard]] std::expected<void, Error>          validate(const EnclosureInput&);
[[nodiscard]] std::expected<EnclosureResult, Error> calculate(const EnclosureInput&);

} // namespace emc::shielding
```

```cpp
// src/shielding/rectangular_enclosure.cpp
auto calculate(const EnclosureInput& in) -> std::expected<EnclosureResult, Error> {
    if (auto ok = validate(in); !ok) return std::unexpected(ok.error());

    // c/2 in the original constant 1.5e8 m/s, expressed honestly via emc::constants -> doc 04.
    const auto k = emc::constants::c / 2.0 / std::sqrt(in.relative_permittivity.numerical_value_in(one));
    const auto L = in.length, W = in.width, H = in.height;

    EnclosureResult out{};
    std::ranges::transform(kModes, out.modes.begin(), [&](Mode md) -> ModeFreq {
        // (m/L)^2 + (n/W)^2 + (p/H)^2 — each term is 1/length^2, sum is 1/length^2.
        const auto inv2 = pow<2>(md.m / L) + pow<2>(md.n / W) + pow<2>(md.p / H);
        return { md, (k * sqrt(inv2)).in(si::mega<si::hertz>) };
    });
    return out;
}
```

> **Why a `constexpr` mode table + `std::ranges::transform`, not 12 hand-written lines?**
> The old file proves the hazard: twelve copies of one formula, each a separate transcription risk,
> and a 12-way `ui->fXYZ->setValue(...)` block to match. Modeling the *mode set* as data and the
> *formula* as code means the equation exists once and is impossible to get inconsistent across modes.
> `std::ranges::transform` over a `constexpr std::array` is allocation-free and (with `constexpr`
> `<cmath>` and constexpr-friendly mp-units) can even fold at compile time for fixed inputs.
> `kModes` is also the **single source of column order**, which we reuse directly in the golden test
> so the CSV and the code can never drift (doc 09).

> **`std::mdspan` callout.** A few calculators expose genuinely 2-D/3-D grids (e.g. a swept
> frequency × material table, or microstrip-current distribution over a cross-section). For those,
> return the data in a flat `std::vector` and hand callers a non-owning `std::mdspan<double,
> extents<...>>` view for `[i, j]` indexing — no nested `vector<vector<>>`, no manual stride math.
> The enclosure's 12 fixed modes do **not** need mdspan; a flat `std::array` is simpler and clearer.
> Reserve mdspan for true multi-dimensional sweeps.

Recommendation: **Option B**. Keep Option A's *named accessors* only where existing UI/tests refer to
results by name (`f110`), layered on top of the array so both styles work.

---

## 5. Purity and thread-safety

Every `calculate`/`validate`/`solve_*` is a **pure function**:

* **No state.** No member variables, no `static` locals that mutate, no singletons. Output depends
  only on the `Input`. (Contrast: the old widgets carry a `ui` pointer and mutate widget state.)
* **No I/O.** No file reads, no `qDebug`, no dialogs. The library never touches the screen or disk.
  Presentation and error display are the caller's job. (Contrast: `QMessageBox::warning` and the
  `TEST_MODE` CSV file writes baked into `MicrostripTraceWidget.cpp`.)
* **Reentrant / thread-safe by construction.** Because there is no shared mutable state, the same
  `calculate` can run on N threads over N inputs with zero locking. This is what makes a future
  `std::execution` / senders batch runner trivial (a C++26 forward-looking note in doc 02).
* **Constants are `constexpr`.** `emc::constants::*` are immutable compile-time values (doc 04), so
  there is no initialization-order or data-race concern around `mu_0`, `c`, etc. — and no per-file
  redefinition of `mu0` as in the old `SkinDepthWidget.h`/`FerriteToroidWidget.h`/... .

Why this matters concretely:

* **Testing.** A pure `calculate` is tested by calling it with an `Input` and comparing the `Result`
  — no widget, no event loop, no `ui->solveButton->clicked()`. The 52 golden CSVs become plain
  data-driven unit tests (doc 09).
* **Reuse.** The same function serves the Qt desktop app, a future CLI, a batch sweep, a web service
  — none of which can use a `QWidget`-bound lambda.
* **Reasoning.** A reviewer can verify one formula in isolation. The old code forces you to read Qt
  signal wiring to find the math.

---

## 6. Two full worked conversions

### 6.A — SkinDepth (simple) — end to end

**Formula:** δ = √( 1 / (π · f · μ · σ) ), with μ = μ₀ · μ_r.

#### BEFORE — the old Qt lambda (verbatim shape)

```cpp
// SkinDepthWidget.cpp 64-72 — math + units + materials + I/O, all fused
connect(ui->solveButton, &RichButton::clicked, [this]() {
    qreal frequency = ui->frequency_spinbox->value() * ui->frequencyUnitBox->currentData().toReal();
    qreal conductivity = ui->conductivity_spinbox->value();
    qreal relativePermeability = ui->ur->value() * mu0;   // mu0 redefined in SkinDepthWidget.h
    qreal skinDepth = qSqrt(1 / (M_PI * frequency * relativePermeability * conductivity));
    ui->skinDepth->setValue(skinDepth / ui->skinDepth_unit->currentData().toReal());
});
// material conductivity/permeability hardcoded in a combobox if/else (lines 42-61)
```

Bugs/smells inherited: `M_PI` (not the project's broken `PI 3.14`, but still ad-hoc); `mu0`
redefined per file; conductivity values copy-pasted from a 5-way `if/else`; no guard against
`frequency == 0` (→ divide-by-zero → `inf`); not callable without the widget.

#### AFTER — the complete new header

```cpp
// include/emc/basic/skin_depth.hpp
#pragma once
#include <emc/units.hpp>        // doc 03
#include <emc/error.hpp>        // doc 05
#include <emc/calculator.hpp>   // emc::Calculator concept
#include <expected>

namespace emc::basic {

using namespace mp_units;

struct SkinDepthInput {
    quantity<isq::frequency[si::hertz]>                              frequency;
    quantity<isq::electrical_conductivity[si::siemens / si::metre]> conductivity;
    quantity<one>                                                   relative_permeability{1 * one};
};

struct SkinDepthResult {
    quantity<isq::length[si::metre]> skin_depth;
};

[[nodiscard]] std::expected<void, Error>          validate(const SkinDepthInput& in);
[[nodiscard]] std::expected<SkinDepthResult, Error> calculate(const SkinDepthInput& in);

// Bind the triple to the concept (section 1e):
struct SkinDepth {
    using Input  = SkinDepthInput;
    using Result = SkinDepthResult;
    static auto calculate(const Input& in) { return emc::basic::calculate(in); }
    static auto validate (const Input& in) { return emc::basic::validate(in);  }
};
static_assert(emc::ValidatedCalculator<SkinDepth>);

} // namespace emc::basic
```

#### AFTER — the implementation (.cpp)

```cpp
// src/basic/skin_depth.cpp
#include <emc/basic/skin_depth.hpp>
#include <emc/constants.hpp>    // emc::constants::mu_0, emc::constants::pi  -> doc 04
#include <cmath>

namespace emc::basic {

using namespace mp_units;

std::expected<void, Error> validate(const SkinDepthInput& in) {
    if (in.frequency <= 0 * si::hertz)
        return std::unexpected(Error{ErrorCode::OutOfRange, "frequency must be > 0"});
    if (in.conductivity <= 0 * (si::siemens / si::metre))
        return std::unexpected(Error{ErrorCode::OutOfRange, "conductivity must be > 0"});
    if (in.relative_permeability <= 0 * one)
        return std::unexpected(Error{ErrorCode::OutOfRange, "relative permeability must be > 0"});
    return {};
}

std::expected<SkinDepthResult, Error> calculate(const SkinDepthInput& in) {
    if (auto ok = validate(in); !ok) return std::unexpected(ok.error());

    const auto mu    = in.relative_permeability * emc::constants::mu_0;     // H/m, typed
    const auto denom = emc::constants::pi * in.frequency * mu * in.conductivity; // -> 1/length^2

    // sqrt of a quantity yields the correctly-dimensioned quantity (length). No bare doubles.
    const auto delta = sqrt(1.0 / denom);
    return SkinDepthResult{ .skin_depth = delta.in(si::metre) };
}

} // namespace emc::basic
```

Notes on the conversion:

* `emc::constants::pi` (full precision) replaces `M_PI`/`PI 3.14`. `emc::constants::mu_0` (one typed
  source) replaces the per-file `qreal mu0 = 4*M_PI*1e-7`.
* The dimensional algebra checks out at compile time: `[H/m]·[Hz]·[S/m] = 1/m²`, so `sqrt(1/denom)`
  *is* a length. If the formula were wrong dimensionally, it would not compile — a guardrail the old
  `qreal` math never had.
* `validate` turns the silent divide-by-zero into an explicit `OutOfRange` error.
* **Material lookup is not the calculator's job.** Conductivity/permeability come from
  `emc::materials` (doc 04). The caller looks up the material and fills the `Input`:

```cpp
auto m  = emc::materials::lookup(emc::materials::Material::Nickel); // -> {conductivity, mu_r, ...}
auto r  = emc::basic::calculate({ .frequency = 27 * MHz,
                                  .conductivity = m.conductivity,
                                  .relative_permeability = m.relative_permeability });
```

#### How a Qt widget calls it (the adapter)

```cpp
connect(ui->solveButton, &RichButton::clicked, [this]() {
    using namespace mp_units; using namespace mp_units::si::unit_symbols;
    auto mat = emc::materials::lookup_by_name(ui->material_combobox->currentText().toStdString());
    if (!mat) { showError(mat.error()); return; }

    auto r = emc::basic::calculate({
        .frequency             = ui->frequency_spinbox->value()
                               * unitFromBox(ui->frequencyUnitBox),   // returns a frequency quantity
        .conductivity          = mat->conductivity,
        .relative_permeability = mat->relative_permeability,
    });
    if (!r) { showError(r.error()); return; }                          // replaces QMessageBox-in-math
    ui->skinDepth->setValue(r->skin_depth.numerical_value_in(unitFromBox(ui->skinDepth_unit)));
});
```

The widget now contains **zero domain math** — only marshalling. That is the target end state for all
~52 leaf widgets.

#### Golden vector

`resources/data/SkinDepthWidget.csv` rows are `frequency(MHz),material`, e.g. `27,Nickel`. These feed
the pure `calculate` directly in a data-driven test (doc 09). **Re-blessing note:** the fixture
contains the misspelling `Cooper` for `Copper` (e.g. row `84,Cooper`); the materials database must map
or reject it, and any golden output produced through the old broken constants may need recomputation
before it is trusted as the reference.

---

### 6.B — MicrostripTrace (complex, bidirectional, validated) — end to end

**Formulas** (one rearrangement per unknown), with derived outputs C0 and Tpd for the Z0 case:

```text
Z0  = 87 * ln(5.98*H / (0.8*W + T)) / sqrt(eps + 1.41)
C0  = 0.67 * (eps + 1.41) / ln(5.98*H / (0.8*W + T))                 // pF/cm (or /2.54 for /inch)
Tpd = C0_temp * Z0                                                    // psec/cm (or /2.54 for /inch)
H   = exp(Z0*sqrt(eps+1.41)/87) * (0.8*W + T) / 5.98
T   = 5.98*H / exp(Z0*sqrt(eps+1.41)/87) - 0.8*W
W   = (5.98*H / exp(Z0*sqrt(eps+1.41)/87) - T) / 0.8
```

**Validation domain** (from the old inline checks): `1 ≤ eps ≤ 15`, `0.1 ≤ W/H ≤ 3`, and all of
`H, W, T, Z0 > 0`.

#### BEFORE — the shape of the old code (one of four near-identical methods)

```cpp
// MicrostripTraceWidget::calH() — 86 lines; the 8 leaves differ ONLY in *39.37 placement
void MicrostripTraceWidget::calH() {
    qreal eps, H, T, W, Z, ...;
    if (ui->hunitRadioMMButton->isChecked()) {
      if (ui->tunitRadioMMButton->isChecked()) {
        if (ui->wunitRadioMMButton->isChecked()) {
            T = ui->t_lineEdit->text().toDouble();          // mm
            W = ui->w_lineEdit->text().toDouble();          // mm
            ...
            H = qExp(hln) * (0.8 * W + T) / 5.98;
        } else { /* W in mils -> *39.37 ... same formula */ }
      } else { /* T mils ... */ }
    } else { /* H mils ... another whole subtree, divides H by 39.37 ... */ }
    if ((eps < 1) || (eps > 15) || ((W/htemp) < 0.1) || ... ) {
        QMessageBox::warning(this, "Warning", "Please check your input!");
        return;
    } else ui->h_lineEdit->setValue((H*10)/10);
}
// calT(), calW(), microstrip() repeat the SAME 8-branch tree with a different equation.
```

Four methods × eight branches = thirty-two leaves of pure unit plumbing wrapped around four
one-line equations, plus four copies of the same validation block ending in `QMessageBox`.

#### AFTER — the header (four typed solvers)

```cpp
// include/emc/component/microstrip_trace.hpp
#pragma once
#include <emc/units.hpp>
#include <emc/error.hpp>
#include <emc/calculator.hpp>
#include <expected>

namespace emc::component {

using namespace mp_units;

// ---- Solve Z0 (+ C0, Tpd) from full geometry --------------------------------
struct MicrostripImpedanceInput {
    quantity<isq::height[si::metre]>    h;
    quantity<isq::thickness[si::metre]> t;
    quantity<isq::width[si::metre]>     w;
    quantity<one>                       relative_permittivity{4.7 * one};
};
struct MicrostripImpedanceResult {
    quantity<isq::resistance[si::ohm]>                          z0;
    quantity<isq::capacitance[si::pico<si::farad>] / si::metre> c0;   // SI per-length; caller -> pF/cm
    quantity<isq::time[si::pico<si::second>] / si::metre>       tpd;  // SI per-length; caller -> psec/cm
};
[[nodiscard]] std::expected<void, Error>                       validate(const MicrostripImpedanceInput&);
[[nodiscard]] std::expected<MicrostripImpedanceResult, Error>  solve_impedance(const MicrostripImpedanceInput&);

// ---- Solve H ---------------------------------------------------------------
struct MicrostripHeightInput {
    quantity<isq::resistance[si::ohm]>  z0;
    quantity<isq::thickness[si::metre]> t;
    quantity<isq::width[si::metre]>     w;
    quantity<one>                       relative_permittivity{4.7 * one};
};
struct MicrostripHeightResult { quantity<isq::height[si::metre]> h; };
[[nodiscard]] std::expected<MicrostripHeightResult, Error> solve_height(const MicrostripHeightInput&);

// ---- Solve T ---------------------------------------------------------------
struct MicrostripThicknessInput {
    quantity<isq::resistance[si::ohm]> z0;
    quantity<isq::height[si::metre]>   h;
    quantity<isq::width[si::metre]>    w;
    quantity<one>                      relative_permittivity{4.7 * one};
};
struct MicrostripThicknessResult { quantity<isq::thickness[si::metre]> t; };
[[nodiscard]] std::expected<MicrostripThicknessResult, Error> solve_thickness(const MicrostripThicknessInput&);

// ---- Solve W ---------------------------------------------------------------
struct MicrostripWidthInput {
    quantity<isq::resistance[si::ohm]>  z0;
    quantity<isq::height[si::metre]>    h;
    quantity<isq::thickness[si::metre]> t;
    quantity<one>                       relative_permittivity{4.7 * one};
};
struct MicrostripWidthResult { quantity<isq::width[si::metre]> w; };
[[nodiscard]] std::expected<MicrostripWidthResult, Error> solve_width(const MicrostripWidthInput&);

// The primary (Z0) triple satisfies the concept; the three solvers are siblings.
struct MicrostripImpedance {
    using Input  = MicrostripImpedanceInput;
    using Result = MicrostripImpedanceResult;
    static auto calculate(const Input& in) { return solve_impedance(in); }
    static auto validate (const Input& in) { return emc::component::validate(in); }
};
static_assert(emc::ValidatedCalculator<MicrostripImpedance>);

} // namespace emc::component
```

Notice what is *gone from the type system itself*: `solve_height`'s input has **no `h` field**. You
cannot ask to solve for height and accidentally also pass a height. The 8-branch unit tree is gone
because units live in the quantities.

#### AFTER — the implementation (.cpp)

```cpp
// src/component/microstrip_trace.cpp
#include <emc/component/microstrip_trace.hpp>
#include <cmath>

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::mm;

namespace {
// Shared domain check (replaces the four copy-pasted validation blocks + QMessageBox).
std::expected<void, Error> check_domain(double eps, double H_mm, double T_mm,
                                        double W_mm, double Z_ohm) {
    if (eps < 1.0 || eps > 15.0)
        return std::unexpected(Error{ErrorCode::OutOfRange, "relative permittivity must be in [1, 15]"});
    if (H_mm <= 0 || W_mm <= 0 || T_mm <= 0 || Z_ohm <= 0)
        return std::unexpected(Error{ErrorCode::OutOfRange, "H, W, T, Z0 must be > 0"});
    const double wh = W_mm / H_mm;
    if (wh < 0.1 || wh > 3.0)
        return std::unexpected(Error{ErrorCode::OutOfRange, "W/H must be in [0.1, 3]"});
    return {};
}
} // namespace

std::expected<void, Error> validate(const MicrostripImpedanceInput& in) {
    return check_domain(in.relative_permittivity.numerical_value_in(one),
                        in.h.numerical_value_in(mm), in.t.numerical_value_in(mm),
                        in.w.numerical_value_in(mm), /*Z0 unknown here*/ 1.0);
}

std::expected<MicrostripImpedanceResult, Error>
solve_impedance(const MicrostripImpedanceInput& in) {
    const double eps = in.relative_permittivity.numerical_value_in(one);
    const double H   = in.h.numerical_value_in(mm);   // evaluate the empirical formula in mm
    const double T   = in.t.numerical_value_in(mm);
    const double W   = in.w.numerical_value_in(mm);

    const double Z = 87.0 * std::log(5.98 * H / (0.8 * W + T)) / std::sqrt(eps + 1.41);
    if (auto ok = check_domain(eps, H, T, W, Z); !ok) return std::unexpected(ok.error());

    const double c0_pf_per_cm  = 0.67 * (eps + 1.41) / std::log(5.98 * H / (0.8 * W + T));
    const double tpd_ps_per_cm = c0_pf_per_cm * Z;

    using namespace mp_units::si::unit_symbols;
    return MicrostripImpedanceResult{
        .z0  = Z * ohm,
        .c0  = (c0_pf_per_cm  * pF / (1.0 * cm)).in(si::pico<si::farad> / si::metre),
        .tpd = (tpd_ps_per_cm * ps / (1.0 * cm)).in(si::pico<si::second> / si::metre),
    };
}

std::expected<MicrostripHeightResult, Error>
solve_height(const MicrostripHeightInput& in) {
    const double eps = in.relative_permittivity.numerical_value_in(one);
    const double Z   = in.z0.numerical_value_in(si::ohm);
    const double T   = in.t.numerical_value_in(mm);
    const double W   = in.w.numerical_value_in(mm);

    const double hln = (Z * std::sqrt(1.41 + eps)) / 87.0;
    const double H   = std::exp(hln) * (0.8 * W + T) / 5.98;     // mm
    if (auto ok = check_domain(eps, H, T, W, Z); !ok) return std::unexpected(ok.error());
    return MicrostripHeightResult{ .h = H * mm };
}

std::expected<MicrostripThicknessResult, Error>
solve_thickness(const MicrostripThicknessInput& in) {
    const double eps = in.relative_permittivity.numerical_value_in(one);
    const double Z   = in.z0.numerical_value_in(si::ohm);
    const double H   = in.h.numerical_value_in(mm);
    const double W   = in.w.numerical_value_in(mm);

    const double tln = (Z * std::sqrt(1.41 + eps)) / 87.0;
    const double T   = (5.98 * H) / std::exp(tln) - 0.8 * W;     // mm
    if (auto ok = check_domain(eps, H, T, W, Z); !ok) return std::unexpected(ok.error());
    return MicrostripThicknessResult{ .t = T * mm };
}

std::expected<MicrostripWidthResult, Error>
solve_width(const MicrostripWidthInput& in) {
    const double eps = in.relative_permittivity.numerical_value_in(one);
    const double Z   = in.z0.numerical_value_in(si::ohm);
    const double H   = in.h.numerical_value_in(mm);
    const double T   = in.t.numerical_value_in(mm);

    const double wln = (Z * std::sqrt(1.41 + eps)) / 87.0;
    const double W   = ((5.98 * H) / std::exp(wln) - T) / 0.8;   // mm
    if (auto ok = check_domain(eps, H, T, W, Z); !ok) return std::unexpected(ok.error());
    return MicrostripWidthResult{ .w = W * mm };
}

} // namespace emc::component
```

What the conversion accomplishes, point by point:

* **32 unit branches → 0.** Each `.numerical_value_in(mm)` pulls the value into the formula's chosen
  unit regardless of what the caller supplied (mm, mils, inches). The old `* 39.37` and the
  `if (...MMButton->isChecked())` ladders are deleted entirely (doc 03).
* **4 copies of validation → 1 `check_domain`.** And it returns an `Error`, not a `QMessageBox` —
  the math no longer talks to the GUI (doc 05).
* **The unknown is encoded in the types.** `solve_width` cannot be called with a width; `solve_height`
  cannot be called without `z0`. Whole classes of "wrong field set" bugs are unrepresentable.
* **Per-length outputs carry their dimension.** `c0` and `tpd` are real per-length quantities; the
  caller chooses `pF/cm` vs `pF/inch` at render time, which replaces the
  `if (!ui->c0PfcmradioButton->isChecked()) ... /2.54` branching.

#### How a Qt widget calls it (bidirectional adapter)

```cpp
// The UI's four buttons map to the four solvers. The library does not know about radio buttons.
connect(ui->h_button, &QPushButton::clicked, [this]() {
    using namespace mp_units; using namespace mp_units::si::unit_symbols;
    auto r = emc::component::solve_height({
        .z0 = ui->z0_lineEdit->value() * ohm,
        .t  = ui->t_lineEdit->value()  * lengthUnit(ui->tunitRadioMMButton),   // mm or mils
        .w  = ui->w_lineEdit->value()  * lengthUnit(ui->wunitRadioMMButton),
        .relative_permittivity = ui->permittivity_LineEdit->value() * one,
    });
    if (!r) { showError(r.error()); return; }     // one error path, no inline QMessageBox in math
    ui->h_lineEdit->setValue(r->h.numerical_value_in(lengthUnit(ui->hunitRadioMMButton)));
});
// z0_button -> solve_impedance, t_button -> solve_thickness, w_button -> solve_width (same shape).
```

#### Golden vector

`resources/data/MicrostripTraceWidget.csv` rows are `h,t,w,relativePermittivity` (mm), e.g.
`18.65392418,11.15918402,15.00320319,5`, with the golden output columns
`Z0, C0(pF/cm), Tpd(psec/cm)` — these exercise `solve_impedance`. Reverse rows (where one geometry
value is omitted and `z0` is given) exercise `solve_height/thickness/width`. See doc 09 for the
data-driven harness; rows that violate `W/H ∈ [0.1,3]` should be asserted to return `OutOfRange`
rather than a number.

---

## 7. Implementer checklist — convert any one calculator

Follow these steps in order for each of the ~52 leaves. This is the procedure
[10-migration-roadmap.md](10-migration-roadmap.md) sequences and tracks.

1. **Locate the formula.** Open the source widget `.cpp` listed in
   [07-calculator-inventory.md](07-calculator-inventory.md). Find the `clicked` lambda or `cal*`
   method. Copy out the raw equation(s) and note every magic constant, `* factor` unit conversion,
   inline `if/else` material table, and `QMessageBox` range check.

2. **Pick the namespace & files.** Place it in the right category namespace
   (`emc::basic`/`component`/`shielding`/...). Create `include/emc/<category>/<name>.hpp` and
   `src/<category>/<name>.cpp`.

3. **Define `Input`.** One mp-units-typed field per independent variable, in a natural order. Add
   `{default}` only for fields with an obvious neutral value (relative permeability `1`, relative
   permittivity a common substrate value). No defaults on the required physical inputs. Map each old
   `ui->...->value() * factor` to a typed quantity (doc 03 for the unit vocabulary).

4. **Define `Result`.** One mp-units-typed field per output. For multi-output, prefer a `constexpr`
   index table + `std::array` (section 4). Do **not** bake in a display unit.

5. **Port the math** into `calculate` (or `solve_*` for bidirectional). Replace:
   `M_PI`/`PI 3.14` → `emc::constants::pi`; `mu0`/`SPEEDOFLIGHT`/`PLANCK_CONSTANT` →
   `emc::constants::*` (doc 04); inline material tables → `emc::materials::lookup` (doc 04);
   `* 39.37` and `addItem(unit,factor)` scaling → `.numerical_value_in(unit)` / quantity literals
   (doc 03). Evaluate empirical (dimensionless-ratio) formulas in one chosen coherent unit to
   reproduce the original constants exactly.

6. **Add `validate`.** Translate every `QMessageBox::warning(...)` range check and every
   `EXIT_FAILURE` sentinel into `return std::unexpected(Error{ErrorCode::OutOfRange, "..."})`.
   Guard divide-by-zero / domain-of-log / domain-of-sqrt explicitly. Call `validate` first inside
   `calculate` (doc 05).

7. **Bind the concept.** Add the tag `struct <Name> { using Input; using Result; static calculate;
   static validate; };` and `static_assert(emc::Calculator<Name>)` (or `ValidatedCalculator`) in the
   header. Fix any mismatch the assert reports.

8. **Wire the golden CSV test.** Point a data-driven test at
   `resources/data/<Widget>.csv`, parse the input columns into the `Input`, run `calculate`, and
   compare each output column within tolerance (doc 09). Mark rows that must error as expected
   `OutOfRange`. **Re-bless** any golden output that the old code computed through a known bug
   (`PI 3.14`, imprecise `SPEEDOFLIGHT`) and record the change.

9. **(Optional) Add the widget adapter.** Once the app is being rewired (doc 08), replace the old
   lambda body with the marshalling adapter (sections 6.A/6.B): read fields → build `Input` → call
   `calculate` → render `Result` or `showError(error)`. The widget must contain **no domain math**.

10. **Done criteria.** The calculator compiles with the `static_assert` passing, the golden test is
    green (or intentionally re-blessed), `calculate`/`validate` are pure (no I/O, no globals), and no
    `M_PI`/`mu0`/`* 39.37`/`QMessageBox` remains in the ported math.

---

## Cross-references

* [00-overview-and-goals.md](00-overview-and-goals.md) — vision, before/after, success criteria.
* [01-architecture-and-layout.md](01-architecture-and-layout.md) — namespaces, directory/lib layout.
* [02-modern-cpp-feature-catalog.md](02-modern-cpp-feature-catalog.md) — concepts, `std::expected`,
  designated initializers, ranges, mdspan rationale in depth.
* [03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md) — the mp-units vocabulary
  that types the `Input`/`Result` fields and deletes the 541 unit conversions.
* [04-constants-and-material-database.md](04-constants-and-material-database.md) — `emc::constants::*`
  and `emc::materials::*` used inside every `calculate`.
* [05-error-handling-and-validation.md](05-error-handling-and-validation.md) — `emc::Error`,
  `ErrorCode`, and the `std::expected` conventions used by `validate`/`calculate`.
* [07-calculator-inventory.md](07-calculator-inventory.md) — the full ~52-calculator work-list that
  applies this pattern.
* [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) — the generic
  `template <emc::Calculator C>` golden-CSV harness this pattern enables.
* [10-migration-roadmap.md](10-migration-roadmap.md) — phasing that reuses the section 7 checklist.
