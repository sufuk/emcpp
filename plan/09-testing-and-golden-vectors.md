# Testing Strategy & Golden Vectors

> Purpose: turn the 52 UI-coupled CSV fixtures from NinjaEMC into a fast, deterministic, Qt-free
> regression suite for the pure `emc::*::calculate()` functions, layered with compile-time, property,
> and CI checks — so the rewrite is *provably* equivalent (or *intentionally* divergent) to the old app.

---

## 0. Why testing is the keystone of this migration

The single biggest risk in this project is **silent numerical drift**: a formula that the old Qt code
computed one way is subtly changed (different constant, different unit factor, different rounding) and
nobody notices until a customer's EMC report is wrong. The old code is *welded to the UI* (math lives
inside `connect(ui->solveButton, ...)` lambdas — see `SkinDepthWidget.cpp` lines 64–72), so the only
existing "tests" are the `#ifdef TEST_MODE` harnesses that drive the widgets and dump CSVs.

We have one enormous asset to de-risk the rewrite: **52 golden CSV fixtures** in
`resources/data/*.csv`. They already encode realistic input vectors for every calculator. The strategy
in this document is:

1. **Reuse those CSVs verbatim** as Qt-free regression vectors against the new `calculate()` functions.
2. **Re-bless** the small subset of golden outputs that were wrong *because of the old code's bugs*
   (`PI 3.14`, `SPEEDOFLIGHT 300000000.0`) — never silently, always with a documented reason.
3. **Add compile-time tests** (`static_assert` over `constexpr` math) for constants and closed-form
   calculators, so a wrong constant fails to *compile*.
4. **Add property/round-trip tests** for the bidirectional converters and solvers.
5. **Wire it all into CI** with sanitizers and coverage gates.

> Cross-cutting note: the calculator contract (Input struct, Result struct,
> `[[nodiscard]] std::expected<Result, Error> calculate(const Input&)`, `Calculator` concept) is
> defined in **06-calculator-design-pattern.md**. The error type is in **05-error-handling-and-validation.md**.
> Units live in **03-quantities-and-units-mp-units.md**. The ranges/`string_view` parsing techniques
> are catalogued in **02-modern-cpp-feature-catalog.md**. This document *consumes* all of those.

---

## 1. The asset: 52 golden CSV fixtures

### 1.1 What they are today

Each leaf widget in the old app contains, under `#ifdef TEST_MODE`, a `testPage(...)` method. At
construction it:

1. opens its fixture from the Qt resource system (`":/data/<Widget>.csv"`),
2. reads each row, splits on `,`,
3. programmatically drives the widget (`ui->frequency_spinbox->setValue(...)`,
   `ui->material_combobox->setCurrentText(...)`, `ui->solveButton->clicked()`),
4. reads back `ui->skinDepth->text()` and writes it to
   `applicationDirPath()/DataTesting/<Widget>.csv`.

The verbatim harness from `src/BasicCalculations/SkinDepth/SkinDepthWidget.cpp` (lines 86–133):

```cpp
#ifdef TEST_MODE
    QStringList list1, list2;
    QFile file(":/data/SkinDepthWidget.csv");
    file.open(QIODevice::ReadOnly);
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList list = line.split(",");
        list1.append(list.at(0));   // frequency (in MHz, per the UI default unit)
        list2.append(list.at(1));   // material name
    }
    file.close();
    testPage(list1, list2);
#endif
// ...
void SkinDepthWidget::testPage(const QStringList& l1, const QStringList& l2) {
    for (int i = 0; i < l1.size(); i++) {
        ui->clearButton->clicked();
        ui->frequency_spinbox->setValue(l1.at(i).toDouble());
        ui->material_combobox->setCurrentText(l2.at(i));
        ui->solveButton->clicked();
        fileContent.append(ui->skinDepth->text());   // result, in cm (UI default unit)
    }
    // ... writes DataTesting/SkinDepthWidget.csv
}
```

**Key observation that makes these reusable:** the input CSV rows are *pure domain inputs* — numbers and
material names — with **no Qt types in the data**. The Qt coupling is only in the *driver*, not the
*data*. We throw away the driver and keep the data.

There is one subtlety to record per fixture: **the unit convention is implicit in the old UI defaults.**
For SkinDepth the frequency column is in **MHz** (`ui->frequencyUnitBox->setCurrentText("MHz")`,
line 31) and the result is in **cm** (`ui->skinDepth_unit->setCurrentText("cm")`, line 32). The new
fixtures must capture these units explicitly (Section 1.4).

### 1.2 The input formats (exact, from the real files)

