# The Calculator Design Pattern 🧮

> The single, repeatable shape every one of the ~52 `emc` calculators takes: a typed `Input`
> aggregate, a typed `Result`, a `[[nodiscard]] std::expected<Result, Error> calculate(const Input&)`
> free function, an optional `validate()`, and a `Calculator` concept that binds the triple into a
> compile-checked contract. This is the template you copy 52 times.

Docs [07-calculator-inventory.md](07-calculator-inventory.md) (the work-list) and
[10-build-roadmap.md](10-build-roadmap.md) (the sequencing) both assume the pattern defined here and
refer back to the checklist in section 6.

---

## 1. The canonical anatomy

Every calculator is **five named things** living in one category namespace (e.g. `emc::basic`,
`emc::component`, `emc::shielding`). For a calculator named `Foo`:

```text
struct FooInput   { /* mp-units-typed fields, sensible defaults */ };
struct FooResult  { /* mp-units-typed fields */ };
[[nodiscard]] std::expected<void,      Error> validate(const FooInput&);    // (d) optional
[[nodiscard]] std::expected<FooResult, Error> calculate(const FooInput&);   // (c) required
// (e) the (Input, Result, calculate) triple satisfies the Calculator concept
```

Header lives in `include/emc/<category>/foo.hpp`; the body of `calculate`/`validate` lives in
`src/<category>/foo.cpp`. It is a compiled library, not header-only (install/export shape:
[08-build-system-cmake.md](08-build-system-cmake.md)).

### (a) The `Input` aggregate struct

```c++
// include/emc/basic/skin_depth.hpp
#pragma once
#include <emc/units.hpp>       // emc::units::* (mp-units vocabulary)  -> doc 03
#include <emc/error.hpp>       // emc::Error, emc::ErrorCode           -> doc 05
#include <emc/materials.hpp>   // emc::materials::Material             -> doc 04
#include <expected>

namespace emc::basic {

using namespace mp_units;

struct SkinDepthInput {
    quantity<isq::frequency[si::hertz]>                             frequency;
    quantity<isq::electrical_conductivity[si::siemens / si::metre]> conductivity;
    quantity<one>                                                  relative_permeability{1 * one};
};

} // namespace emc::basic
```

Rules for `Input`:

* **It is an aggregate** (no user-declared constructors, no private members). That unlocks
  *designated initializers* at the call site (section 2) plus aggregate `==` and structured bindings.
* **Every dimensional field is an mp-units `quantity`**, never a bare `double`. The dimension is part
  of the type, so `frequency = 3 * mm` does not compile.
* **Sensible defaults** go on members with an obvious neutral value: `relative_permeability{1}` is
  the non-magnetic default. Required physical inputs (`frequency`, `conductivity`) get *no* default,
  so the compiler forces the caller to supply them — there is no silent zero.

> [!NOTE]
> **Why an aggregate of named quantities, not positional parameters?**
> EMC formulas routinely take 4–8 length/impedance arguments; a `calc(double, double, double, ...)`
> signature hides which argument is width and which is height. A named aggregate makes every call
> self-documenting and makes adding a field a *source-compatible* change — old call sites still
> compile, and the new field takes its default.

### (b) The `Result` struct

```c++
struct SkinDepthResult {
    quantity<isq::length[si::metre]> skin_depth;
};
```

Rules for `Result`:

* Also an aggregate of mp-units quantities. The caller chooses the display unit at render time
  (`r.skin_depth.in(si::micro<si::metre>)`), so the library never bakes in a presentation unit.
* For **multi-output** calculators the `Result` simply has more fields (section 4).

### (c) `calculate` — the one required function

```c++
[[nodiscard]] std::expected<SkinDepthResult, Error> calculate(const SkinDepthInput& in);
```

* Returns `std::expected<Result, Error>`: success carries the typed `Result`, failure carries an
  `emc::Error` (an `ErrorCode` plus context). This is the single error channel for the whole library
  (see [05-error-handling-and-validation.md](05-error-handling-and-validation.md)).
* `[[nodiscard]]` — ignoring the result is almost always a bug, because the result *is* the answer
  and *is* the error.
* It is a **free function**, not a member: no object to construct, no state to manage, no vtable
  (section 5).

### (d) `validate` — optional range checks, separable from the math

```c++
[[nodiscard]] std::expected<void, Error> validate(const SkinDepthInput& in);
```

