# ⚠️ Error Handling & Validation

Defines a single, GUI-free, value-oriented error model (`emc::ErrorCode` + `emc::Error` + `std::expected`) and a declarative validation layer that every fallible calculator in `emc` reports failure through.

> [!NOTE]
> This is the failure contract for *every* fallible calculator in `emc`. [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md) builds calculators on it, [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md) asserts against it, and any front end maps it back onto its input fields.

---

## 1. 🎯 Why failure is a value, not a side effect

EMC calculators are math functions over physical inputs. A caller — script, server, or interactive front end — can supply geometry or material parameters outside a formula's valid domain: a permittivity below 1, a non-positive trace height, an unknown material id. **These failures are routine, not exceptional.**

> [!IMPORTANT]
> A fallible calculation returns its outcome on the **value channel**. Success and failure are mutually exclusive states of one returned object — `std::expected<Result, emc::Error>` — never a magic number, a thrown exception on the hot path, or a side effect on some external object.

Four properties fall out of treating failure as a value:

| Property | Design consequence |
| --- | --- |
| **Headless-usable** ✅ | The core never includes a UI header; no GUI-toolkit type appears in `include/emc/`. Enforced in [`01-architecture-and-layout.md`](01-architecture-and-layout.md). |
| **Failure is a value** ✅ | Every fallible operation returns `std::expected<Result, emc::Error>`. The caller cannot read a result that was never produced. |
| **A non-result is never a number** ✅ | No sentinel `0`, `-1`, or NaN-as-signal. An unknown material is an `Error`, not a plausible-looking resistivity. |
| **Rules are data** ✅ | Range checks are declarative, so they live in one place and report back field-by-field. |

These rules let the library serve a headless test, a CLI, a server, or a desktop front end equally — none is assumed.

---

## 2. 🧱 The model: `emc::ErrorCode` and `emc::Error`

Two types live in `include/emc/error.hpp`. `ErrorCode` is the machine-readable *category*; `Error` is the rich payload front ends and tests inspect.

### 2.1 `emc::ErrorCode`

```c++
// include/emc/error.hpp
#pragma once
#include <cstdint>
#include <string_view>

namespace emc {

enum class ErrorCode : std::uint8_t {
    Ok = 0,            // never stored in an Error; reserved so 0 is not a real failure
    OutOfRange,        // a quantity is outside its physically/numerically valid interval
    InvalidInput,      // structurally bad input (empty selection, NaN, wrong combination)
    DomainError,       // math domain violation (log of <= 0, sqrt of negative, etc.)
    DivisionByZero,    // a denominator that the formula cannot tolerate evaluated to 0
    NotConverged,      // an iterative solver hit its iteration cap without a result
    Unsupported,       // a valid-but-not-implemented case (e.g. unknown Material id)
    Internal,          // a library invariant was violated (should never reach a user)
};

// Stable, lowercase, machine-friendly spelling — handy for logs and diffs.
[[nodiscard]] constexpr std::string_view to_string(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::Ok:             return "ok";
        case ErrorCode::OutOfRange:     return "out_of_range";
        case ErrorCode::InvalidInput:   return "invalid_input";
        case ErrorCode::DomainError:    return "domain_error";
        case ErrorCode::DivisionByZero: return "division_by_zero";
        case ErrorCode::NotConverged:   return "not_converged";
        case ErrorCode::Unsupported:    return "unsupported";
        case ErrorCode::Internal:       return "internal";
    }
    return "unknown";
}

} // namespace emc
```

**Modern C++ features used here / and why**

- **Scoped `enum class : std::uint8_t`** — EMC failures fall into a small, fixed vocabulary of *categories*. A scoped enum names each one so it cannot implicitly convert to `int`; an error category can never be confused with a physical quantity such as a resistivity. It is cheap to copy and exhaustively `switch`-able, and fixing the underlying type to `std::uint8_t` keeps `Error` small.
- **`constexpr to_string` returning `std::string_view`** — lets tests and `std::format` print a stable token with zero allocation, and being `constexpr` lets reference tables be built at compile time (see [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md)).

> [!CAUTION]
> `Ok = 0` is reserved and is **never stored inside an `Error`** — an `Error` always means failure. It exists only so `0` is unambiguously "no error" if the enum is ever logged on its own.