| Fixture (`resources/data/…`)   | Columns (left→right)                                   | Example row                          | Old implicit units            |
|--------------------------------|-------------------------------------------------------|--------------------------------------|-------------------------------|
| `SkinDepthWidget.csv`          | `frequency, material`                                  | `27,Nickel`                          | freq=MHz, result=cm           |
| `MicrostripTraceWidget.csv`    | `h, t, w, relativePermittivity`                        | `18.65392418,11.15918402,15.00320319,5` | h/t/w in mm, εr unitless   |
| `WavelengthvsFrequency.csv`    | `wavelength`                                           | `177.1613428`                        | wavelength=m → freq=MHz       |
| `EnergyVsFrequency.csv`        | `energy` (single column)                              | `177.1613428`                        | converter input               |
| `RectangularEnclosureWidget.csv` | `a, b, d, ...` (enclosure dims + εr)                | `86.86,2.47,88.37,4.3`               | dims in cm                    |
| `StandardGaugeWireWidget.csv`  | `len, current, AWG, material, ...`                    | `18.65…,11.15…,39,4,1`               | enum indices for material/unit|

Each file has **99 rows** (98 data rows; the harness reads to EOF). The header is *not* present in the
input CSV — the widget appends a column name only to the *output* CSV (`fileContent << "SkinDepth";`).

> Pitfall captured here: `MicrostripTraceWidget.csv` is `h,t,w,εr` (confirmed from
> `MicrostripTraceWidget.cpp` lines 67–70: `hList.append(list.at(0)); tList.append(list.at(1));
> wList.append(list.at(2)); relativePermittivityList.append(list.at(3));`). The old widget then
> multiplies by `39.37` (m→inch) inside an 8-branch unit tree depending on the mm/mils combobox. In the
> new fixtures the unit is explicit, so that branch disappears (see **03-…mp-units.md**).

### 1.3 Mapping a row → `Input` + expected `Result`

The contract from **06-calculator-design-pattern.md** for skin depth:

```cpp
namespace emc::basic {
struct SkinDepthInput {
    quantity<isq::frequency[si::hertz]>            frequency;
    quantity<isq::electrical_conductivity[si::siemens / si::metre]> conductivity;
    quantity<dimensionless[one]>                   relative_permeability{1};
};
struct SkinDepthResult { quantity<isq::length[si::metre]> skin_depth; };
[[nodiscard]] std::expected<SkinDepthResult, Error> calculate(const SkinDepthInput&);
}
```

A single CSV row `27,Nickel` maps as follows. The material name resolves through the **single material
database** from **04-constants-and-material-database.md** (which replaces the inline `if (material ==
"Nickel")` block at `SkinDepthWidget.cpp` lines 58–61):

```cpp
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

// Row "27,Nickel":  frequency column is MHz, material drives conductivity + µr.
const auto& nickel = emc::materials::by_name("Nickel").value();   // expected<…> from the DB
const SkinDepthInput in{
    .frequency             = 27.0 * MHz,
    .conductivity          = nickel.conductivity,                 // 1.4493e7 S/m
    .relative_permeability = nickel.relative_permeability,        // 600 (dimensionless)
};
const auto result = emc::basic::calculate(in);                    // expected<SkinDepthResult, Error>
// Expected output (from the OLD golden CSV) is in cm:
//   expected_cm := value at SkinDepthWidget golden row 0
```

The old golden *output* file stored one number per row in **cm**. mp-units lets us assert in *exactly*
that unit so the comparison is apples-to-apples (Section 4).

### 1.4 The new fixture schema: self-describing, units explicit