`validate` answers "are these inputs in the physically meaningful domain?" and returns
`expected<void, Error>` — `{}` on success, an `Error` on the first violation. It is *optional*:
trivial calculators may omit it and let `calculate` guard the few things it must. When present,
`calculate` calls `validate` first:

```c++
auto calculate(const SkinDepthInput& in) -> std::expected<SkinDepthResult, Error> {
    if (auto ok = validate(in); !ok) return std::unexpected(ok.error());
    /* ...math... */
}
```

Keeping `validate` separate lets a front end **pre-flight** the inputs (disable the solve action,
show an inline hint) by calling `validate` alone, without committing to the calculation.

### (e) The `Calculator` concept — the contract, enforced at compile time

The concept lives once, in `include/emc/calculator.hpp`, and binds the three pieces into a checkable
shape:

```c++
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

Because the calculators are **free functions in a namespace**, not classes, we bind them to the
concept with a tiny **trait/tag type** per calculator — a thin, zero-overhead descriptor that holds
no data and is never instantiated at runtime. It exists only so generic code (test harnesses, batch
runners, future reflection) can name the triple as one entity:

```c++
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

> [!TIP]
> **Why a concept instead of a virtual base class?**
> An OO base `class Calculator { virtual Result solve() = 0; }` forces heap allocation, a vtable,
> type erasure of the input/result, and runtime dispatch — for math that is pure and known at the
> call site. The `Calculator` *concept* gives the same "all calculators share a shape" guarantee
> with **zero runtime cost** and **better diagnostics**: a calculator whose `calculate` returns the
> wrong type fails the `static_assert` *in its own header* with a readable message. It also lets us
> write one generic golden-vector runner that works for all 52 calculators:
> `template <emc::Calculator C> void run_golden(...)` (doc 09).

The header `static_assert(Calculator<...>)` turns the contract into a build-time tripwire: change a
`Result` field or a `calculate` return type and forget to update the tag, and the library *fails to
compile at the point of the mistake*.

---

## 2. Call-site ergonomics: designated initializers + unit literals

Because `Input` is an aggregate of mp-units quantities, calls read like the formula's variable list,
with units welded on:

```c++
#include <emc/basic/skin_depth.hpp>
using namespace mp_units;
using namespace mp_units::si::unit_symbols;     // Hz, m, S, ...
using mp_units::si::unit_symbols::MHz;

auto r = emc::basic::calculate({
    .frequency             = 27 * MHz,
    .conductivity          = 1.4493e7 * (si::siemens / si::metre),
    .relative_permeability = 600 * one,          // Nickel
});
if (!r) { /* handle r.error() */ }
std::print("delta = {}\n", r->skin_depth.in(si::micro<si::metre>));
```

> [!IMPORTANT]
> **Modern C++ features used here / and why**
> * **Designated initializers** (C++20) — `.frequency = 27 * MHz` names the field *and* the unit, so
>   a call is self-documenting; keep fields in declaration order to satisfy the standard.
> * **mp-units typed quantities** — EMC inputs span Hz..GHz and m..mils, so compile-time unit safety
>   matters: `27 * mm` for a `frequency` field is a *compile error*, not a wrong number silently fed
>   into `sqrt`.
> * **Defaults on neutral fields** — a copper call can omit `relative_permeability` (defaults to
>   `1 * one`); a nickel call supplies it. No overload explosion.

A generic front end (printing with `std::print`, a CLI, a service) does the marshalling: read inputs
→ fill `Input` → call `calculate` → render `Result` or report the error (full example in section 6).

---

## 3. The bidirectional / solve-for-X pattern

A microstrip trace can solve for any one of **Z0, H, T, or W** given the other three. Each is a
single rearrangement of one impedance equation:

```text
Z0 = 87 * ln( 5.98*H / (0.8*W + T) ) / sqrt(eps + 1.41)        // solve Z0
H  = exp( Z0*sqrt(eps+1.41)/87 ) * (0.8*W + T) / 5.98          // solve H
T  = 5.98*H / exp( Z0*sqrt(eps+1.41)/87 ) - 0.8*W              // solve T
W  = ( 5.98*H / exp( Z0*sqrt(eps+1.41)/87 ) - T ) / 0.8        // solve W
```

These are standard closed-form microstrip impedance relations. With mp-units the quantities carry
their units and conversions are implicit and compile-checked.

### Design: four typed solver functions (not an enum dispatch)

Provide one function per unknown, each with its own typed `Input`/`Result`:

```c++
// include/emc/component/microstrip_trace.hpp
namespace emc::component {

using namespace mp_units;

// --- Solve Z0 (and the derived C0, Tpd) from the geometry -------------------
struct MicrostripImpedanceInput {
    quantity<isq::height[si::metre]>    h;                 // substrate height
    quantity<isq::thickness[si::metre]> t;                 // trace thickness
    quantity<isq::width[si::metre]>     w;                 // trace width
    quantity<one>                       relative_permittivity;
};
struct MicrostripImpedanceResult {
    quantity<isq::resistance[si::ohm]>                          z0;
    quantity<isq::capacitance[si::pico<si::farad>] / si::metre> c0;   // per length
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
// ... solve_width(MicrostripWidthInput)         -> {w}

} // namespace emc::component
```

> [!NOTE]
> **Why four typed functions and not one `enum SolveFor` + a single entry point?**
> An `enum SolveFor { Z0, H, T, W }` plus a god-struct with *every* field optional (three known, one
> unknown) lets the caller ask to solve for `H` while leaving `z0` unset, with nothing caught until
> runtime. With four typed functions, **the unknown is simply absent from the input type and present
> in the result type.** `solve_height`'s `Input` has no `h` field — you *cannot* forget to provide a
> known, and you *cannot* accidentally provide the unknown. Each solver also gets its own honest
> `[[nodiscard]]` result and its own precise `validate`. A front end that wants a single dispatch
> point writes a 4-line `switch` over *its own* state — that adapter belongs in the front end, not
> the library.

The geometry math is on dimensionless ratios of lengths, so the body evaluates it in one chosen
coherent unit (`mm`) to reproduce the standard constants exactly; mp-units guarantees we *enter* that
unit correctly regardless of what the caller supplied:

```c++
// src/component/microstrip_trace.cpp
auto solve_height(const MicrostripHeightInput& in)
    -> std::expected<MicrostripHeightResult, Error>
{
    using mp_units::si::unit_symbols::mm;
    const double eps = in.relative_permittivity.numerical_value_in(one);
    const double Z   = in.z0.numerical_value_in(si::ohm);
    const double T   = in.t.numerical_value_in(mm);
    const double W   = in.w.numerical_value_in(mm);

    const double hln = (Z * std::sqrt(1.41 + eps)) / 87.0;
    const double H   = std::exp(hln) * (0.8 * W + T) / 5.98;   // result is in mm

    return MicrostripHeightResult{ .h = H * mm };               // unit reattached, caller converts
}
```

The caller may pass `h` in mils and `w` in mm; mp-units reconciles them when it builds the `Input`
and again when the caller reads `result.h.in(mils)`. Four short equations, no unit branching.

---

## 4. The multi-output pattern

A rectangular enclosure returns **12 cavity-resonance frequencies**, one per (m, n, p) mode. Each is
the same equation evaluated at a different `(m, n, p)`:

```text
f_mnp = (c/2 / sqrt(eps_r)) * sqrt( (m/L)^2 + (n/W)^2 + (p/H)^2 )
```

There are two ways to model "many outputs."

### Option A — named struct fields (good when the set is fixed and callers want names)

```c++
struct CavityModeFreq { int m, n, p; quantity<isq::frequency[si::mega<si::hertz>]> f; };

struct RectangularEnclosureResult {
    std::array<CavityModeFreq, 12> modes;   // ordered to match the reference column order
    // Convenience accessors so call sites can stay readable:
    [[nodiscard]] auto f110() const { return modes[0].f; }
    [[nodiscard]] auto f101() const { return modes[1].f; }
    // ...
};
```

### Option B — compute the modes with `std::ranges` over a `constexpr` mode table (recommended)

Define the 12 modes **once** as data, then compute all of them with **one** formula in a loop. The
formula appears a single time; adding a mode is adding a row, not copying an equation.

```c++
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

// The 12 modes, in a fixed reference column order, declared ONCE:
inline constexpr std::array<Mode, 12> kModes{{
    {1,1,0},{1,0,1},{0,1,1},{1,1,1},{2,0,1},{1,2,0},
    {2,1,1},{2,1,0},{0,2,1},{2,2,0},{2,2,1},{1,2,1},
}};

struct ModeFreq { Mode mode; quantity<isq::frequency[si::mega<si::hertz>]> f; };
struct EnclosureResult { std::array<ModeFreq, kModes.size()> modes; };

[[nodiscard]] std::expected<void, Error>            validate(const EnclosureInput&);
[[nodiscard]] std::expected<EnclosureResult, Error> calculate(const EnclosureInput&);

} // namespace emc::shielding
```