### 2.2 `emc::Error`

`Error` carries structured data: the category, a human message, *which field* was wrong, the *valid range* it violated (optional), and *where* it was raised (optional `std::source_location`, for diagnostics — never shown to end users).

```c++
// include/emc/error.hpp (continued)
#include <optional>
#include <source_location>
#include <string>
#include <format>

namespace emc {

// A closed interval [min, max] describing the legal range of a single field.
// Stored as double in the field's *display* unit so a front end can echo it
// verbatim (e.g. "permittivity must be in [1, 15]"). The canonical typed range
// lives in the validator (section 4); this is the human-facing projection.
struct FieldRange {
    double      lo{};
    double      hi{};
    std::string unit{};   // e.g. "", "mm", "Hz" — empty for dimensionless
};

class Error {
public:
    Error(ErrorCode code,
          std::string message,
          std::string field = {},
          std::optional<FieldRange> range = std::nullopt,
          std::source_location where = std::source_location::current())
        : code_{code},
          message_{std::move(message)},
          field_{std::move(field)},
          range_{std::move(range)},
          where_{where} {}

    [[nodiscard]] ErrorCode                          code()    const noexcept { return code_; }
    [[nodiscard]] const std::string&                 message() const noexcept { return message_; }
    [[nodiscard]] const std::string&                 field()   const noexcept { return field_; }
    [[nodiscard]] const std::optional<FieldRange>&   range()   const noexcept { return range_; }
    [[nodiscard]] const std::source_location&        where()   const noexcept { return where_; }

    // Human-readable one-liner for logs/CLIs. A front end builds its own string
    // from the structured fields instead (section 7); this is a fallback.
    [[nodiscard]] std::string format() const {
        std::string s = std::format("[{}] {}", to_string(code_), message_);
        if (!field_.empty()) s += std::format(" (field: {})", field_);
        if (range_) s += std::format(" (allowed: [{}, {}]{}{})",
                                     range_->lo, range_->hi,
                                     range_->unit.empty() ? "" : " ", range_->unit);
        return s;
    }

private:
    ErrorCode                  code_;
    std::string                message_;
    std::string                field_;   // canonical field id, e.g. "permittivity", "w_over_h"
    std::optional<FieldRange>  range_;
    std::source_location       where_;
};

} // namespace emc
```

**Modern C++ features used here / and why**

- **`std::optional<FieldRange>`** — when a quantity is out of range, the most useful thing to hand back is the range it *should* have been in. Carrying `[lo, hi]` as data lets a front end render "must be between 1 and 15" without restating the constant, and lets a test assert the exact bound. `std::optional` makes "no range applies" (e.g. a `DivisionByZero`) a type-level state rather than a sentinel `-1`.
- **`field()` as a stable id** — an EMC input form has many fields, so an error must point at *which one* failed. A stable id like `"permittivity"` maps straight to the offending input field so a front end can highlight exactly that input (section 7).
- **`std::source_location` defaulted in the constructor** — captures the call site for free at the point an `Error` is *constructed*, giving a diagnostic breadcrumb. It is diagnostic-only; section 7 drops it before showing anything to a user. The defaulted argument keeps call sites clean: `Error{ErrorCode::DomainError, "..."}` auto-records where it was raised.
- **`std::format` / `std::print`** — allocation-light, locale-independent string building suitable for CI logs.

> [!TIP]
> Calculators rarely write the full constructor; section 4 provides factory helpers (`out_of_range(...)`, `domain_error(...)`) so the common cases are one short call.

---

## 3. 🔁 Fallible API shape

Every calculator exposes the canonical triple from the shared conventions. The two error-bearing pieces are:

```c++
// include/emc/<category>/<calc>.hpp
namespace emc::component {

struct MicrostripInput  { /* mp-units quantities, designated-init at call site */ };
struct MicrostripResult { /* mp-units quantities */ };

// Cheap, pure, no-throw range/shape check. Returns nothing on success.
[[nodiscard]] std::expected<void, Error>
validate(const MicrostripInput& in) noexcept;

// The calculation. Calls validate() first, then computes. Never throws on
// expected failures; the only path out on bad input is the unexpected branch.
[[nodiscard]] std::expected<MicrostripResult, Error>
calculate(const MicrostripInput& in) noexcept;

} // namespace emc::component
```