The old CSVs leak unit conventions into tribal knowledge ("frequency is MHz because the combobox
defaulted to MHz"). We promote each fixture to a **self-describing** form: a `vectors/<calc>.csv` with a
**header row naming the unit of each column**, plus the *expected* output columns inlined. This is the
"golden vector" format the new suite reads.

```text
# vectors/skin_depth.csv  — generated once from the old input CSV + freshly computed/blessed outputs
frequency[MHz],material,expected_skin_depth[cm],status
27,Nickel,0.000559…,reblessed:PI_3.14
6.2,Aluminum,0.003391…,ok
59.7,Silver,0.000275…,ok
```

```text
# vectors/microstrip_trace.csv
h[mm],t[mm],w[mm],eps_r,expected_z0[ohm],expected_c0[pF/m],expected_tpd[ns/m],status
18.65392418,11.15918402,15.00320319,5,…,…,…,ok
```

The `status` column (`ok` / `reblessed:<reason>`) is central to the re-blessing process in Section 7.
Keeping the *input* identical to the old CSV (same numbers) preserves the regression value; we only
*append* explicit units and expected outputs.

---

## 2. A data-driven harness (Qt-free)

The new harness is pure standard C++: parse with `std::ranges`/`std::views` + `std::string_view`
(techniques from **02-modern-cpp-feature-catalog.md**), build the `Input` with mp-units, call
`calculate()`, compare within tolerance.

### 2.1 CSV parsing with ranges + `string_view`

```cpp
// tests/support/csv.hpp  (test-only helper, header-only is fine here)
#include <ranges>
#include <string_view>
#include <vector>
#include <charconv>
#include <fstream>
#include <sstream>

namespace emc::test {

// One row = a vector of trimmed fields (zero-copy views into the owning line string).
struct Row {
    std::string line;                       // owns the storage
    std::vector<std::string_view> fields;   // views into `line`
};

[[nodiscard]] inline std::vector<std::string_view> split(std::string_view s, char delim = ',') {
    // C++23: views::split yields subranges; materialize as string_views.
    auto to_sv = [](auto&& sub) {
        return std::string_view{&*sub.begin(), std::ranges::distance(sub)};
    };
    return s | std::views::split(delim) | std::views::transform(to_sv)
             | std::ranges::to<std::vector>();          // C++23 ranges::to
}

[[nodiscard]] inline double to_double(std::string_view sv) {
    double v{};
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    if (ec != std::errc{})
        throw std::runtime_error("bad double in fixture: " + std::string{sv});
    return v;
}

// Reads a fixture, skips comment (#) and header lines, returns owning rows.
[[nodiscard]] inline std::vector<Row> read_fixture(const std::filesystem::path& p) {
    std::ifstream in{p};
    if (!in) throw std::runtime_error("cannot open fixture: " + p.string());
    std::vector<Row> rows;
    for (std::string line; std::getline(in, line);) {
        std::string_view sv{line};
        if (sv.empty() || sv.starts_with('#') || sv.starts_with("frequency[") ||
            sv.starts_with("h[")) continue;                         // skip header(s)
        Row r{.line = std::move(line)};
        r.fields = split(r.line);                                   // re-view the moved string
        rows.push_back(std::move(r));
    }
    return rows;
}

} // namespace emc::test
```

> **Why `std::string_view` + `std::from_chars`** (rationale tied to a pain point): the old code parses
> with `QString::split(",")` and `.toDouble()` — allocating a `QStringList` per line and dragging in Qt.
> `views::split` + `string_view` is allocation-free per field and Qt-free; `from_chars` is locale-
> independent (the old `toDouble()` is locale-sensitive, a real source of `,` vs `.` decimal bugs on
> non-US machines). See **02-…feature-catalog.md** for the full ranges treatment. Pitfall: a `string_view`
> field is only valid while its `Row::line` is alive — that is why `Row` owns the string and the views
> point into it.

### 2.2 A generic, parameterized golden runner

The `Calculator` concept (from **06**) lets one template drive *every* calculator. We supply, per
calculator, two small lambdas: `make_input(row)` and `expected(row)` — the only per-calculator code.

```cpp
// tests/support/golden_runner.hpp
#include "csv.hpp"
#include "emc/core/concepts.hpp"   // emc::Calculator
#include "emc/core/error.hpp"

namespace emc::test {

struct Tolerance { double rel = 1e-9; double abs = 1e-12; };

// Compare two quantities by extracting their numeric value IN A FIXED UNIT (see Section 4).
template <class Q>
[[nodiscard]] bool close(Q got, Q want, Tolerance tol) {
    const double g = got.numerical_value_in(want.unit);   // mp-units: same unit, raw double
    const double w = want.numerical_value_in(want.unit);
    const double diff = std::abs(g - w);
    return diff <= tol.abs || diff <= tol.rel * std::max(std::abs(g), std::abs(w));
}

// One reusable engine. `MakeInput` : Row -> Input ; `Expected` : (Row, Result) -> bool (asserts).
template <class MakeInput, class Check>
void run_golden(const std::filesystem::path& fixture, MakeInput make_input, Check check) {
    for (const auto& row : read_fixture(fixture)) {
        const auto in  = make_input(row);
        const auto out = emc::calculate(in);             // ADL picks the right calculate()
        // Most golden rows must succeed; validation failures are a separate, explicit test set.
        REQUIRE(out.has_value());                        // framework macro (Section 3)
        check(row, *out);
    }
}

} // namespace emc::test
```

A concrete SkinDepth golden test built on it (full body in Section 3) just supplies the two lambdas.
This keeps **52 fixtures** behind **one** engine — the polar opposite of 52 hand-rolled `testPage()`
methods.

---

## 3. Framework choice: **Catch2 v3** (recommended)

**Decision: use Catch2 v3.** Rationale, tied to this project's shape:

| Need                                  | Catch2 v3                                                  | GoogleTest                                  |
|---------------------------------------|-----------------------------------------------------------|---------------------------------------------|
| Data-driven golden rows               | `GENERATE(from_range(...))` is first-class & inline        | `TEST_P` + `INSTANTIATE_TEST_SUITE_P` (verbose) |
| FP comparisons                        | `Catch::Approx` / `WithinRel` / `WithinAbs` / `WithinULP`  | `EXPECT_NEAR` only (no ULP, no rel built-in)|
| CMake / CPM integration               | single `find_package(Catch2 3)` + `catch_discover_tests`   | `find_package(GTest)` + `gtest_discover_tests` |
| BDD / `SECTION` for round-trip tests  | `SECTION`s share setup naturally                           | fixtures, more boilerplate                  |
| Header weight                         | v3 is compiled (fast incremental builds)                   | compiled                                    |

Catch2's `WithinRel`/`WithinAbs`/`WithinULP` matchers map *exactly* onto Section 4's tolerance model,
and `GENERATE(from_range(...))` lets us feed parsed CSV rows directly as test parameters with one line —
which is why it wins here. (GoogleTest is a fine alternative; if the team already standardizes on it,
the `run_golden` engine in 2.2 is framework-agnostic and only the assertion macros change.)

### 3.1 A table-driven golden test in Catch2

```cpp
// tests/basic/skin_depth_golden.test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "emc/basic/skin_depth.hpp"
#include "emc/materials/database.hpp"
#include "support/csv.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using Catch::Matchers::WithinRel;

namespace {
struct SkinRow { double freq_MHz; std::string material; double expected_cm; std::string status; };

std::vector<SkinRow> load_rows() {
    std::vector<SkinRow> out;
    for (const auto& r : emc::test::read_fixture("vectors/skin_depth.csv")) {
        out.push_back({ emc::test::to_double(r.fields[0]),
                        std::string{r.fields[1]},
                        emc::test::to_double(r.fields[2]),
                        std::string{r.fields[3]} });
    }
    return out;
}
} // namespace

TEST_CASE("skin depth matches golden vectors", "[basic][skin_depth][golden]") {
    auto rows = load_rows();
    auto row  = GENERATE_REF(from_range(rows));          // one assertion per CSV row

    CAPTURE(row.freq_MHz, row.material, row.status);     // shown on failure

    const auto mat = emc::materials::by_name(row.material);
    REQUIRE(mat.has_value());

    const emc::basic::SkinDepthInput in{
        .frequency             = row.freq_MHz * MHz,
        .conductivity          = mat->conductivity,
        .relative_permeability = mat->relative_permeability,
    };

    const auto res = emc::basic::calculate(in);
    REQUIRE(res.has_value());

    // Compare in the SAME unit the golden file used (cm). mp-units guarantees the conversion.
    const double got_cm = res->skin_depth.numerical_value_in(cm);
    REQUIRE_THAT(got_cm, WithinRel(row.expected_cm, 1e-6));
}
```

> **Why `GENERATE_REF(from_range(...))`** (pain point): the old `testPage` is a hand-written `for` loop
> per widget (52 copies) that *appends to a file* and has no pass/fail concept — you eyeball the output
> CSV. `GENERATE` makes each CSV row an independent, named, individually-reported test case; a single bad
> row is pinpointed (`CAPTURE` prints `freq_MHz`, `material`) instead of failing the whole file.

### 3.2 The error-path tests (validation)

Golden rows test the *happy path*. The `validate()` half of the contract (from **05**) gets its own
tiny table — values that must be **rejected** with a specific `ErrorCode`:

```cpp
TEST_CASE("skin depth rejects invalid input", "[basic][skin_depth][validation]") {
    using emc::ErrorCode;
    struct Case { quantity<isq::frequency[si::hertz]> f; ErrorCode code; };
    auto c = GENERATE(values<Case>({
        { 0.0 * Hz,  ErrorCode::OutOfRange },     // f must be > 0 (old code would divide → inf)
        { -1.0 * Hz, ErrorCode::OutOfRange },
    }));
    emc::basic::SkinDepthInput in{ .f = c.f, .conductivity = 5.8e7 * (si::siemens/si::metre) };
    const auto res = emc::basic::calculate(in);
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error().code == c.code);
}
```

This replaces the old inline `QMessageBox::warning(...)` validation (10 files; cf.
`MicrostripTraceWidget.cpp` permittivity 1..15 / W:H checks) with a *testable* contract — see **05**.

---

## 4. Floating-point comparison

### 4.1 The model: relative OR absolute, plus ULP for the strict cases

Never compare floats with `==`. Use **relative tolerance with an absolute floor** (so values near zero
don't blow up the relative term):

```
pass  ⇔  |got − want| ≤ abs_tol            (handles values near 0)
      OR  |got − want| ≤ rel_tol · max(|got|, |want|)
```

This is exactly the `close()` helper in 2.2 and Catch2's `WithinAbs || WithinRel`. For results that
should be *bit-stable* across platforms (pure constants, simple closed forms) use **ULP** comparison:

```cpp
REQUIRE_THAT(got, Catch::Matchers::WithinULP(want, /*maxUlpDiff=*/4));
```

### 4.2 Choosing the tolerance

| Situation                                                            | Suggested tolerance        |
|---------------------------------------------------------------------|----------------------------|
| Compare to **freshly computed** reference (new code vs analytic)     | `WithinRel(1e-12)`         |
| Compare to **old golden CSV** (text round-tripped, ~10 sig figs)     | `WithinRel(1e-6)`          |
| Multi-step formula with `exp`/`log`/`pow` (Shielding, ESD coupling)  | `WithinRel(1e-9)`          |
| Sum/cancellation-prone (resonant-mode diffs)                         | add `WithinAbs(1e-9)` floor|
| Bit-stable constants / closed form                                  | `WithinULP(2..4)`          |

Rationale for the `1e-6` on old golden files: the old `ui->skinDepth->text()` was a **formatted string**
(spinbox display precision), so the golden value already lost digits. Demanding `1e-12` against a
6–10-significant-figure text value would produce false failures. Tighter tolerances are reserved for
references we compute ourselves at full `double` precision.

### 4.3 Where mp-units makes FP comparison *safer*

The killer feature: **compare quantities in a fixed unit, with the conversion compile-checked.**

```cpp
// Old code: result is "skin depth in cm", an untyped double whose unit lives in a combobox.
// New code: result is quantity<isq::length[m]>. Pick the comparison unit explicitly:
const double got_cm = res->skin_depth.numerical_value_in(cm);   // ill-formed if not a length
REQUIRE_THAT(got_cm, WithinRel(expected_cm, 1e-6));
```

`numerical_value_in(cm)` **won't compile** if `skin_depth` isn't a length, so a whole class of
unit-mismatch test bugs (compare metres to cm, the old `*39.37` confusion) is *impossible*. It also
removes the temptation to bake unit factors into the tolerance. See **03-…mp-units.md** §"extracting
raw values".

> **Why this matters here** (pain point): the old MicrostripTrace test compared raw doubles where the
> unit depended on an 8-branch mm/mils tree (`*39.37`, `MicrostripTraceWidget.cpp` lines 88–105). With
> mp-units the fixture says `w[mm]`, the calculator returns a `length`, and the test extracts `in(mm)` —
> the branch and the magic factor are gone, and the comparison can't silently be off by 39.37×.

---

## 5. Compile-time tests (`constexpr` + `static_assert`)

C++23 makes `<cmath>` `constexpr` (`std::sqrt`, `std::exp`, `std::log`, `std::pow`), so the simpler
closed-form calculators can be evaluated **at compile time** and asserted with `static_assert`. A wrong
constant then fails the *build*, not a test run — the strongest possible regression guard.

### 5.1 Constants

```cpp
// tests/constants_compiletime.test.cpp
#include "emc/constants/constants.hpp"
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

// The OLD bugs we are eradicating, asserted to be GONE:
static_assert(emc::constants::pi > 3.14159 && emc::constants::pi < 3.14160,
              "pi must be real pi, NOT the #define PI 3.14 from HelperTypes.h");

static_assert(emc::constants::c.numerical_value_in(m/s) == 299'792'458.0,
              "speed of light must be exact c, NOT SPEEDOFLIGHT 300000000.0");

// mu0 defined ONCE (replaces the 8+ copies of `qreal mu0 = 4*M_PI*1e-7;`)
static_assert(emc::constants::mu_0.numerical_value_in(si::henry/m) > 1.2566e-6,
              "vacuum permeability single source of truth");
```

> **Why `static_assert` over `constexpr`** (pain point, with numbers): `HelperTypes.h` ships
> `#define PI 3.14` (a precision *bug*), `#define SPEEDOFLIGHT 300000000.0` (off by 207542 m/s), and
> `mu0 = 4*M_PI*1e-7` is re-declared in **8+** files. A `static_assert` over the single `constexpr`
> constant (from **04-constants-and-material-database.md**) makes those bugs un-reintroducible: any
> regression to `3.14` breaks compilation everywhere at once.

### 5.2 A closed-form calculator at compile time

If `calculate()` is `constexpr` (and it should be for the pure ones — see **06**), a known analytic
point can be a `static_assert`:

```cpp
// Skin depth of copper at 1 MHz ≈ 66.1 µm (analytic).  delta = 1/sqrt(pi·f·mu·sigma)
constexpr auto copper_1MHz = [] {
    emc::basic::SkinDepthInput in{
        .frequency    = 1.0 * si::mega<si::hertz>,
        .conductivity = 5.8005e7 * (si::siemens / si::metre),
        .relative_permeability = 0.999991 * one,
    };
    return emc::basic::calculate(in).value().skin_depth;  // expected<…>::value() is constexpr in C++23
}();

static_assert(copper_1MHz.numerical_value_in(si::micro<si::metre>) > 66.0 &&
              copper_1MHz.numerical_value_in(si::micro<si::metre>) < 66.3,
              "copper skin depth at 1 MHz must be ~66 um");
```

> **Why C++23 `constexpr <cmath>` + `constexpr std::expected`**: it lets the *exact same* `calculate()`
> run at compile time and runtime — no separate "test formula". The compile-time path also proves the
> function is genuinely pure (no global state, no I/O), which is one of the locked design goals. Pitfall:
> `mp-units` quantity arithmetic is already `constexpr`; just ensure your calculator body avoids
> non-`constexpr` calls (no logging, no allocation).

---

## 6. Property-based & round-trip tests

Golden vectors prove "same as before"; property tests prove "internally consistent" — they catch bugs
the golden set never exercised. Three families fit NinjaEMC directly.

### 6.1 Round-trip identity (the converters)

`Wavelength↔Frequency` and `Energy↔Frequency` are exact inverses. λ→f→λ must return the input
(within tolerance). This directly tests `WavelengthvsFrequency.cpp`'s bidirectional logic.

```cpp
TEST_CASE("wavelength↔frequency is an involution", "[converter][property][roundtrip]") {
    // Feed the EXISTING golden inputs as the property domain (reuse the asset).
    for (const auto& r : emc::test::read_fixture("vectors/wavelength_freq.csv")) {
        const auto lambda0 = emc::test::to_double(r.fields[0]) * m;

        const auto f   = emc::converter::wavelength_to_frequency({.wavelength = lambda0});
        REQUIRE(f.has_value());
        const auto l2  = emc::converter::frequency_to_wavelength({.frequency = f->frequency});
        REQUIRE(l2.has_value());

        REQUIRE_THAT(l2->wavelength.numerical_value_in(m),
                     Catch::Matchers::WithinRel(lambda0.numerical_value_in(m), 1e-12));
    }
}
```

> **Why round-trip** (pain point): the old converter used `SPEEDOFLIGHT 300000000.0` in *both*
> directions, so λ→f→λ happened to round-trip *despite* the wrong `c` (the error cancels). The new code
> uses exact `c`; the round-trip test confirms the *new* pair is self-consistent, while the golden test
> (Section 7) is where the absolute λ→f value gets re-blessed against the corrected constant.

### 6.2 Solve-forward / solve-back consistency (MicrostripTrace)

`MicrostripTraceWidget` has four copy-pasted solvers (`microstrip`/`calH`/`calT`/`calW`) — solve Z0
from (H,T,W), or W from (H,T,Z0), etc. The library replaces these with named functions (see **06**),
and the property is: solving forward then back returns the original geometry.

```cpp
TEST_CASE("microstrip Z0(W) and W(Z0) are mutually consistent", "[component][property]") {
    for (const auto& r : emc::test::read_fixture("vectors/microstrip_trace.csv")) {
        const emc::component::MicrostripInput geom{
            .height      = emc::test::to_double(r.fields[0]) * mm,
            .thickness   = emc::test::to_double(r.fields[1]) * mm,
            .width       = emc::test::to_double(r.fields[2]) * mm,
            .eps_r       = emc::test::to_double(r.fields[3]) * one,
        };
        const auto fwd = emc::component::microstrip_impedance(geom);     // Z0 from W
        REQUIRE(fwd.has_value());

        const auto back = emc::component::microstrip_width_for_z0({       // W from Z0
            .height = geom.height, .thickness = geom.thickness,
            .eps_r = geom.eps_r,  .z0 = fwd->z0 });
        REQUIRE(back.has_value());

        // Solving back for W should recover the input width (looser tol: iterative/closed-form mix).
        REQUIRE_THAT(back->width.numerical_value_in(mm),
                     Catch::Matchers::WithinRel(geom.width.numerical_value_in(mm), 1e-4));
    }
}
```

### 6.3 Monotonicity & physical-sign invariants

Cheap, high-value sanity laws that hold for *any* input — generate random in-range inputs and assert:

```cpp
TEST_CASE("skin depth shrinks as frequency rises", "[basic][property][monotonic]") {
    auto sigma = 5.8e7 * (si::siemens / si::metre);
    auto d = [&](double f_MHz) {
        return emc::basic::calculate({.frequency = f_MHz*MHz, .conductivity = sigma})
                 .value().skin_depth.numerical_value_in(si::micro<si::metre>);
    };
    REQUIRE(d(1.0)  > d(10.0));     // δ ∝ 1/√f  → strictly decreasing
    REQUIRE(d(10.0) > d(100.0));
    REQUIRE(d(1.0)  > 0.0);         // physically positive
}
```

Other useful invariants to encode: decibel(x) is monotone in x; VSWR ≥ 1 always; shielding
effectiveness ≥ 0 dB for a real shield; resonant-mode frequencies are ordered (`f110 ≤ f111 ≤ …` in
`RectangularEnclosureWidget`). These need *no* golden file — they encode physics.

---

## 7. Re-blessing process (the bug-aware golden import)

**Problem:** some golden outputs in the old CSVs are *wrong* because the old code used `PI 3.14`,
`SPEEDOFLIGHT 300000000.0`, and inconsistent material tables. If we blindly demand the new code match
them, we'd be forcing the new code to *reproduce the bugs*. If we blindly overwrite, we lose all
regression value. The answer is a **deliberate, audited re-blessing workflow**.

### 7.1 The rule

> **Never silently overwrite a golden value.** Every divergence from the imported old output is either
> (a) a known fixed bug → re-bless with a documented reason, or (b) an unexplained difference → a
> *failure* that blocks the migration until understood.

### 7.2 The workflow

```text
Step 1  IMPORT      Copy each resources/data/<W>.csv input column unchanged into vectors/<calc>.csv.
                    Run the OLD app once (TEST_MODE) to capture old outputs → expected column.
                    Mark every row status = "baseline".

Step 2  RUN NEW     Run the new suite with tolerance WithinRel(1e-6) against the baseline expecteds.

Step 3  TRIAGE      For each failing row, classify the cause:
                      • PI 3.14            → ratio ≈ (3.14159265/3.14)^k for some power k
                      • c = 3e8 vs exact   → ratio ≈ 299792458/300000000 = 0.99930819…
                      • material table mismatch (per 04-…materials)
                      • UNKNOWN            → STOP. This is a real regression. Do not bless.

Step 4  RE-BLESS    For each KNOWN-cause row: replace expected with the new value, set
                    status = "reblessed:<reason>", and add a one-line note to vectors/CHANGELOG.md
                    citing the old constant and the corrected one.

Step 5  LOCK        Commit. CI now treats "reblessed:*" rows like any other golden row, but the
                    status column + CHANGELOG record WHY they differ from the shipped app.
```

### 7.3 A diff/classify helper to make triage mechanical

```cpp
// tools/rebless/classify.cpp  (dev tool, not part of libemc)
enum class Cause { ExactMatch, PiBug, SpeedOfLightBug, MaterialTable, Unknown };

Cause classify(double old_val, double new_val) {
    if (emc::test::close(old_val, new_val, {.rel = 1e-6}))            return Cause::ExactMatch;
    const double ratio = old_val / new_val;
    // c bug: every length/frequency conversion scales by 300e6/299792458 per power of c used.
    for (int k : {1, 2, -1, -2})
        if (std::abs(ratio - std::pow(300'000'000.0 / 299'792'458.0, k)) < 1e-6)
            return Cause::SpeedOfLightBug;
    // PI bug: ratio is a power of (3.14159265.../3.14).
    for (int k : {1, 2, -1, -2})
        if (std::abs(ratio - std::pow(3.14 / std::numbers::pi, k)) < 1e-4)
            return Cause::PiBug;
    return Cause::Unknown;   // → flagged for human review
}
```

The classifier turns "207 rows differ" into "198 explained by the `c` bug, 7 by `PI 3.14`, 2 UNKNOWN —
investigate these 2." Only after the UNKNOWN bucket is empty (or each explained) may a fixture be locked.

### 7.4 Worked example: SkinDepth Nickel row

Row `27,Nickel`: old output used `mu0 = 4*M_PI*1e-7` (fine) and `M_PI` (fine in this widget — note
SkinDepth uses `M_PI`, not the broken `PI 3.14`). So SkinDepth rows should match at `1e-6` and stay
`status=ok`. By contrast `WavelengthvsFrequency.csv` rows used `SPEEDOFLIGHT 3e8` and will *all*
re-bless with `reblessed:c_3e8` (ratio `0.99930819…`). Recording this per-fixture is exactly what the
`status` column is for.

---

## 8. CI integration, sanitizers, coverage, fuzzing

### 8.1 CTest wiring (CMake)

Tests live in `tests/` and register with CTest via Catch2's discovery (full build setup in
**08-build-system-cmake.md**):

```cmake
# tests/CMakeLists.txt
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(emc_tests
    basic/skin_depth_golden.test.cpp
    component/microstrip_property.test.cpp
    converter/wavelength_roundtrip.test.cpp
    constants_compiletime.test.cpp
    # ... one or more files per calculator
)
target_link_libraries(emc_tests PRIVATE emc::emc Catch2::Catch2WithMain)
target_compile_features(emc_tests PRIVATE cxx_std_23)

# Make the golden vectors available next to the test binary.
file(COPY ${CMAKE_SOURCE_DIR}/tests/vectors DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

catch_discover_tests(emc_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
```

### 8.2 Sanitizers (a dedicated CI build type)

```cmake
# Enable with -DEMC_SANITIZE=address,undefined on a CI job.
if(EMC_SANITIZE)
    target_compile_options(emc_tests PRIVATE -fsanitize=${EMC_SANITIZE} -fno-omit-frame-pointer -g)
    target_link_options(emc_tests    PRIVATE -fsanitize=${EMC_SANITIZE})
endif()
```

Run the suite under **ASan+UBSan** (catches the `1/(pi·f·…)` divide-by-zero and any overflow that the
old `QMessageBox` guards used to mask) and, in a separate job, under **TSan** (proves the "thread-safe
by construction / no global mutable state" design goal — a regression that adds a `static` cache would
trip TSan).

### 8.3 GitHub Actions matrix (sketch)

```yaml
# .github/workflows/ci.yml
jobs:
  test:
    strategy:
      matrix:
        compiler: [gcc-14, clang-19]      # both need full C++23 + constexpr <cmath>
        sanitize: ["", "address,undefined"]
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake --preset ci -DEMC_SANITIZE=${{ matrix.sanitize }}
      - name: Build
        run: cmake --build --preset ci
      - name: Test
        run: ctest --preset ci --output-on-failure
```

> **Why a compiler matrix**: C++23 `constexpr <cmath>` and `std::expected`/`std::print` support differ
> across GCC and Clang versions. The compile-time tests in Section 5 are the canary — if a toolchain
> can't `constexpr`-evaluate `calculate()`, that job fails fast. Pin minimums that ship the needed
> library features (and `mp-units`'s requirements; see **03**).

### 8.4 Coverage targets

```cmake
if(EMC_COVERAGE)
    target_compile_options(emc PUBLIC --coverage -O0 -g)
    target_link_options(emc    PUBLIC --coverage)
endif()
```

Use `gcovr`/`llvm-cov` to report **line + branch** coverage. Targets:

| Layer                                | Target line coverage |
|--------------------------------------|----------------------|
| `emc::*` calculators (the 44 math ones) | ≥ 95 %            |
| `emc::materials` / `emc::constants`  | 100 % (tiny, data)   |
| `emc::detail` helpers                | ≥ 90 %               |

Branch coverage matters most for the `validate()` functions (every rejected-range branch should have a
test in 3.2). Gate the PR if coverage drops below threshold.

### 8.5 Optional: fuzzing the parser & benchmarking

The CSV/`from_chars` parser is the only place that touches untrusted-ish text; a small libFuzzer target
hardens it (and is a natural ASan companion):

```cpp
// tests/fuzz/fuzz_csv.cpp  — built only with -DEMC_FUZZ=ON, clang + -fsanitize=fuzzer,address
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string_view sv{reinterpret_cast<const char*>(data), size};
    try { (void)emc::test::split(sv); } catch (...) {}   // must never crash/UB
    return 0;
}
```

Optionally add micro-benchmarks (Catch2 `BENCHMARK`) on the hottest calculators to catch accidental
pessimization (e.g. someone reintroducing per-call allocation). Benchmarks are informational, not a CI
gate.

---

## 9. Putting it together: the test directory layout

```text
emcpp/
├── tests/
│   ├── CMakeLists.txt
│   ├── support/
│   │   ├── csv.hpp                 # ranges/string_view parser (Section 2.1)
│   │   └── golden_runner.hpp       # generic engine + close() (Section 2.2, 4.1)
│   ├── vectors/                    # NEW self-describing golden CSVs (Section 1.4)
│   │   ├── skin_depth.csv
│   │   ├── microstrip_trace.csv
│   │   ├── wavelength_freq.csv
│   │   ├── … (52 total, one per calculator)
│   │   └── CHANGELOG.md            # re-blessing audit log (Section 7.2)
│   ├── basic/      *.test.cpp      # golden + property + validation per calc
│   ├── component/  *.test.cpp
│   ├── converter/  *.test.cpp
│   ├── …                           # one subdir per category namespace
│   ├── constants_compiletime.test.cpp
│   └── fuzz/fuzz_csv.cpp           # optional
└── tools/
    └── rebless/classify.cpp        # dev tool (Section 7.3)
```

Each of the 52 fixtures yields at minimum: **(1)** a golden table test, plus, where applicable,
**(2)** a property/round-trip test and **(3)** a validation (error-path) test. That is the
**definition of "done" for a calculator's tests** referenced by **10-migration-roadmap.md**.

---

## 10. Checklist (per-calculator test exit criteria)

- [ ] Old `resources/data/<W>.csv` inputs imported unchanged into `tests/vectors/<calc>.csv`.
- [ ] Explicit unit headers added to every column.
- [ ] Old outputs captured as baseline; each row classified (`ok` / `reblessed:<reason>` / investigated).
- [ ] `CHANGELOG.md` entry for every `reblessed:*` row, citing old vs corrected constant.
- [ ] Golden table test passes at the chosen tolerance (Section 4.2).
- [ ] Validation test covers every `validate()` rejection branch.
- [ ] Round-trip / property test where the calculator is bidirectional or has a known invariant.
- [ ] At least one `static_assert` compile-time check if `calculate()` is `constexpr`.
- [ ] Green under ASan+UBSan; ≥ 95 % line coverage for the calculator.

---

## Cross-references

- **02-modern-cpp-feature-catalog.md** — `std::ranges`/`views::split`, `std::string_view`,
  `std::from_chars`, `constexpr`, `static_assert` rationales used throughout the harness.
- **03-quantities-and-units-mp-units.md** — `numerical_value_in(unit)`, why comparing quantities in a
  fixed unit removes whole classes of test bugs (Section 4.3).
- **04-constants-and-material-database.md** — the single `constexpr` constants + material DB that the
  compile-time tests (Section 5) and `materials::by_name` lookups (Section 1.3) assert against.
- **05-error-handling-and-validation.md** — `emc::Error` / `ErrorCode` checked by the validation tests
  (Section 3.2).
- **06-calculator-design-pattern.md** — the Input/Result/`calculate`/`validate`/`Calculator`-concept
  contract the generic runner (Section 2.2) depends on.
- **07-calculator-inventory.md** — the work-list mapping each of the 52 fixtures to its calculator.
- **08-build-system-cmake.md** — full CMake/CTest/Catch2 wiring, presets, and the sanitizer/coverage
  options sketched in Section 8.
- **10-migration-roadmap.md** — uses the per-calculator test checklist (Section 10) as a phase exit
  criterion / definition of done.