```c++
// src/shielding/rectangular_enclosure.cpp
auto calculate(const EnclosureInput& in) -> std::expected<EnclosureResult, Error> {
    if (auto ok = validate(in); !ok) return std::unexpected(ok.error());

    // c/2, expressed honestly via emc::constants -> doc 04.
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

> [!TIP]
> **Modern C++ features used here / and why**
> * **`constexpr` mode table + `std::ranges::transform`** — modeling the *mode set* as data and the
>   *formula* as code means the equation exists once and cannot drift across modes. The transform
>   over a `constexpr std::array` is allocation-free and (with constexpr `<cmath>` and
>   constexpr-friendly mp-units) can fold at compile time for fixed inputs.
> * **`kModes` as single source of order** — the same table drives the reference column order in the
>   golden test, so data and code cannot disagree (doc 09).

> [!NOTE]
> **`std::mdspan` callout.** A few calculators expose genuinely 2-D/3-D grids (a swept
> frequency × material table, or a current distribution over a cross-section). For those, return the
> data in a flat `std::vector` and hand callers a non-owning
> `std::mdspan<double, extents<...>>` view for `[i, j]` indexing — no nested `vector<vector<>>`, no
> manual stride math. The enclosure's 12 fixed modes do **not** need mdspan; a flat `std::array` is
> simpler. Reserve mdspan for true multi-dimensional sweeps.

Recommendation: **Option B**, layering Option A's named accessors (`f110`) on top of the array only
where a caller prefers named access — so both styles work.

---

## 5. Purity and thread-safety

Every `calculate`/`validate`/`solve_*` is a **pure function**:

* **No state.** No member variables, no mutating `static` locals, no singletons. Output depends only
  on the `Input`.
* **No I/O.** No file reads, no logging, no dialogs. The library never touches the screen or disk;
  presentation and error display are the caller's job.
* **Reentrant / thread-safe by construction.** With no shared mutable state, the same `calculate`
  runs on N threads over N inputs with zero locking — which makes a future `std::execution` / senders
  batch runner trivial (a C++26 forward-looking note in doc 02).
* **Constants are `constexpr`.** `emc::constants::*` are immutable compile-time values (doc 04), so
  there is no initialization-order or data-race concern around `mu_0`, `c`, etc.

Why this matters concretely:

* **Testing.** A pure `calculate` is tested by calling it with an `Input` and comparing the `Result`
  — no front end, no event loop. The golden tests become plain data-driven unit tests (doc 09).
* **Reuse.** The same function serves a desktop front end, a CLI, a batch sweep, or a web service.
* **Reasoning.** A reviewer verifies one formula in isolation.

---

## 6. Two full worked examples

### 6.A — SkinDepth (simple) — end to end

**Formula:** δ = √( 1 / (π · f · μ · σ) ), with μ = μ₀ · μ_r. This is the standard skin-depth
relation for a good conductor.

#### The complete header

```c++
// include/emc/basic/skin_depth.hpp
#pragma once
#include <emc/units.hpp>        // doc 03
#include <emc/error.hpp>        // doc 05
#include <emc/calculator.hpp>   // emc::Calculator concept
#include <expected>