> [!IMPORTANT]
> **The `[[nodiscard]] Result` discipline is the spine of this design.** Because the return type *is* the error channel, a caller that ignores it gets a compiler diagnostic — "this can fail" is part of the type, not a convention you can forget. Silently dropping a possible failure is a compile-time nag, not a runtime surprise.

### 3.1 Why `std::expected` over exceptions

Calculator inputs have physical domains, so an out-of-domain input is a recoverable, typed error that belongs on the value channel rather than the exception channel.

| Axis | `std::expected<Result, Error>` | Exceptions |
| --- | --- | --- |
| **Frequency** | Out-of-range inputs are **expected and routine** — a caller can supply anything. Not exceptional. | Throwing on every bad input abuses the channel. |
| **Hot path** | These functions return a value and may run interactively (e.g. a wavelength-vs-frequency view recomputes live on edit). `expected` is a tagged union, no unwinding. | Throw/catch cost and optimizer opacity on a path that fires per edit. |
| **Visibility** | `[[nodiscard]] std::expected<...>` makes "this can fail" part of the **type**; the compiler nags if you ignore it. | `noexcept`-by-default discipline is lost; callers can't see from the signature what may throw. |
| **Boundary** | Crosses any library/front-end boundary as a plain value; no `try/catch` glue. | Each call site needs a `try/catch` plus a catch-all for unknown types. |
| **Determinism** | Same input → same `expected`. Trivially testable by pattern-matching the value or the `ErrorCode`. | Test must check for a throw, can't pattern-match the `ErrorCode` as cleanly. |

> [!NOTE]
> `ErrorCode::Internal` is reserved for genuine "this should never happen" invariant breaks; those may be paired with `assert`/contracts (section 8), **not** with `expected`, because they are programmer errors, not user errors.

### 3.2 Why `expected` over error codes / out-params

The C-style alternative — `bool calculate(const Input&, Result* out)` or `ErrorCode calculate(const Input&, Result&)` — forces a *default-constructed, meaningless* `Result` to exist before success is known, invites reading it on the failure path, and cannot carry the rich `Error` (field, range, location). `std::expected` makes result and error *mutually exclusive by construction*: there is no `Result` object at all on the failure path.

---

## 4. 📋 Declarative validation: rules as data, not nested `if`s

Express a calculator's domain constraints — for microstrip, `(eps in [1,15]) && (W/H in [0.1,3]) && (H,W,T > 0)` — as a *single table* of field rules, so each constraint lives in exactly one place no matter how many solve directions the calculator supports.

### 4.1 The `in_range` helper and `Error` factories

```c++
// include/emc/validation.hpp
#pragma once
#include <emc/error.hpp>
#include <mp-units/systems/si.hpp>
#include <concepts>
#include <string_view>

namespace emc {

// --- Error factories: keep calculators terse. ---------------------------------
[[nodiscard]] inline Error out_of_range(std::string_view field, double lo, double hi,
                                        std::string_view unit = {},
                                        std::source_location w = std::source_location::current()) {
    return Error{ErrorCode::OutOfRange,
                 std::format("{} is out of range", field),
                 std::string{field},
                 FieldRange{lo, hi, std::string{unit}}, w};
}

[[nodiscard]] inline Error domain_error(std::string_view field, std::string_view why,
                                        std::source_location w = std::source_location::current()) {
    return Error{ErrorCode::DomainError, std::format("{}: {}", field, why),
                 std::string{field}, std::nullopt, w};
}

[[nodiscard]] inline Error division_by_zero(std::string_view field,
                                            std::source_location w = std::source_location::current()) {
    return Error{ErrorCode::DivisionByZero,
                 std::format("{} would divide by zero", field),
                 std::string{field}, std::nullopt, w};
}

// --- in_range: one reusable check over any mp-units quantity OR a raw double. --
// Works on bare doubles (dimensionless ratios like W/H, permittivity) and on
// typed quantities (the [lo, hi] are quantities in the same dimension).
template <std::totally_ordered T>
[[nodiscard]] constexpr std::expected<void, Error>
in_range(std::string_view field, T value, T lo, T hi,
         std::string_view unit = {},
         std::source_location w = std::source_location::current()) {
    if (value < lo || value > hi)
        return std::unexpected(out_of_range(field, double(lo), double(hi), unit, w));
    return {};
}

} // namespace emc
```

