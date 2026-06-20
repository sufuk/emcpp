# ⚠️ Error Handling & Validation

A single, value-oriented error model (`emc::ErrorCode` + `emc::Error` + `std::expected`) plus a declarative validation layer that every fallible calculator in `emc` reports failure through.

> [!NOTE]
> This is the failure contract for *every* fallible calculator. Calculators build on it ([`06-calculator-design-pattern.md`](06-calculator-design-pattern.md)); tests assert against it ([`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md)).

---

## 1. 🎯 Failure is a value, not a side effect

EMC calculators are math functions over physical inputs. A caller can supply geometry or material parameters outside a formula's valid domain (permittivity below 1, non-positive trace height, unknown material id). **These failures are routine, not exceptional**, so a fallible calculation returns its outcome on the **value channel** — `std::expected<Result, emc::Error>`, never a magic number, hot-path throw, or external side effect.

| Property | Design consequence |
| --- | --- |
| **Headless** ✅ | The core includes no UI header; no GUI type appears in `include/emc/`. Enforced in [`01-architecture-and-layout.md`](01-architecture-and-layout.md). |
| **Failure is a value** ✅ | Every fallible op returns `std::expected<Result, emc::Error>`. A caller cannot read a result that was never produced. |
| **A non-result is never a number** ✅ | No sentinel `0`/`-1`/NaN-as-signal. An unknown material is an `Error`, not a plausible resistivity. |
| **Rules are data** ✅ | Range checks are declarative — one place, field-by-field reporting. |

This lets the library serve a headless test, a CLI, or a server equally — none is assumed.

---

## 2. 🧱 The model: `ErrorCode` and `Error`

Both live in `include/emc/error.hpp`. `ErrorCode` is the machine-readable *category*; `Error` is the rich payload callers and tests inspect.

### 2.1 `ErrorCode`

A scoped `enum class : std::uint8_t` — small, fixed vocabulary, cannot implicitly convert to `int`, cheap to copy, exhaustively `switch`-able.

| Code | Meaning |
| --- | --- |
| `Ok = 0` | Reserved; **never stored** in an `Error`. Exists only so `0` is unambiguously "no error" if the enum is logged alone. |
| `OutOfRange` | Quantity outside its valid interval. |
| `InvalidInput` | Structurally bad input (empty selection, NaN, wrong combination). |
| `DomainError` | Math domain violation (`log` of ≤0, `sqrt` of negative). |
| `DivisionByZero` | A denominator the formula cannot tolerate evaluated to 0. |
| `NotConverged` | Iterative solver hit its cap. |
| `Unsupported` | Valid-but-not-implemented case (e.g. unknown `Material` id). |
| `Internal` | A library invariant was violated (should never reach a user). |

A `constexpr to_string(ErrorCode) -> std::string_view` returns a stable lowercase token (`"out_of_range"`, …) for zero-allocation logging and compile-time reference tables.

### 2.2 `Error`

`Error` carries structured data: category, human message, *which field* was wrong, the *valid range* it violated (optional `FieldRange{lo, hi, unit}`), and *where* it was raised (a defaulted `std::source_location`, diagnostic-only — never shown to end users).

- **`std::optional<FieldRange>`** — when a value is out of range, hand back the range it should have been in (`[lo, hi]` in the field's display unit). `optional` makes "no range applies" (e.g. a `DivisionByZero`) a type-level state, not a sentinel.
- **`field()` as a stable id** — e.g. `"permittivity"`, `"w_over_h"`; maps straight to the offending input so a front end highlights exactly that field (section 7).
- **`std::source_location` defaulted in the ctor** — captures the call site for free; dropped before anything reaches a user.

Factory helpers (section 4) keep the common cases to one short call; calculators rarely write the full constructor.

> [!CAUTION]
> `Ok = 0` is reserved and **never stored inside an `Error`** — an `Error` always means failure.

---

## 3. 🔁 Fallible API shape

Every calculator exposes the canonical triple. The two error-bearing pieces:

```cpp
// include/emc/<category>/<calc>.hpp
[[nodiscard]] std::expected<void, Error>             validate(const MicrostripInput& in) noexcept;
[[nodiscard]] std::expected<MicrostripResult, Error> calculate(const MicrostripInput& in) noexcept;
```

`validate` is a cheap, pure range/shape check; `calculate` runs it first, then computes. The only path out on bad input is the unexpected branch.

> [!IMPORTANT]
> The `[[nodiscard]] Result` discipline is the spine of this design: because the return type *is* the error channel, ignoring it is a compiler diagnostic. "This can fail" is part of the type, not a convention you can forget.

### 3.1 Why `std::expected` over exceptions

Calculator inputs have physical domains, so an out-of-domain input is a recoverable, typed error that belongs on the value channel.

| Axis | `std::expected<Result, Error>` | Exceptions |
| --- | --- | --- |
| **Frequency** | Out-of-range inputs are expected and routine. | Throwing on every bad input abuses the channel. |
| **Hot path** | Tagged union, no unwinding; safe to recompute live on edit. | Throw/catch cost and optimizer opacity per edit. |
| **Visibility** | `[[nodiscard]]` makes "can fail" part of the type. | Signature hides what may throw. |
| **Boundary** | Crosses any library boundary as a plain value. | Each call site needs `try/catch` plus a catch-all. |
| **Determinism** | Same input → same `expected`; pattern-match the `ErrorCode`. | Test must catch a throw; harder to match cleanly. |

`ErrorCode::Internal` is reserved for genuine invariant breaks; those pair with `assert`/contracts (section 8), **not** `expected`, because they are programmer errors.

### 3.2 Why `expected` over error codes / out-params

`bool calculate(const Input&, Result*)` forces a default-constructed, meaningless `Result` to exist before success is known, invites reading it on the failure path, and cannot carry the rich `Error`. `std::expected` makes result and error mutually exclusive by construction — there is no `Result` object on the failure path.

---

## 4. 📋 Declarative validation: rules as data

Express a calculator's domain constraints as a *single table* of field rules, so each constraint lives in exactly one place no matter how many solve directions the calculator supports.

Two pieces in `include/emc/validation.hpp`:

- **Error factories** — `out_of_range(field, lo, hi, unit)`, `domain_error(field, why)`, `division_by_zero(field)`. Each defaults a `std::source_location` and returns a fully-populated `Error`, keeping calculators terse.
- **`in_range`** — one reusable, `constexpr` check over any `std::totally_ordered T` (bare `double` ratios like `W/H` and permittivity, or typed `mp-units` quantities). It is the single home of the comparison logic; on failure it returns `std::unexpected(out_of_range(...))`.

For microstrip, the constraints `(eps ∈ [1,15]) && (W/H ∈ [0.1,3]) && (H,W,T > 0)` become a flat rule list evaluated in one pass, reusable by every solve direction because it validates the geometric/material invariants, not a particular unknown.

What this buys structurally:

- Each constant (`1`, `15`, `0.1`, `3`) appears **once**, attached to a named field — no chance for copies to drift.
- The check runs **before** any `log`/`sqrt`, so the formula never sees a non-positive argument.
- The function is `noexcept`, pure, UI-free, and unit-testable headless.
- Every failure names its `field`, so a front end can highlight the specific offending input.

> [!WARNING]
> Validate **before** computing. The microstrip formula calls `log()` and `sqrt()`; running them on a non-positive `0.8*W + T` or negative `eps + 1.41` yields NaN, not a catchable error. Ordering the range check ahead of the math is a correctness requirement, not a style choice.

For many-field calculators, the per-field `if`s can themselves be data — a small `std::vector<Rule>` (a `field` label plus a predicate returning `std::optional<Error>`) returned by `rules_for(in)`. This is the foundation for aggregation (section 6). Whether a calculator uses the inline form or the table form depends on field count; both feed the same `Error`.

---

## 5. 🔗 Composition with monadic operations

C++23 gives `std::expected` `and_then` / `transform` / `or_else` / `value_or`. A calculator then reads as a pipeline — validate → compute → post-check — with the error short-circuit handled by the type, not a pyramid of `else`.

| Op | Use in a calculator |
| --- | --- |
| `and_then(f)` | Chain a step that can *itself* fail (validate → compute → post-check). |
| `transform(f)` | Wrap an infallible final shaping step (build the `Result` struct). |
| `or_else(f)` | Recover / enrich the error (rare in core; used at the front-end boundary). |
| `value_or(x)` | Only at outermost call sites that truly want a default. |

A `calculate()` body therefore returns `validate(in).and_then([&]{ ... compute ..., return Result{...}; })`: each stage passes its value forward or short-circuits with an `Error`, and the happy path reads top-to-bottom with no nesting. A fallible material lookup composes the same way — `materials::resistivity_of(id)` returns `expected<quantity, Error>`, an unknown id yields `ErrorCode::Unsupported`, and the custom path is selected explicitly by `MaterialId::Custom`. No numeric sentinel anywhere in the chain.

> [!CAUTION]
> `value_or(x)` inside core math resurrects the sentinel anti-pattern — it silently substitutes a default for a failure the caller never sees. Reserve it for outermost call sites.

---

## 6. 📑 Aggregating multiple failures

A short-circuiting boolean check reports only the *first* problem, forcing a user with three bad fields into a fix-resubmit loop. With validation as data (section 4), all violations collect in one pass.

- `using ErrorList = std::vector<Error>;` and `ok_or(ErrorList) -> expected<void, ErrorList>` (empty → success).
- Given the `rules_for(in)` table, `validate_all` runs every rule via a lazy `std::ranges` pipeline (`transform` → `optional<Error>`, `filter` the failures, `transform` to unwrap) and materializes the survivors into an `ErrorList`. Nothing is allocated until the final list is built.

Two entry points, **one** rule table:

- `validate(in) -> expected<void, Error>` — fast path, first failure; used internally by `calculate()`.
- `validate_all(in) -> expected<void, ErrorList>` — front-end path, every failure so each bad field can light up.

> [!IMPORTANT]
> Both are generated from the **same** `rules_for(in)` table, so they can never disagree about what "valid" means. There is exactly one definition of the domain.

---

## 7. 🖥️ The front-end boundary

The mapping from `Error` to a message and a field highlight lives **in the front end**, never in the library. The library produces structured data; the front end decides presentation, keying off `code()`, `field()`, and `range()`.

```cpp
// In a generic front end (CLI, server response builder, ...) — NOT in libemc.
std::string render(const emc::Error& e) {
    if (e.range())
        return std::format("{} must be between {} and {}{}{}",
                           e.field(), e.range()->lo, e.range()->hi,
                           e.range()->unit.empty() ? "" : " ", e.range()->unit);
    return std::format("{}: {}", e.field(), e.message());
}
```

A caller runs `validate_all` first (reporting every bad field), then `calculate`; both branches inspect the typed `Error`. Boundary rules:

- **Presentation lives in the front end.** `message()` is a stable English fallback; the front end reconstructs a localized string from `code()`/`field()`/`range()`. The library links no translation system.
- **`field()` drives targeting** — every `Error` names exactly one field.
- **`source_location` is dropped here** — developer logs only; the front end never reads `where()`.
- **The dependency points one way:** front end → `emc`, never the reverse ([`01-architecture-and-layout.md`](01-architecture-and-layout.md)).

---

## 8. 🔮 C++26 forward-looking: Contracts

> [!NOTE]
> Forward-looking — not part of the C++23 baseline. Sketch only.

C++26 adds language-level **contracts** (`pre`, `post`, `contract_assert`), expressing the invariants a function assumes and guarantees, checked under a chosen semantic (ignore / observe / enforce). They complement `std::expected`: `expected` is for user-facing, recoverable errors; contracts are for programmer-facing invariants that hold by construction (in C++23, `assert` / `ErrorCode::Internal`).

A `detail::` helper whose preconditions the public `validate()` already established could state them as `pre(eps >= 1.0 && eps <= 15.0)`, `pre(0.8*W + T > 0.0)`, `post(z0 : z0 > 0.0)` — checkable in debug/CI, elidable in release.

| Failure kind | C++23 | C++26 contracts |
| --- | --- | --- |
| User typed a bad value | `validate()` → `std::expected` | unchanged — must reach the front end |
| Internal helper called with validated args | `assert` / `ErrorCode::Internal` | `pre(...)` on the `detail::` helper |
| Function promises a property of its output | hand-written post-check + `domain_error` | `post(r : ...)` |

> [!IMPORTANT]
> Contracts let the *internal* math state its assumptions in its signature, while the *public* `calculate()` keeps returning `std::expected` — reachable user errors must never be merely "asserted away."

---

## Cross-references

See [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md) for the calculator triple and `Calculator` concept this model plugs into; [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md), [`04-constants-and-material-database.md`](04-constants-and-material-database.md), and [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md) for typed bounds, the fallible material lookup, and failure-asserting tests.