namespace emc::basic {

using namespace mp_units;

struct SkinDepthInput {
    quantity<isq::frequency[si::hertz]>                             frequency;
    quantity<isq::electrical_conductivity[si::siemens / si::metre]> conductivity;
    quantity<one>                                                  relative_permeability{1 * one};
};

struct SkinDepthResult {
    quantity<isq::length[si::metre]> skin_depth;
};

[[nodiscard]] std::expected<void, Error>            validate(const SkinDepthInput& in);
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

#### The implementation (.cpp)

```c++
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

    const auto mu    = in.relative_permeability * emc::constants::mu_0;            // H/m, typed
    const auto denom = emc::constants::pi * in.frequency * mu * in.conductivity;   // -> 1/length^2

    // sqrt of a quantity yields the correctly-dimensioned quantity (length). No bare doubles.
    const auto delta = sqrt(1.0 / denom);
    return SkinDepthResult{ .skin_depth = delta.in(si::metre) };
}

} // namespace emc::basic
```

Notes:

* `emc::constants::pi` (full precision) and `emc::constants::mu_0` (one typed source) replace any
  ad-hoc per-file constants.
* The dimensional algebra checks out at compile time: `[H/m]·[Hz]·[S/m] = 1/m²`, so `sqrt(1/denom)`
  *is* a length. A dimensionally wrong formula would not compile.
* `validate` turns a silent divide-by-zero into an explicit `OutOfRange` error.
* **Material lookup is not the calculator's job.** Conductivity/permeability come from
  `emc::materials` (doc 04); the caller fills the `Input`:

```c++
auto m = emc::materials::lookup(emc::materials::Material::Nickel); // -> {conductivity, mu_r, ...}
auto r = emc::basic::calculate({ .frequency             = 27 * MHz,
                                 .conductivity          = m.conductivity,
                                 .relative_permeability = m.relative_permeability });
```

#### Example usage — a generic front end (no GUI toolkit)

```c++
#include <emc/basic/skin_depth.hpp>
#include <emc/materials.hpp>
#include <print>
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

void report_skin_depth(double freq_mhz, std::string_view material_name) {
    auto mat = emc::materials::lookup_by_name(material_name);
    if (!mat) { std::print("error: {}\n", mat.error().message()); return; }

    auto r = emc::basic::calculate({
        .frequency             = freq_mhz * MHz,
        .conductivity          = mat->conductivity,
        .relative_permeability = mat->relative_permeability,
    });
    if (!r) { std::print("error: {}\n", r.error().message()); return; }

    std::print("delta = {}\n", r->skin_depth.in(si::micro<si::metre>));
}
```

The front end contains **zero domain math** — only marshalling and rendering. That is the target end
state for every leaf calculator, whatever the front end happens to be.

#### Tests

Drive the pure `calculate` with hand-computed reference values from a textbook closed-form example
(δ for copper at a known frequency), then layer property checks:

* **Closed-form reference** — for copper at 1 MHz, the standard δ = √(1/(π·f·μ₀·σ)) ≈ 66 µm;
  assert within tolerance.
* **Monotonicity** — δ decreases as `frequency` increases and as `conductivity` increases.
* **Edge / error** — `frequency = 0` returns `OutOfRange`, not `inf`.
* **constexpr** — if `calculate` is constexpr-evaluable for fixed inputs, `static_assert` a known
  value.

See [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) for the data-driven harness.

---

### 6.B — MicrostripTrace (complex, bidirectional, validated) — end to end

**Formulas** (one rearrangement per unknown), with derived outputs C0 and Tpd for the Z0 case —
standard closed-form microstrip relations:

```text
Z0  = 87 * ln(5.98*H / (0.8*W + T)) / sqrt(eps + 1.41)
C0  = 0.67 * (eps + 1.41) / ln(5.98*H / (0.8*W + T))                 // pF/cm
Tpd = C0 * Z0                                                         // psec/cm
H   = exp(Z0*sqrt(eps+1.41)/87) * (0.8*W + T) / 5.98
T   = 5.98*H / exp(Z0*sqrt(eps+1.41)/87) - 0.8*W
W   = (5.98*H / exp(Z0*sqrt(eps+1.41)/87) - T) / 0.8
```

**Validation domain:** `1 ≤ eps ≤ 15`, `0.1 ≤ W/H ≤ 3`, and all of `H, W, T, Z0 > 0`.

#### The header (four typed solvers)

```c++
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

`solve_height`'s input has **no `h` field**: you cannot ask to solve for height and accidentally also
pass a height. Units live in the quantities, so there is no unit-branching plumbing anywhere.

#### The implementation (.cpp)

```c++
// src/component/microstrip_trace.cpp
#include <emc/component/microstrip_trace.hpp>
#include <cmath>

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::mm;

namespace {
// Shared domain check, used by every solver.
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

What this design buys, point by point:

* **No unit branching.** Each `.numerical_value_in(mm)` pulls the value into the formula's chosen
  unit regardless of what the caller supplied (mm, mils, inches).
* **One `check_domain`, not four copies.** It returns an `Error`; the math never talks to a front end.
* **The unknown is encoded in the types.** `solve_width` cannot be called with a width; `solve_height`
  cannot be called without `z0`. Whole classes of "wrong field set" bugs are unrepresentable.
* **Per-length outputs carry their dimension.** `c0` and `tpd` are real per-length quantities; the
  caller chooses `pF/cm` vs `pF/inch` at render time.

#### Example usage — a generic bidirectional front end

```c++
// A front end maps each "solve" action to the matching solver. The library knows nothing about it.
#include <emc/component/microstrip_trace.hpp>
#include <print>
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

void report_height(double z0_ohm, double t_mm, double w_mm, double eps) {
    auto r = emc::component::solve_height({
        .z0                    = z0_ohm * ohm,
        .t                     = t_mm * mm,
        .w                     = w_mm * mm,
        .relative_permittivity = eps * one,
    });
    if (!r) { std::print("error: {}\n", r.error().message()); return; }   // one error path
    std::print("H = {}\n", r->h.in(mm));
}
// z0 -> solve_impedance, t -> solve_thickness, w -> solve_width (same shape).
```

#### Tests

Use hand-computed reference values plus structural checks:

* **Closed-form reference** — pick a geometry inside the domain, compute Z0, C0, Tpd by hand from
  the formulas above, and assert `solve_impedance` matches within tolerance.
* **Round-trip** — `solve_impedance` then `solve_height` (feeding back the computed Z0 and the other
  two geometry values) must recover the original `h`; likewise for thickness and width.
* **Domain / error** — a row with `W/H ∉ [0.1, 3]` or `eps ∉ [1, 15]` must return `OutOfRange`
  rather than a number.
* **Type-level** — `static_assert(emc::ValidatedCalculator<MicrostripImpedance>)` already guards the
  contract at compile time.

---

## 7. Implementer checklist — build any one calculator

Follow these steps for each leaf. This is the procedure [10-build-roadmap.md](10-build-roadmap.md)
sequences and tracks.

1. **State the formula.** Write the closed-form equation(s) from the EMC reference, and note every
   constant and the input/output unit conventions.

2. **Pick the namespace & files.** Place it in the right category namespace
   (`emc::basic`/`component`/`shielding`/...). Create `include/emc/<category>/<name>.hpp` and
   `src/<category>/<name>.cpp`.

3. **Define `Input`.** One mp-units-typed field per independent variable, in a natural order. Add a
   `{default}` only for fields with an obvious neutral value (relative permeability `1`, a common
   substrate permittivity). No defaults on the required physical inputs (doc 03 for the unit
   vocabulary).

4. **Define `Result`.** One mp-units-typed field per output. For multi-output, prefer a `constexpr`
   index table + `std::array` (section 4). Do **not** bake in a display unit.

5. **Write the math** in `calculate` (or `solve_*` for bidirectional), using `emc::constants::*`
   (doc 04) for π, μ₀, c, etc., and `emc::materials::lookup` (doc 04) for material properties.
   Evaluate empirical (dimensionless-ratio) formulas in one chosen coherent unit to reproduce the
   standard constants exactly.

6. **Add `validate`.** Encode the physical domain as `return std::unexpected(Error{ErrorCode::OutOfRange, "..."})`.
   Guard divide-by-zero / domain-of-log / domain-of-sqrt explicitly. Call `validate` first inside
   `calculate` (doc 05).

7. **Bind the concept.** Add the tag `struct <Name> { using Input; using Result; static calculate;
   static validate; };` and `static_assert(emc::Calculator<Name>)` (or `ValidatedCalculator`) in the
   header. Fix any mismatch the assert reports.

8. **Write the tests.** Derive expected values from hand computation or a textbook closed-form
   example; add round-trip / property / monotonicity / edge / constexpr checks; assert that
   out-of-domain inputs return `OutOfRange` (doc 09).

9. **(Optional) Add a front-end adapter.** A generic front end reads fields → builds `Input` → calls
   `calculate` → renders `Result` or reports `error`. The front end must contain **no domain math**.

10. **Done criteria.** Compiles with the `static_assert` passing, tests green,
    `calculate`/`validate` are pure (no I/O, no globals).

---

## Cross-references

* [02-modern-cpp-feature-catalog.md](02-modern-cpp-feature-catalog.md) — concepts, `std::expected`,
  designated initializers, ranges, mdspan rationale in depth.
* [03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md) — the mp-units
  vocabulary that types the `Input`/`Result` fields.
* [04-constants-and-material-database.md](04-constants-and-material-database.md) — `emc::constants::*`
  and `emc::materials::*` used inside every `calculate`.
* [05-error-handling-and-validation.md](05-error-handling-and-validation.md) — `emc::Error`,
  `ErrorCode`, and the `std::expected` conventions used by `validate`/`calculate`.
* [07-calculator-inventory.md](07-calculator-inventory.md) — the full ~52-calculator work-list.
* [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) — the generic
  `template <emc::Calculator C>` golden-vector harness this pattern enables.