> [!TIP]
> For typed quantities, `double(lo)` becomes `lo.numerical_value_in(<display unit>)` so the reported bound is in the user's unit; the overload set in the real header handles both. The point is the *shape*: `in_range` is the single home of the comparison logic, reused by every calculator.

### 4.2 The MicrostripTrace validation, as a flat rule list

The rules are a flat list, evaluated in one pass, reusable by every solve direction because it validates the geometric/material invariants, not a particular unknown:

```c++
// src/component/microstrip_trace.cpp
#include <emc/component/microstrip_trace.hpp>
#include <emc/validation.hpp>
#include <mp-units/systems/si.hpp>

namespace emc::component {
using namespace mp_units;
using mp_units::si::unit_symbols::mm;

std::expected<void, Error> validate(const MicrostripInput& in) noexcept {
    const double eps  = in.relative_permittivity;          // dimensionless
    const double h_mm = in.dielectric_height.numerical_value_in(mm);
    const double w_mm = in.trace_width.numerical_value_in(mm);
    const double t_mm = in.trace_thickness.numerical_value_in(mm);

    // Each rule is one line. The list IS the spec — each constant is named and
    // attached to the field it constrains, reportable per field.
    if (auto r = in_range("relative_permittivity", eps, 1.0, 15.0); !r) return r;
    if (h_mm <= 0.0) return std::unexpected(out_of_range("dielectric_height", 0.0, 1e9, "mm"));
    if (w_mm <= 0.0) return std::unexpected(out_of_range("trace_width",       0.0, 1e9, "mm"));
    if (t_mm <= 0.0) return std::unexpected(out_of_range("trace_thickness",   0.0, 1e9, "mm"));

    // The geometric validity window W/H in [0.1, 3] for the Wheeler/IPC model.
    const double w_over_h = w_mm / h_mm;
    if (auto r = in_range("w_over_h", w_over_h, 0.1, 3.0); !r) return r;

    return {};   // success: an empty expected<void>
}
} // namespace emc::component
```

What this buys structurally:

- Each constant (`1`, `15`, `0.1`, `3`) appears **once**, attached to a named field — no chance for copies to drift apart.
- The check runs **before** any `log`/`sqrt`, so the formula never sees a non-positive argument.
- No UI dependency. The function is `noexcept`, pure, and unit-testable headless.
- Every failure names its `field`, so a front end can highlight the *specific* offending input.

> [!WARNING]
> Validate **before** computing. The microstrip formula calls `log()` and `sqrt()`; running it on a non-positive `0.8*W + T` or a negative `eps + 1.41` produces NaN, not an error you can catch. Ordering the range check ahead of the math is a correctness requirement, not a style choice.

### 4.3 A reusable rule list (optional, for many-field calculators)

For calculators with many fields, the per-field `if`s can themselves be data — a small array of closures evaluated by a `ranges` pipeline (the foundation for *aggregating* failures in section 6):

```c++
// A field rule: a label plus a predicate that yields an Error when violated.
struct Rule {
    std::string_view field;
    std::function<std::optional<Error>()> check;   // nullopt == passes
};

[[nodiscard]] std::vector<Rule> rules_for(const MicrostripInput& in);  // returns the table
```

Whether a calculator uses the inline form (§4.2) or the table form depends on field count; both feed the same `Error`.

---

## 5. 🔗 Composition with `std::expected` monadic operations

C++23 gives `std::expected` the same monadic surface as `std::optional`: `and_then`, `transform`, `or_else`, plus `value_or`. A calculator then reads as a *pipeline* — validate, then compute, then post-check — with the error short-circuit handled by the type, not by a pyramid of `else` blocks.

### 5.1 The operations, mapped to their job

