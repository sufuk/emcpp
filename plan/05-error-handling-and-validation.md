# Error Handling & Validation

> Purpose: define a single, Qt-free, value-oriented error model (`emc::ErrorCode` + `emc::Error` + `std::expected`) and a declarative validation layer that replaces the `QMessageBox`-inside-the-math, `return`-with-no-value, and `EXIT_FAILURE`-as-a-number patterns found across the NinjaEMC widgets.

This document specifies how *every* fallible calculator in the `emc` library reports failure. It is the contract that [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md) builds calculators on, that [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md) asserts against, and that the rewired Qt app in [`08-build-system-cmake.md`](08-build-system-cmake.md) maps back onto its UI.

---

## 1. The problem: validation welded to the UI

In NinjaEMC, "is this input valid?" and "tell the user it's wrong" are the *same line of code*, sitting in the *middle of the formula*, inside a Qt widget. Three distinct anti-patterns appear, and all three make the math impossible to reuse, test, or run off the UI thread.

### 1.1 `QMessageBox::warning` inside the math (10 files)

`MicrostripTraceWidget.cpp` is the textbook case. The validation is interleaved with the computation, and reporting the error *is* a call into the GUI toolkit:

```cpp
// src/.../MicrostripTrace/MicrostripTraceWidget.cpp  (microstrip(), lines ~107-118)
Z = 87 * log(5.98 * H / (0.8 * W + T * 1)) / qSqrt(eps * 1 + 1.41);
if ((eps < 1) || (eps > 15)) {
    QMessageBox::warning(this, "Warning", "Permittivity is out of range");
    return;                                  // <-- swallows the failure, no value, no code
}
else {
    if (((W / H) < 0.1) || ((W / H) > 3) || (H <= 0) || (W <= 0) || (T <= 0) || (Z <= 0)) {
        QMessageBox::warning(this, "Warning", "Parameters are out of range");
        return;
    }
    else {
        ui->z0_lineEdit->setValue(((Z * 10) / 10));   // <-- and the "result" is a UI side effect
        // ... more math, more setValue() ...
    }
}
```

Everything wrong with this in one fragment:

| Symptom | Consequence |
| --- | --- |
| `QMessageBox::warning(this, ...)` | The domain rule (`1 <= eps <= 15`) cannot run without a `QWidget` and a running event loop. Untestable headless. |
| `return;` (void) | Caller cannot distinguish "did not run" from "ran and produced 0". No way to react programmatically. |
| Result written via `ui->...->setValue()` | There is no value to return, assert on, or feed into another calculator. |
| Validation *after* `Z` is already computed | The formula runs on out-of-range inputs before the check; `log` of a non-positive argument can already have produced NaN. |
| The same range tree (`eps 1..15`, `W/H 0.1..3`) is **copy-pasted into all four methods** (`microstrip`, `calH`, `calT`, `calW`) | Four chances to drift out of sync; the rule lives in four places. |

### 1.2 Early `return` with no value (the silent failure)

Because the slots are `void`, the *only* failure channel is the message box. If a developer forgets the box (several widgets do), the function simply returns and the stale previous result stays on screen. There is no type-level reminder that the operation can fail — nothing is `[[nodiscard]]`, nothing is `expected`.

### 1.3 `EXIT_FAILURE` smuggled in as a numeric sentinel

`StandardGaugeWireWidget.cpp` is the worst offender: a *resistivity lookup* returns the process-exit constant `EXIT_FAILURE` (the integer `1`) to mean "material not found":

```cpp
// src/.../Resistance/StandardGaugeWire/StandardGaugeWireWidget.cpp  (lines ~157-178)
qreal StandardGaugeWireWidget::GetResistivity(enum Material material) {
    if (material == Custom)    return 0;            // 0 also means "not found / use custom"
    else if (material == Copper)  return 0.0000000172;
    else if (material == Silver)  return 0.0000000159;
    // ... 6 more ...
    else
        return EXIT_FAILURE;                        // <-- 1.0 ohm-metre?! a sentinel masquerading as physics
}
```

`EXIT_FAILURE` is `1`. As a resistivity (`1 Ohm*m`) it is a *physically plausible-looking number* that will silently flow into `rho = 1.0 * m; sigma = 1 / m;` and produce a wrong-but-not-obviously-wrong answer. Worse, `0` is *also* overloaded: `0` means "Custom material, read the manual `rho`/`sigma` spinboxes instead" — so the same function uses two different magic numbers to mean two different non-results, neither of which the type system knows about. The downstream code (lines ~80-91) has to *re-derive* the meaning of `0` with another `if` tree.

