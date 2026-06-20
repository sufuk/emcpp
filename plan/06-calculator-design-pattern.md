# The Calculator Design Pattern 🧮

> The single repeatable shape every one of the ~52 `emc` calculators takes: a typed `Input`
> aggregate, a typed `Result`, a `[[nodiscard]] std::expected<Result, Error> calculate(const Input&)`
> free function, an optional `validate()`, and a `Calculator` concept binding the triple into a
> compile-checked contract. This is the template, copied 52 times.

The inventory ([07](07-calculator-inventory.md)) and roadmap ([10](10-build-roadmap.md)) both assume
this pattern.

---

## 1. The canonical anatomy

Every calculator is **five named things** in one category namespace (`emc::basic`, `emc::component`,
`emc::shielding`, ...). For a calculator `Foo`:

```text
struct FooInput   { /* mp-units-typed fields, sensible defaults */ };
struct FooResult  { /* mp-units-typed fields */ };
[[nodiscard]] std::expected<void,      Error> validate (const FooInput&);   // optional
[[nodiscard]] std::expected<FooResult, Error> calculate(const FooInput&);   // required
struct Foo { using Input=FooInput; using Result=FooResult; /* static calculate/validate */ };
```

Header in `include/emc/<category>/foo.hpp`, body in `src/<category>/foo.cpp`. It is a **compiled**
library, not header-only (install/export shape: [08](08-build-system-cmake.md)).

| Piece | Rules |
|---|---|
| `Input` | **Aggregate** (no user ctors/private members) → designated initializers, `==`, structured bindings. Every dimensional field is an mp-units `quantity`, never a bare `double`. Defaults only on neutral fields (`relative_permeability{1*one}`); required physical inputs get *no* default, so the compiler forces the caller to supply them. |
| `Result` | Aggregate of mp-units quantities. The caller chooses the display unit at render time (`r.skin_depth.in(si::micro<si::metre>)`) — the library never bakes in a presentation unit. Multi-output → more fields (§4). |
| `calculate` | The one **required** function. `[[nodiscard]]` — the result *is* the answer and the error. **Free function**, not a member: no object, no state, no vtable (§5). |
| `validate` | Optional range checks, returns `expected<void, Error>` (`{}` on success). Separable so a front end can **pre-flight** inputs without committing to the math. `calculate` calls it first. |

> [!NOTE]
> **Why a named aggregate, not positional parameters?**
> EMC formulas routinely take 4–8 length/impedance arguments; `calc(double, double, double, ...)`
> hides which is width and which is height. A named aggregate makes every call self-documenting and
> makes adding a field a *source-compatible* change — old call sites still compile, new field takes
> its default.

### The `Calculator` concept — contract enforced at compile time

Lives once, in `include/emc/calculator.hpp`:

```cpp
template <class C>
concept Calculator =
    requires { typename C::Input; typename C::Result; } &&
    requires (const typename C::Input& in) {
        { C::calculate(in) } -> std::same_as<std::expected<typename C::Result, Error>>;
    };

template <class C>                       // stronger variant: also requires validate()
concept ValidatedCalculator = Calculator<C> &&
    requires (const typename C::Input& in) {
        { C::validate(in) } -> std::same_as<std::expected<void, Error>>;
    };
```

Because calculators are **free functions in a namespace**, a tiny **tag type** per calculator binds
them to the concept — a zero-data, never-instantiated descriptor so generic code (test harnesses,
batch runners) can name the triple as one entity:

```cpp
struct SkinDepth {                       // at the bottom of skin_depth.hpp
    using Input  = SkinDepthInput;
    using Result = SkinDepthResult;
    static auto calculate(const Input& in) { return emc::basic::calculate(in); }
    static auto validate (const Input& in) { return emc::basic::validate(in);  }
};
static_assert(emc::ValidatedCalculator<SkinDepth>);   // contract checked in the header
```

> [!TIP]
> **Why a concept, not a virtual base class?**
> An OO base `class Calculator { virtual Result solve()=0; }` forces heap allocation, a vtable, type
> erasure, and runtime dispatch — for math that is pure and known at the call site. The concept gives
> the same "all calculators share a shape" guarantee with **zero runtime cost** and **better
> diagnostics**: a wrong return type fails the `static_assert` *in its own header*. It also lets one
> generic golden runner serve all 52: `template <emc::Calculator C> void run_golden(...)` (doc 09).