| Op | Signature intuition | Use in a calculator |
| --- | --- | --- |
| `and_then(f)` | `expected<T,E>` + `T -> expected<U,E>` → `expected<U,E>` | Chain a step that can *itself* fail (validate → compute → post-check). |
| `transform(f)` | `expected<T,E>` + `T -> U` → `expected<U,E>` | Wrap an infallible final shaping step (build the `Result` struct). |
| `or_else(f)` | `expected<T,E>` + `E -> expected<T,E>` | Recover / enrich the error (rare in core; used at the front-end boundary). |
| `value_or(x)` | → `T` | Only at call sites that truly want a default; **never** inside core math. |

> [!CAUTION]
> `value_or(x)` inside core math resurrects the sentinel anti-pattern — it silently substitutes a default for a failure the caller never sees. Reserve it for outermost call sites that genuinely want a fallback.

### 5.2 A full `calculate()` body using the pipeline

```c++
// src/component/microstrip_trace.cpp (continued)
namespace emc::component {

std::expected<MicrostripResult, Error> calculate(const MicrostripInput& in) noexcept {
    return validate(in)                          // expected<void, Error>
        .and_then([&]() -> std::expected<MicrostripResult, Error> {
            using namespace mp_units;
            using mp_units::si::unit_symbols::mm;

            const double eps = in.relative_permittivity;
            const double H   = in.dielectric_height.numerical_value_in(mm);
            const double W   = in.trace_width.numerical_value_in(mm);
            const double T   = in.trace_thickness.numerical_value_in(mm);

            // Guard the denominator of the log() argument explicitly.
            const double denom = 0.8 * W + T;            // argument to log()
            if (denom <= 0.0)
                return std::unexpected(domain_error("trace_geometry",
                                                    "0.8*W + T must be > 0 for log()"));

            // Wheeler/IPC microstrip formula in pure C++23 (std::log, std::sqrt).
            const double Z0 = 87.0 * std::log(5.98 * H / denom) / std::sqrt(eps + 1.41);
            if (Z0 <= 0.0)
                return std::unexpected(domain_error("characteristic_impedance",
                                                    "computed Z0 must be > 0"));

            const double C0  = 0.67 * (eps + 1.41) / std::log(5.98 * H / denom);  // pF/cm
            const double Tpd = C0 * Z0;                                           // psec/cm

            return MicrostripResult{
                .characteristic_impedance = Z0  * one /*ohm*/,
                .capacitance_per_cm       = C0  * /* pF/cm unit */,
                .propagation_delay_per_cm = Tpd * /* psec/cm unit */,
            };
        });
}

} // namespace emc::component
```

**Modern C++ features used here / and why** — each stage either passes its value forward or short-circuits with an `Error`, and the happy path reads top-to-bottom with no nesting. `and_then` means "only run the next step if everything so far succeeded" — the compiler, not hand-written `else` blocks, enforces it. For a chain of fallible physical steps (validate → impedance → derived quantities) this keeps the success path flat and the failure path explicit.

> [!NOTE]
> The actual `quantity` construction in the `Result` initializer uses the project unit vocabulary from [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md); the `/* ... */` placeholders stand in for those typed units.

### 5.3 Second worked example: a fallible material lookup

A resistivity lookup that may not have the requested material is naturally an `expected`:

```c++
// include/emc/materials.hpp  (see 04-constants-and-material-database.md for the table)
namespace emc::materials {

// One source of truth, constexpr, typed.
[[nodiscard]] std::expected<quantity<isq::resistivity[si::ohm * si::metre]>, Error>
resistivity_of(MaterialId id) noexcept;

}
```

```c++
// src/component/standard_gauge_wire.cpp
std::expected<WireResult, Error> calculate(const WireInput& in) noexcept {
    return validate(in)
        .and_then([&]() -> std::expected<WireResult, Error> {
            // Resolve material OR fall back to the user-supplied rho/sigma.
            // The custom path is explicit; an unknown built-in material is an
            // Error, not a guess.
            auto rho = (in.material == MaterialId::Custom)
                ? in.custom_resistivity                       // expected<quantity,Error>
                : materials::resistivity_of(in.material);

            return rho.and_then([&](auto resistivity)
                       -> std::expected<WireResult, Error> {
                if (resistivity.numerical_value_in(si::ohm * si::metre) <= 0.0)
                    return std::unexpected(domain_error("resistivity", "must be > 0"));
                // ... skin-depth / AC-resistance math, all typed ...
                return WireResult{ /* ... */ };
            });
        });
}
```