### 1.4 What we are buying with the rewrite

The library must be usable from a headless test, a CLI, a server, a future mobile shell, *or* the existing Qt app — none of which should be assumed. So the rules below are absolute:

- **The core never includes a UI header.** No `<QMessageBox>`, no `QWidget*`, no `qDebug()`. The dependency rule is enforced in [`01-architecture-and-layout.md`](01-architecture-and-layout.md).
- **Failure is a value, not a side effect.** Every fallible operation returns `std::expected<Result, emc::Error>`.
- **A non-result is never a number.** No `EXIT_FAILURE`, no `0`, no `-1`, no NaN-as-signal.
- **The rule is data, not control flow.** Range checks are described declaratively so they live in one place and can be reported back to the UI field-by-field.

---

## 2. The model: `emc::ErrorCode` and `emc::Error`

Two types live in `include/emc/error.hpp` (namespace `emc`). `ErrorCode` is the machine-readable *category*; `Error` is the rich payload the UI and tests inspect.

### 2.1 `emc::ErrorCode`

```cpp
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

// Stable, lowercase, machine-friendly spelling — handy for logs and CSV diffs.
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

**Why a scoped `enum class : std::uint8_t`** — Pain point: NinjaEMC has *no* error vocabulary at all; the closest thing is `EXIT_FAILURE` and `0`. A scoped enum gives every failure a name that cannot implicitly convert to `int` (so it can never again be confused with a resistivity), is cheap to copy, and is exhaustively `switch`-able. Fixing the underlying type to `std::uint8_t` keeps `Error` small. `Ok = 0` is reserved and *never* stored inside an `Error` (an `Error` always means failure); it exists only so that `0` is unambiguously "no error" if the enum is ever logged.

**Why `constexpr to_string` returning `std::string_view`** — it lets tests and `std::format` print a stable token with zero allocation, and it is `constexpr` so golden-vector tables can be built at compile time (see [`09`](09-testing-and-golden-vectors.md)).

### 2.2 `emc::Error`

`Error` carries everything the *old* `QMessageBox` string carried, but as structured data: the category, a human message, *which field* was wrong, the *valid range* it violated (optional), and *where* it was raised (optional `std::source_location`, for diagnostics — never shown to end users).

```cpp
// include/emc/error.hpp (continued)
#include <optional>
#include <source_location>
#include <string>
#include <format>

namespace emc {

// A closed interval [min, max] describing the legal range of a single field.
// Stored as double in the field's *display* unit so the UI can echo it verbatim
// (e.g. "permittivity must be in [1, 15]"). The canonical typed range lives in
// the validator (section 4); this is the human-facing projection.
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