The header `static_assert` is a build-time tripwire: change a `Result` field or a `calculate` return
type and forget the tag, and the library *fails to compile at the point of the mistake*.

---

## 2. Call-site ergonomics

`Input` is an aggregate of mp-units quantities, so calls read like the formula's variable list with
units welded on:

```cpp
auto r = emc::basic::calculate({
    .frequency             = 27 * MHz,
    .conductivity          = 1.4493e7 * (si::siemens / si::metre),
    .relative_permeability = 600 * one,          // Nickel
});
if (!r) { /* handle r.error() */ }
std::print("delta = {}\n", r->skin_depth.in(si::micro<si::metre>));
```

- **Designated initializers** name the field *and* the unit (keep declaration order). A call is
  self-documenting.
- **Typed quantities** make `27 * mm` for a `frequency` field a *compile error*, not a wrong number
  silently fed into `sqrt`.
- **Defaults on neutral fields** — a copper call omits `relative_permeability`; a nickel call supplies
  it. No overload explosion.

A generic front end does only marshalling: read inputs → fill `Input` → call `calculate` → render
`Result` or report the error. It contains **zero domain math**.

---

## 3. The bidirectional / solve-for-X pattern

A microstrip trace solves for any one of **Z0, H, T, W** given the other three — each a rearrangement
of one impedance equation (`Z0 = 87·ln(5.98H/(0.8W+T))/√(eps+1.41)`, etc.).

**Design: one typed solver per unknown, not an enum dispatch.** Each gets its own `Input`/`Result`:

```cpp
struct MicrostripHeightInput {           // note: NO h field
    quantity<isq::resistance[si::ohm]>  z0;
    quantity<isq::thickness[si::metre]> t;
    quantity<isq::width[si::metre]>     w;
    quantity<one>                       relative_permittivity{4.7 * one};
};
struct MicrostripHeightResult { quantity<isq::height[si::metre]> h; };
[[nodiscard]] std::expected<MicrostripHeightResult, Error> solve_height(const MicrostripHeightInput&);
// siblings: solve_impedance, solve_thickness, solve_width — each with its own typed Input/Result
```

> [!NOTE]
> **Why typed functions, not one `enum SolveFor` + a god-struct?**
> An enum plus a struct with every field optional (three known, one unknown) lets the caller ask to
> solve for `H` while leaving `z0` unset — nothing caught until runtime. With typed functions **the
> unknown is absent from the input type and present in the result type**: `solve_height`'s `Input` has
> no `h` field, so you cannot forget a known nor supply the unknown. Each solver gets its own honest
> `[[nodiscard]]` result and precise `validate`. A front end wanting one dispatch point writes a 4-line
> `switch` over *its own* state — that adapter belongs in the front end, not the library.

Empirical formulas are dimensionless ratios of lengths, so the body pulls each input into one chosen
coherent unit (`mm`) to reproduce the standard constants exactly, then reattaches the unit on the way
out; mp-units guarantees we *enter* that unit correctly whatever the caller supplied:

```cpp
const double H = std::exp((Z * std::sqrt(1.41 + eps)) / 87.0) * (0.8 * W + T) / 5.98;  // mm
return MicrostripHeightResult{ .h = H * mm };   // unit reattached; caller converts (mils, in, ...)
```

No unit-branching plumbing anywhere. Full worked solver set lives in implementation/06.

---

## 4. The multi-output pattern

A rectangular enclosure returns **12 cavity-resonance frequencies**, one per `(m, n, p)` mode —
`f_mnp = (c/2/√eps)·√((m/L)² + (n/W)² + (p/H)²)`. Two ways to model "many outputs":

| Option | Shape | When |
|---|---|---|
| **A — named struct fields** | `std::array<ModeFreq, 12>` + accessors (`f110()`, `f101()`...) | Set is fixed and callers want names. |
| **B — `constexpr` mode table + `std::ranges` (recommended)** | The 12 modes declared **once** as data; **one** formula computes all of them in a transform. | Default. Adding a mode is adding a row, not copying an equation. |

Option B sketch — table once, formula once:

```cpp
inline constexpr std::array<Mode, 12> kModes{{ {1,1,0},{1,0,1},{0,1,1},/* ... */ }};

std::ranges::transform(kModes, out.modes.begin(), [&](Mode md) -> ModeFreq {
    const auto inv2 = pow<2>(md.m / L) + pow<2>(md.n / W) + pow<2>(md.p / H);  // 1/length²
    return { md, (k * sqrt(inv2)).in(si::mega<si::hertz>) };
});
```

`kModes` is the single source of order — the same table drives the reference column order in the
golden test, so data and code cannot disagree (doc 09). The transform over a `constexpr std::array` is
allocation-free and can fold at compile time for fixed inputs.

> [!NOTE]
> **`std::mdspan` callout.** A few calculators expose genuine 2-D/3-D grids (a swept frequency ×
> material table, a current distribution over a cross-section). For those, return a flat `std::vector`
> and hand callers a non-owning `std::mdspan<double, extents<...>>` for `[i, j]` indexing — no nested
> `vector<vector<>>`, no manual strides. The enclosure's 12 fixed modes do **not** need mdspan; a flat
> `std::array` is simpler. Reserve mdspan for true multi-dimensional sweeps.

**Recommendation:** Option B, layering Option A's named accessors on top of the array only where a
caller prefers named access.

---

## 5. Purity and thread-safety

Every `calculate`/`validate`/`solve_*` is a **pure function**:

- **No state.** No member variables, no mutating `static` locals, no singletons. Output depends only
  on the `Input`.
- **No I/O.** No file reads, no logging. Presentation and error display are the caller's job.
- **Reentrant / thread-safe by construction.** No shared mutable state → the same `calculate` runs on
  N threads over N inputs with zero locking, making a future `std::execution` batch runner trivial.
- **Constants are `constexpr`.** `emc::constants::*` are immutable compile-time values (doc 04) — no
  init-order or data-race concern around `mu_0`, `c`, etc.

Why it matters: **testing** is calling `calculate` with an `Input` and comparing `Result` — no front
end, no event loop (doc 09). **Reuse** — one function serves a desktop front end, a CLI, a batch
sweep, or a web service. **Reasoning** — a reviewer verifies one formula in isolation.

---

## 6. Implementer checklist — build any one calculator

The procedure [10-build-roadmap.md](10-build-roadmap.md) sequences and tracks.

1. **State the formula.** Closed-form equation(s) from the EMC reference; note every constant and the
   input/output unit conventions.
2. **Pick namespace & files.** Right category namespace; create the `.hpp` and `.cpp`.
3. **Define `Input`.** One mp-units-typed field per independent variable, natural order. `{default}`
   only on neutral fields; none on required physical inputs (doc 03 for the unit vocabulary).
4. **Define `Result`.** One mp-units-typed field per output; multi-output → `constexpr` table +
   `std::array` (§4). No baked-in display unit.
5. **Write the math** in `calculate` (or `solve_*`), using `emc::constants::*` and
   `emc::materials::lookup` (doc 04). Evaluate empirical formulas in one chosen coherent unit.
6. **Add `validate`.** Encode the physical domain as `std::unexpected(Error{ErrorCode::OutOfRange,
   "..."})`. Guard divide-by-zero / domain-of-log / domain-of-sqrt. Call it first inside `calculate`
   (doc 05).
7. **Bind the concept.** Add the tag `struct <Name>` + `static_assert(emc::Calculator<Name>)` (or
   `ValidatedCalculator`) in the header; fix any mismatch the assert reports.
8. **Write the tests.** Hand-computed / textbook reference values; round-trip / property / monotonicity
   / edge / constexpr checks; assert out-of-domain inputs return `OutOfRange` (doc 09).
9. **(Optional) front-end adapter.** Reads fields → builds `Input` → calls `calculate` → renders
   `Result` or reports `error`. **No domain math.**
10. **Done criteria.** Compiles with the `static_assert` passing, tests green, `calculate`/`validate`
    pure (no I/O, no globals).

---

## Cross-references

[03](03-quantities-and-units-mp-units.md) units · [04](04-constants-and-material-database.md)
constants/materials · [05](05-error-handling-and-validation.md) `Error`/`expected` ·
[07](07-calculator-inventory.md) work-list · [09](09-testing-and-golden-vectors.md) golden harness.