An unknown material id returns `std::unexpected(Error{ErrorCode::Unsupported, ...})` from `resistivity_of`, and the custom path is selected explicitly by `MaterialId::Custom` — no overloaded numeric sentinel anywhere in the chain.

---

## 6. 📑 Aggregating multiple failures

A short-circuiting boolean check reports only the *first* problem, forcing a user with three bad fields into a fix-resubmit loop. With validation as data (section 4), all violations are collected in one pass and handed back as a batch, so a front end can highlight every bad field at once.

### 6.1 A multi-error result type

```c++
// include/emc/validation.hpp (continued)
namespace emc {

// Returned by validate_all(): empty on success, else every violated field.
using ErrorList = std::vector<Error>;

[[nodiscard]] inline std::expected<void, ErrorList>
ok_or(ErrorList errs) {
    if (errs.empty()) return {};
    return std::unexpected(std::move(errs));
}

}
```

### 6.2 Collecting with `std::ranges`

Given the `Rule` table from §4.3, run every rule, keep the ones that produced an `Error`, and materialize them with a ranges pipeline:

```c++
// src/component/microstrip_trace.cpp
namespace emc::component {

std::expected<void, ErrorList> validate_all(const MicrostripInput& in) {
    auto failures = rules_for(in)
        | std::views::transform([](const Rule& r) { return r.check(); }) // -> optional<Error>
        | std::views::filter([](const auto& e)     { return e.has_value(); })
        | std::views::transform([](auto&& e)       { return *std::move(e); });

    return ok_or(ErrorList(std::ranges::begin(failures), std::ranges::end(failures)));
}

}
```

**Modern C++ features used here / and why** — a boolean-OR check (`(eps<1)||(eps>15)||((W/H)<0.1)||...`) short-circuits by construction, reporting only the first true clause. A ranges pipeline over a rule list visits *every* rule, so the result enumerates *all* offending fields. `transform`/`filter` are lazy views, so nothing is allocated until the final `ErrorList` is built. Adding a `Rule` extends single-field validation, aggregation, and field highlighting all at once.

### 6.3 Two validation entry points, one rule table

- `validate(in) -> expected<void, Error>` — fast path, returns the *first* failure. Used internally by `calculate()` (cheap, short-circuits).
- `validate_all(in) -> expected<void, ErrorList>` — front-end path, returns *all* failures so every bad field can light up.

> [!IMPORTANT]
> Both entry points are generated from the **same** `rules_for(in)` table, so they can never disagree about what "valid" means. There is exactly one definition of the domain.

---

## 7. 🖥️ The front-end boundary: mapping `emc::Error` to text

The mapping from `Error` to a message and a field highlight lives **in the front end**, never in the library. The library produces structured data; the front end decides how to present it. The example below is framework-agnostic — a generic front end turning an `emc::Error` into text with `std::print`, keyed off `code()`, `field()`, and `range()`.

### Example usage

```c++
// In a generic front end (a CLI, a server response builder, ...) — NOT in libemc.
#include <emc/component/microstrip_trace.hpp>
#include <print>
#include <string>

// Turn one Error into a single line of human-facing text. A front end localizes
// from the structured fields rather than printing message() verbatim.
std::string render(const emc::Error& e) {
    if (e.range())
        return std::format("{} must be between {} and {}{}{}",
                           e.field(), e.range()->lo, e.range()->hi,
                           e.range()->unit.empty() ? "" : " ", e.range()->unit);
    return std::format("{}: {}", e.field(), e.message());
}

void solve_z0(const emc::component::MicrostripInput& in) {
    // Batch validation first so every bad field is reported at once.
    if (auto v = emc::component::validate_all(in); !v) {
        for (const emc::Error& e : v.error())
            std::println("[{}] {}", emc::to_string(e.code()), render(e));
        return;   // each error names e.field() — point the user at that input field
    }

    // Compute. A failure here is a domain/division issue, not a field range.
    auto r = emc::component::calculate(in);
    if (!r) {
        std::println("cannot compute: {}", render(r.error()));
        return;
    }

    std::println("Z0 = {} ohm",
                 r->characteristic_impedance.numerical_value_in(mp_units::one));
}
```