    // Human-readable one-liner for logs/CLIs. The UI builds its own string from
    // the structured fields instead (section 7); this is a fallback, not the path.
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

**Design notes and the *why* for each modern feature:**

- **`std::optional<FieldRange>`** — Pain point: the old warnings were free text (`"Permittivity is out of range"`) with the *actual* bounds (`1..15`) buried unreadably in the `if`. By carrying the range as data, the UI can render "must be between 1 and 15" *without re-stating the constant*, and a test can assert the exact bound. `std::optional` (C++17, but used pervasively here) makes "no range applies" (e.g. a `DivisionByZero`) a type-level state rather than a sentinel `-1`.
- **`field()` as a stable id** — replaces the implicit "the user must guess which spinbox" of `"Please check your input!"` (the literal message `calH`/`calT`/`calW` use). The UI can map `"permittivity"` straight to the offending widget and highlight it (section 7).
- **`std::source_location` defaulted in the constructor** — captures the call site *for free* at the point an `Error` is *constructed*, replacing scattered `qDebug() << ...`. It is diagnostic-only; section 7 explicitly drops it before showing anything to a user. Using a defaulted argument means call sites stay clean: `Error{ErrorCode::DomainError, "..."}` auto-records where it was raised.
- **`std::format` / `std::print`** — the C++23 replacement for `qDebug()` streaming and `QString` concatenation. `Error::format()` is allocation-light and locale-independent, suitable for CI logs.

> **Construction ergonomics.** In practice calculators do not write the full constructor; section 4 provides factory helpers (`out_of_range(...)`, `domain_error(...)`) so the common cases are one short call.

---

## 3. Fallible API shape

Every calculator exposes the canonical triple from the shared conventions. The two error-bearing pieces are:

```cpp
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

### 3.1 Why `std::expected` over exceptions

| Axis | `std::expected<Result, Error>` | Exceptions |
| --- | --- | --- |
| Frequency | Out-of-range inputs are **expected and routine** — the UI lets the user type anything. They are not exceptional. | Throwing on every bad keystroke-driven recompute is abuse of the channel. |
| Hot path | These functions return a value and run interactively (the old `WavelengthvsFrequency` recomputes live on edit). `expected` is a tagged union, no unwinding. | Throw/catch cost and optimizer opacity on a path that fires per keystroke. |
| Visibility | `[[nodiscard]] std::expected<...>` makes "this can fail" part of the **type**; the compiler nags if you ignore it. Directly fixes the silent `void`-`return` failures (§1.2). | `noexcept`-by-default discipline is lost; callers can't see from the signature what may throw. |
| Boundary | Crosses the Qt/library boundary as a plain value; no `try/catch` glue in the widget. | The widget would need a `try/catch` per call and a catch-all for unknown types. |
| Determinism | Same input -> same `expected`. Trivially testable against the 52 golden CSVs. | Test must `EXPECT_THROW`, can't pattern-match the `ErrorCode` as cleanly. |

We *do* reserve `ErrorCode::Internal` for genuine "this should never happen" invariant breaks; those may be paired with `assert`/contracts (section 8), not with `expected`, because they are programmer errors, not user errors.

### 3.2 Why `expected` over error codes / out-params

The C-style alternative — `bool calculate(const Input&, Result* out)` or `ErrorCode calculate(const Input&, Result&)` — is exactly the `GetResistivity` trap generalized: it forces a *default-constructed, meaningless* `Result` to exist before success is known, invites reading it on the failure path, and cannot carry the rich `Error` (field, range, location). `std::expected` makes the result and the error *mutually exclusive by construction*: there is no `Result` object at all on the failure path.

---

## 4. Declarative validation: rules as data, not nested `if`s

The goal: turn the copy-pasted `(eps < 1) || (eps > 15) || ((W/H) < 0.1) || ((W/H) > 3) || ...` tree — duplicated across four `MicrostripTraceWidget` methods — into a *single table* of field rules.

### 4.1 The `in_range` helper and `Error` factories

```cpp
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

For typed quantities, `double(lo)` is replaced by `lo.numerical_value_in(<display unit>)` so the reported bound is in the user's unit; the overload set in the real header handles both. The point is the *shape*: `in_range` is the single home of the comparison logic that NinjaEMC duplicated dozens of times.

### 4.2 Before -> After: the MicrostripTrace validation tree

**Before** (one of *four* identical copies; here `microstrip()`, lines ~108-116):

```cpp
if ((eps < 1) || (eps > 15)) {
    QMessageBox::warning(this, "Warning", "Permittivity is out of range");
    return;
} else {
    if (((W / H) < 0.1) || ((W / H) > 3) || (H <= 0) || (W <= 0) || (T <= 0) || (Z <= 0)) {
        QMessageBox::warning(this, "Warning", "Parameters are out of range");
        return;
    } else { /* ... compute & setValue ... */ }
}
```

**After** — the rules are a flat list, evaluated in one pass, reusable by *all four* solve directions because it validates the geometric/material invariants, not a particular unknown:

```cpp
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

    // Each rule is one line. The list IS the spec — the same constants the old
    // code buried in a boolean soup, now named and reportable per field.
    if (auto r = in_range("relative_permittivity", eps, 1.0, 15.0); !r) return r;
    if (h_mm <= 0.0) return std::unexpected(out_of_range("dielectric_height", 0.0, 1e9, "mm"));
    if (w_mm <= 0.0) return std::unexpected(out_of_range("trace_width",       0.0, 1e9, "mm"));
    if (t_mm <= 0.0) return std::unexpected(out_of_range("trace_thickness",   0.0, 1e9, "mm"));

    // The geometric validity window W/H in [0.1, 3] — the rule the old code
    // re-typed in microstrip(), calH(), calT(), calW().
    const double w_over_h = w_mm / h_mm;
    if (auto r = in_range("w_over_h", w_over_h, 0.1, 3.0); !r) return r;

    return {};   // success: an empty expected<void>
}
} // namespace emc::component
```

What changed structurally:

- The constants `1`, `15`, `0.1`, `3` now appear **once**, each attached to a named field. They were previously in four places.
- The check runs **before** any `log`/`qSqrt`, so the formula never sees a non-positive argument (fixing the §1.1 ordering bug where `Z` was computed first).
- No `QWidget`, no message box. The function is `noexcept`, pure, and unit-testable headless.
- Every failure names its `field`, so the UI can highlight the *specific* offending input instead of the old generic `"Please check your input!"`.

### 4.3 A reusable rule list (optional, for many-field calculators)

For calculators with many fields, the per-field `if`s can themselves be data — a small array of closures evaluated by a `ranges` pipeline (this is the foundation for *aggregating* failures in section 6):

```cpp
// A field rule: a label plus a predicate that yields an Error when violated.
struct Rule {
    std::string_view field;
    std::function<std::optional<Error>()> check;   // nullopt == passes
};

[[nodiscard]] std::vector<Rule> rules_for(const MicrostripInput& in);  // returns the table
```

Whether a calculator uses the inline form (§4.2) or the table form depends on field count; both feed the same `Error`.

---

## 5. Composition with `std::expected` monadic operations

C++23 gives `std::expected` the same monadic surface as `std::optional`: `and_then`, `transform`, `or_else`, plus `value_or`. This lets a calculator read as a *pipeline* — validate, then compute, then post-check — with the error short-circuit handled by the type, not by a pyramid of `else` blocks.

### 5.1 The operations, mapped to their job

| Op | Signature intuition | Use in a calculator |
| --- | --- | --- |
| `and_then(f)` | `expected<T,E>` + `T -> expected<U,E>` -> `expected<U,E>` | Chain a step that can *itself* fail (validate -> compute -> post-check). |
| `transform(f)` | `expected<T,E>` + `T -> U` -> `expected<U,E>` | Wrap an infallible final shaping step (build the `Result` struct). |
| `or_else(f)` | `expected<T,E>` + `E -> expected<T,E>` | Recover / enrich the error (rarely in core; used at the UI boundary). |
| `value_or(x)` | -> `T` | Only at call sites that truly want a default; **never** inside core math (that would resurrect the sentinel). |

### 5.2 A full `calculate()` body using the pipeline

```cpp
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

            // Guard the one denominator the old code never guarded explicitly.
            const double denom = 0.8 * W + T;            // argument to log()
            if (denom <= 0.0)
                return std::unexpected(domain_error("trace_geometry",
                                                    "0.8*W + T must be > 0 for log()"));

            // Wheeler/IPC microstrip formula, now in pure C++23 (std::log, std::sqrt).
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

**Why the monadic style** — Pain point: the old `microstrip()` is a four-deep `if/else` staircase where the *success* path is at the bottom of the indentation and the *failure* paths are scattered `return;`s. The pipeline inverts that: each stage either passes its value forward or short-circuits with an `Error`, and the happy path reads top-to-bottom with no nesting. `and_then` means "only run the next step if everything so far succeeded" — the compiler, not hand-written `else` blocks, enforces it. This is the structural antidote to the staircase.

> The actual `quantity` construction in the `Result` initializer uses the project unit vocabulary from [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md); the `/* ... */` placeholders above stand in for those typed units.

### 5.3 Second worked conversion: `GetResistivity` -> a fallible lookup

The `EXIT_FAILURE`/`0` sentinel mess (§1.3) collapses into one honest `expected`:

```cpp
// include/emc/materials.hpp  (see 04-constants-and-material-database.md for the table)
namespace emc::materials {

// One source of truth, constexpr, typed. No magic 0, no EXIT_FAILURE.
[[nodiscard]] std::expected<quantity<isq::resistivity[si::ohm * si::metre]>, Error>
resistivity_of(MaterialId id) noexcept;

}
```

```cpp
// src/component/standard_gauge_wire.cpp
std::expected<WireResult, Error> calculate(const WireInput& in) noexcept {
    return validate(in)
        .and_then([&]() -> std::expected<WireResult, Error> {
            // Resolve material OR fall back to the user-supplied rho/sigma.
            // or_else turns "no built-in material" into the explicit custom path,
            // instead of the old "0 means custom, 1 (EXIT_FAILURE) means error" guesswork.
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

The two overloaded meanings of `0` and the `EXIT_FAILURE` smuggling are *gone*: an unknown material id returns `std::unexpected(Error{ErrorCode::Unsupported, ...})` from `resistivity_of`, and the custom path is explicit, not inferred from a magic zero.

---

## 6. Aggregating multiple failures

The old UI reports failures **one at a time**: the first `QMessageBox` that fires hides every other problem, so a user with three bad fields must fix-resubmit-fix-resubmit. With validation as data (section 4), we can collect *all* violations in one pass and hand the UI a batch, so it can highlight every bad field at once.

### 6.1 A multi-error result type

```cpp
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

Given the `Rule` table from §4.3, we run every rule, keep the ones that produced an `Error`, and materialize them with a ranges pipeline:

```cpp
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

**Why ranges here** — Pain point: the old boolean-OR check (`(eps<1)||(eps>15)||((W/H)<0.1)||...`) is *short-circuiting* by construction — it can only ever report the first true clause. A ranges pipeline over a rule list is the opposite: it visits *every* rule, so the result enumerates *all* offending fields. `transform`/`filter` are lazy views, so nothing is allocated until the final `ErrorList` is built. The pipeline is also trivially extendable — add a `Rule`, and aggregation, single-field validation, and UI highlighting all pick it up.

### 6.3 Two validation entry points, one rule table

- `validate(in) -> expected<void, Error>` — fast path, returns the *first* failure. Used internally by `calculate()` (cheap, short-circuits).
- `validate_all(in) -> expected<void, ErrorList>` — UI path, returns *all* failures so every bad field lights up.

Both are generated from the *same* `rules_for(in)` table, so they can never disagree about what "valid" means — the exact failure mode (four drifting copies of the rule) that NinjaEMC had.

---

## 7. The UI boundary: mapping `emc::Error` back to Qt

The mapping from `Error` to `QMessageBox`/field-highlight lives **in the app**, never in the library. The library produces structured data; the widget decides how to present it. This is the inverse of the old design, where presentation *was* the validation.

```cpp
// IN THE QT APP (e.g. src/ui/microstrip_trace_widget.cpp) — NOT in libemc.
#include <emc/component/microstrip_trace.hpp>
#include <QMessageBox>

void MicrostripTraceWidget::onSolveZ0() {
    const emc::component::MicrostripInput in{
        .relative_permittivity = ui->permittivity_LineEdit->value(),
        .dielectric_height     = ui->h_lineEdit->value() * unitFromRadio(/*h*/),
        .trace_width           = ui->w_lineEdit->value() * unitFromRadio(/*w*/),
        .trace_thickness       = ui->t_lineEdit->value() * unitFromRadio(/*t*/),
    };

    // Batch validation first so every bad field highlights at once.
    if (auto v = emc::component::validate_all(in); !v) {
        for (const emc::Error& e : v.error())
            highlightField(e);                 // app-side: field id -> QWidget
        showProblems(v.error());               // one QMessageBox listing all fields
        return;
    }

    // Compute. A failure here is a domain/division issue, not a field range.
    auto r = emc::component::calculate(in);
    if (!r) {
        QMessageBox::warning(this, tr("Cannot compute"),
                             messageFor(r.error()));   // app-side i18n of the Error
        return;
    }
    ui->z0_lineEdit->setValue(r->characteristic_impedance.numerical_value_in(mp_units::one));
    // ...
}

// Pure presentation glue — converts a library field id into a UI action.
void MicrostripTraceWidget::highlightField(const emc::Error& e) {
    QWidget* w = fieldWidgetMap_.value(QString::fromStdString(e.field()), nullptr);
    if (w) w->setStyleSheet("border: 1px solid red;");
}

// App owns the human/i18n string. The library gives structure; Qt gives tr().
QString MicrostripTraceWidget::messageFor(const emc::Error& e) {
    if (e.range())
        return tr("%1 must be between %2 and %3 %4")
            .arg(QString::fromStdString(e.field()))
            .arg(e.range()->lo).arg(e.range()->hi)
            .arg(QString::fromStdString(e.range()->unit));
    return QString::fromStdString(e.message());
}
```

Key boundary rules:

- **Translation lives in the app.** `Error::message()` is a stable English fallback; the widget reconstructs a localized string from `code()`, `field()`, and `range()` via Qt's `tr()`. The library never links a translation system.
- **`field()` drives highlighting.** A small `QHash<QString, QWidget*>` (`fieldWidgetMap_`) replaces the old "user guesses which input." Every `Error` points at exactly one input.
- **`source_location` is dropped here.** It is for developer logs only and is never surfaced to a user — that distinction is enforced by the app simply not reading `where()`.
- **No library symbol appears outside this glue.** The dependency points one way: app -> `emc`. The rule is part of the layering in [`01-architecture-and-layout.md`](01-architecture-and-layout.md).

---

## 8. C++26 forward-looking: Contracts

> *Forward-looking callout — not part of the C++23 baseline. Sketch only.*

C++26 adds language-level **contracts** (`pre`, `post`, and `contract_assert`). They express the *invariants* a function assumes and guarantees, checked at runtime under a chosen evaluation semantic (ignore / observe / enforce). They complement — not replace — `std::expected`: `expected` is for **user-facing, recoverable** errors (out-of-range inputs); contracts are for **programmer-facing invariants** that should hold by construction (today these would be `assert` / `ErrorCode::Internal`).

### 8.1 The same invariants, expressed natively

```cpp
// C++26 sketch — guarding an INTERNAL helper whose preconditions the public
// validate() has already established. (Not the public boundary — see note below.)
namespace emc::detail {

[[nodiscard]] constexpr double
microstrip_z0(double eps, double H, double W, double T)
    pre (eps >= 1.0 && eps <= 15.0)          // permittivity window (was the QMessageBox check)
    pre (H > 0.0 && W > 0.0 && T > 0.0)       // positivity (was the >0 soup)
    pre (0.8 * W + T > 0.0)                   // log() argument is positive
    post (z0 : z0 > 0.0)                      // we promise a physical impedance
{
    return 87.0 * std::log(5.98 * H / (0.8 * W + T)) / std::sqrt(eps + 1.41);
}

}
```

### 8.2 Division of labour

| Failure kind | Mechanism today (C++23) | With C++26 contracts |
| --- | --- | --- |
| User typed a bad value | `validate()` -> `std::expected` | unchanged — still `std::expected` (recoverable, must reach UI) |
| Internal helper called with already-validated args | `assert` / `ErrorCode::Internal` | `pre(...)` on the `detail::` helper |
| Function promises a property of its output | hand-written post-check + `domain_error` | `post(r : ...)` |

The point: contracts let the *internal* `detail::` math state its assumptions as part of its signature, so they are checkable in debug/CI builds and elidable in release — while the *public* `calculate()` keeps returning `std::expected` because reachable user errors must never be merely "asserted away." A future migration would push the redundant positivity post-checks in §5.2 into `post`-conditions and keep only the genuinely user-driven `expected` failures.

Other C++26 directions that touch this subsystem (covered in [`02-modern-cpp-feature-catalog.md`](02-modern-cpp-feature-catalog.md)): **reflection** could auto-generate the `rules_for()` table and `field()` ids from the `Input` struct members; **pattern matching** (`inspect`) would make `or_else` recovery on `ErrorCode` read as a clean match arm at the UI boundary.

---

## Cross-references

- [`00-overview-and-goals.md`](00-overview-and-goals.md) — why "no global state, pure, thread-safe" makes `expected` the natural error channel.
- [`01-architecture-and-layout.md`](01-architecture-and-layout.md) — the dependency rule that forbids UI headers in the core (enforces section 7's boundary).
- [`02-modern-cpp-feature-catalog.md`](02-modern-cpp-feature-catalog.md) — `std::expected`, `std::format`, `std::source_location`, ranges, and the C++26 callouts in their full catalog form.
- [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md) — the `quantity` types used in `Input`/`Result` and in typed `in_range` bounds.
- [`04-constants-and-material-database.md`](04-constants-and-material-database.md) — `materials::resistivity_of` and the `MaterialId` enum that replaces the `EXIT_FAILURE` lookup.
- [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md) — the full calculator triple (`Input`/`Result`/`calculate`/`validate`) and the `Calculator` concept this error model plugs into.
- [`07-calculator-inventory.md`](07-calculator-inventory.md) — which of the ~52 calculators carry which `ErrorCode`s.
- [`08-build-system-cmake.md`](08-build-system-cmake.md) — wiring the Qt app to consume `emc::emc` and the UI-boundary mapping of section 7.
- [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md) — asserting `expected` results (and expected *failures*) against the 52 golden CSVs.