Key boundary rules:

- **Presentation lives in the front end.** `Error::message()` is a stable English fallback; the front end reconstructs a localized string from `code()`, `field()`, and `range()`. The library never links a translation system.
- **`field()` drives targeting.** Every `Error` names exactly one field, so the front end can point the user at exactly one input field (highlight it in a UI, prefix it in a log).
- **`source_location` is dropped here.** It is for developer logs only and is never surfaced to a user — the front end simply does not read `where()`.
- **The dependency points one way:** front end → `emc`, never the reverse. Part of the layering in [`01-architecture-and-layout.md`](01-architecture-and-layout.md).

---

## 8. 🔮 C++26 forward-looking: Contracts

> [!NOTE]
> Forward-looking callout — not part of the C++23 baseline. Sketch only.

C++26 adds language-level **contracts** (`pre`, `post`, and `contract_assert`), expressing the *invariants* a function assumes and guarantees, checked at runtime under a chosen evaluation semantic (ignore / observe / enforce). They complement `std::expected`: `expected` is for **user-facing, recoverable** errors (out-of-range inputs); contracts are for **programmer-facing invariants** that hold by construction (in C++23 these would be `assert` / `ErrorCode::Internal`).

### 8.1 The same invariants, expressed natively

```c++
// C++26 sketch — guarding an INTERNAL helper whose preconditions the public
// validate() has already established. (Not the public boundary — see note below.)
namespace emc::detail {

[[nodiscard]] constexpr double
microstrip_z0(double eps, double H, double W, double T)
    pre (eps >= 1.0 && eps <= 15.0)          // permittivity window
    pre (H > 0.0 && W > 0.0 && T > 0.0)       // positivity
    pre (0.8 * W + T > 0.0)                   // log() argument is positive
    post (z0 : z0 > 0.0)                      // we promise a physical impedance
{
    return 87.0 * std::log(5.98 * H / (0.8 * W + T)) / std::sqrt(eps + 1.41);
}

}
```

### 8.2 Division of labour

| Failure kind | Mechanism in C++23 | With C++26 contracts |
| --- | --- | --- |
| User typed a bad value | `validate()` → `std::expected` | unchanged — still `std::expected` (recoverable, must reach the front end) |
| Internal helper called with already-validated args | `assert` / `ErrorCode::Internal` | `pre(...)` on the `detail::` helper |
| Function promises a property of its output | hand-written post-check + `domain_error` | `post(r : ...)` |

> [!IMPORTANT]
> Contracts let the *internal* `detail::` math state its assumptions as part of its signature — checkable in debug/CI builds, elidable in release — while the *public* `calculate()` keeps returning `std::expected`, because reachable user errors must never be merely "asserted away."

Other C++26 directions that touch this subsystem (covered in [`02-modern-cpp-feature-catalog.md`](02-modern-cpp-feature-catalog.md)): **reflection** could auto-generate the `rules_for()` table and `field()` ids from the `Input` struct members; **pattern matching** (`inspect`) would make `or_else` recovery on `ErrorCode` read as a clean match arm at the front-end boundary.

---

## Cross-references

- [`01-architecture-and-layout.md`](01-architecture-and-layout.md) — the dependency rule that forbids UI headers in the core (enforces section 7's boundary).
- [`02-modern-cpp-feature-catalog.md`](02-modern-cpp-feature-catalog.md) — `std::expected`, `std::format`, `std::source_location`, ranges, and the C++26 callouts in catalog form.
- [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md) — the `quantity` types used in `Input`/`Result` and in typed `in_range` bounds.
- [`04-constants-and-material-database.md`](04-constants-and-material-database.md) — `materials::resistivity_of` and the `MaterialId` enum behind the fallible lookup.
- [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md) — the full calculator triple and the `Calculator` concept this error model plugs into.
- [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md) — asserting `expected` results and expected *failures* with hand-computed, property, and edge tests.
