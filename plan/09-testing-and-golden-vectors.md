# Testing Strategy 🧪

> Purpose: a fast, deterministic, GUI-free regression suite for the pure `emc::*::calculate()`
> functions, layered with compile-time, property, and CI checks so every formula is provably correct
> against established EMC references.

The suite rests on four pillars:

1. **Known-value tests** — hand-computed / textbook closed-form results.
2. **Property tests** — round-trip, monotonicity, and physical-sign invariants.
3. **Compile-time tests** — `static_assert` over `constexpr` math, so a wrong constant fails the build.
4. **Edge / validation tests** — out-of-domain inputs rejected with a specific `ErrorCode`.

All four run under the Catch2 v3 harness with explicit tolerances and a CI matrix.

> [!NOTE]
> The calculator contract (`Input`/`Result` structs,
> `[[nodiscard]] std::expected<Result, Error> calculate(const Input&)`, the `Calculator` concept) is
> defined in **06-calculator-design-pattern.md**; the error type in **05-error-handling-and-validation.md**;
> units in **03-quantities-and-units-mp-units.md**. This document consumes all of those.

---

## 1. Reference vectors: self-describing, units explicit

Test vectors live in `tests/vectors/<calc>.csv`, each a **self-describing** file: a header row naming
the unit of every column, the domain inputs, and the expected outputs computed by hand from the
closed-form formula or taken from a textbook worked example.

```text
# vectors/skin_depth.csv  — expected values hand-computed from delta = 1/sqrt(pi*f*mu*sigma)
frequency[MHz],material,expected_skin_depth[cm]
27,Nickel,0.000559
6.2,Aluminum,0.003391
59.7,Silver,0.000275
```

```text
# vectors/microstrip_trace.csv  — expected Z0 from the standard Wheeler/IPC microstrip closed form
h[mm],t[mm],w[mm],eps_r,expected_z0[ohm],expected_c0[pF/m],expected_tpd[ns/m]
1.6,0.035,3.0,4.3,50.12,113.4,5.97
```

Encoding the unit of each column in the header keeps the data free of tribal knowledge: the test reads
`frequency[MHz]` and constructs `27.0 * MHz` directly, with no implicit convention.

> [!IMPORTANT]
> Expected outputs are reference values, never captured from a running front end. Each value is a
> hand computation or a documented textbook example, so a passing test means the formula matches the
> established EMC engineering result, not merely some prior implementation.

### 1.1 Mapping a row → `Input` + expected `Result`

The contract for skin depth (from **06-calculator-design-pattern.md**):

```c++
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

A row `27,Nickel` maps through the **material database** from **04-constants-and-material-database.md**,
which is the single source of truth for `conductivity` and `relative_permeability`:

```c++
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

// Row "27,Nickel": frequency column is MHz, material drives conductivity + mu_r.
const auto& nickel = emc::materials::by_name("Nickel").value();   // expected<…> from the DB
const SkinDepthInput in{
    .frequency             = 27.0 * MHz,
    .conductivity          = nickel.conductivity,                 // 1.4493e7 S/m
    .relative_permeability = nickel.relative_permeability,        // 600 (dimensionless)
};
const auto result = emc::basic::calculate(in);                    // expected<SkinDepthResult, Error>
```

mp-units lets us assert in *exactly* the unit the reference uses (cm here), so the comparison is
apples-to-apples (Section 4).

---

## 2. A data-driven harness (GUI-free)

The harness is pure standard C++: parse with `std::ranges`/`std::views` + `std::string_view`
(techniques from **02-modern-cpp-feature-catalog.md**), build the `Input` with mp-units, call
`calculate()`, compare within tolerance.

### 2.1 CSV parsing with ranges + `string_view`

```c++
// tests/support/csv.hpp  (test-only helper, header-only)
#include <ranges>
#include <string_view>
#include <vector>
#include <charconv>
#include <fstream>
#include <filesystem>
#include <stdexcept>

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

// Reads a vector file, skips comment (#) and header lines, returns owning rows.
[[nodiscard]] inline std::vector<Row> read_fixture(const std::filesystem::path& p) {
    std::ifstream in{p};
    if (!in) throw std::runtime_error("cannot open fixture: " + p.string());
    std::vector<Row> rows;
    for (std::string line; std::getline(in, line);) {
        std::string_view sv{line};
        if (sv.empty() || sv.starts_with('#') ||
            std::ranges::any_of(sv, [](char c){ return c == '['; }))  // header row carries unit tags
            continue;
        Row r{.line = std::move(line)};
        r.fields = split(r.line);                                   // re-view the moved string
        rows.push_back(std::move(r));
    }
    return rows;
}

} // namespace emc::test
```

> [!TIP]
> **Why `std::string_view` + `std::from_chars`:** `views::split` over a `string_view` is allocation-free
> per field, and `from_chars` is locale-independent — it always reads `.` as the decimal point regardless
> of the host locale, removing a class of `,` vs `.` parsing bugs. See **02-…feature-catalog.md** for the
> full ranges treatment. A `string_view` field is only valid while its `Row::line` is alive, which is why
> `Row` owns the string and the views point into it.

### 2.2 A generic, parameterized runner

The `Calculator` concept (from **06**) lets one template drive *every* calculator. Per calculator we
supply two small lambdas — `make_input(row)` and a `check(row, result)` — the only per-calculator code.

```c++
// tests/support/vector_runner.hpp
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

// One reusable engine. `MakeInput` : Row -> Input ; `Check` : (Row, Result) -> void (asserts).
template <class MakeInput, class Check>
void run_vectors(const std::filesystem::path& fixture, MakeInput make_input, Check check) {
    for (const auto& row : read_fixture(fixture)) {
        const auto in  = make_input(row);
        const auto out = emc::calculate(in);             // ADL picks the right calculate()
        REQUIRE(out.has_value());                        // framework macro (Section 3)
        check(row, *out);
    }
}

} // namespace emc::test
```

A concrete SkinDepth known-value test (Section 3) supplies only the two lambdas, keeping every vector
file behind one engine.

---

## 3. Framework choice: Catch2 v3

| Need                                  | Catch2 v3                                                  | GoogleTest                                  |
|---------------------------------------|-----------------------------------------------------------|---------------------------------------------|
| Data-driven rows                      | `GENERATE(from_range(...))` is first-class & inline        | `TEST_P` + `INSTANTIATE_TEST_SUITE_P` (verbose) |
| FP comparisons                        | `Catch::Approx` / `WithinRel` / `WithinAbs` / `WithinULP`  | `EXPECT_NEAR` only (no ULP, no rel built-in)|
| CMake integration                     | `find_package(Catch2 3)` + `catch_discover_tests`          | `find_package(GTest)` + `gtest_discover_tests` |
| `SECTION` for round-trip tests        | `SECTION`s share setup naturally                           | fixtures, more boilerplate                  |

`WithinRel`/`WithinAbs`/`WithinULP` map exactly onto Section 4's tolerance model, and
`GENERATE(from_range(...))` feeds parsed rows directly as test parameters in one line. The `run_vectors`
engine in 2.2 is framework-agnostic, so only the assertion macros change if the team standardizes on a
different framework.

### 3.1 A table-driven known-value test in Catch2

```c++
// tests/basic/skin_depth_known.test.cpp
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
struct SkinRow { double freq_MHz; std::string material; double expected_cm; };

std::vector<SkinRow> load_rows() {
    std::vector<SkinRow> out;
    for (const auto& r : emc::test::read_fixture("vectors/skin_depth.csv")) {
        out.push_back({ emc::test::to_double(r.fields[0]),
                        std::string{r.fields[1]},
                        emc::test::to_double(r.fields[2]) });
    }
    return out;
}
} // namespace

TEST_CASE("skin depth matches reference values", "[basic][skin_depth][known]") {
    auto rows = load_rows();
    auto row  = GENERATE_REF(from_range(rows));          // one assertion per row

    CAPTURE(row.freq_MHz, row.material);                 // shown on failure

    const auto mat = emc::materials::by_name(row.material);
    REQUIRE(mat.has_value());

    const emc::basic::SkinDepthInput in{
        .frequency             = row.freq_MHz * MHz,
        .conductivity          = mat->conductivity,
        .relative_permeability = mat->relative_permeability,
    };

    const auto res = emc::basic::calculate(in);
    REQUIRE(res.has_value());

    // Compare in the SAME unit the reference used (cm). mp-units guarantees the conversion.
    const double got_cm = res->skin_depth.numerical_value_in(cm);
    REQUIRE_THAT(got_cm, WithinRel(row.expected_cm, 1e-6));
}
```

> [!TIP]
> `GENERATE_REF(from_range(...))` makes each row an independent, named, individually-reported test case,
> so a single bad row is pinpointed (`CAPTURE` prints `freq_MHz`, `material`) instead of failing the
> whole file.

### 3.2 Edge / validation tests

Known-value rows test the happy path. The `validate()` half of the contract (from **05**) gets its own
table — values that must be **rejected** with a specific `ErrorCode`:

```c++
TEST_CASE("skin depth rejects invalid input", "[basic][skin_depth][validation]") {
    using emc::ErrorCode;
    struct Case { quantity<isq::frequency[si::hertz]> f; ErrorCode code; };
    auto c = GENERATE(values<Case>({
        { 0.0 * Hz,  ErrorCode::OutOfRange },     // f must be > 0 (1/(pi*f*…) is undefined at 0)
        { -1.0 * Hz, ErrorCode::OutOfRange },
    }));
    emc::basic::SkinDepthInput in{ .frequency = c.f,
                                   .conductivity = 5.8e7 * (si::siemens/si::metre) };
    const auto res = emc::basic::calculate(in);
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error().code == c.code);
}
```

Validation lives in the calculator contract and is exercised directly here — every rejection branch
(e.g. MicrostripTrace permittivity range, W:H ratio) gets a row. See **05**.

---

## 4. Floating-point comparison

### 4.1 The model: relative OR absolute, plus ULP for the strict cases

Never compare floats with `==`. Use **relative tolerance with an absolute floor** so values near zero
don't blow up the relative term:

```text
pass  <=>  |got - want| <= abs_tol            (handles values near 0)
      OR   |got - want| <= rel_tol * max(|got|, |want|)
```

This is the `close()` helper in 2.2 and Catch2's `WithinAbs || WithinRel`. For results that should be
*bit-stable* across platforms (pure constants, simple closed forms) use **ULP** comparison:

```c++
REQUIRE_THAT(got, Catch::Matchers::WithinULP(want, /*maxUlpDiff=*/4));
```

### 4.2 Choosing the tolerance

| Situation                                                            | Suggested tolerance        |
|---------------------------------------------------------------------|----------------------------|
| Compare to **full-precision** analytic reference                     | `WithinRel(1e-12)`         |
| Compare to **textbook value** quoted to ~6 sig figs                  | `WithinRel(1e-6)`          |
| Multi-step formula with `exp`/`log`/`pow` (Shielding, ESD coupling)  | `WithinRel(1e-9)`          |
| Sum/cancellation-prone (resonant-mode diffs)                         | add `WithinAbs(1e-9)` floor|
| Bit-stable constants / closed form                                  | `WithinULP(2..4)`          |

A textbook value quoted to six significant figures cannot support a `1e-12` demand without false
failures; tight tolerances are reserved for references we compute ourselves at full `double` precision.

### 4.3 Where mp-units makes FP comparison safer

The key feature: **compare quantities in a fixed unit, with the conversion compile-checked.**

```c++
// result is quantity<isq::length[m]>. Pick the comparison unit explicitly:
const double got_cm = res->skin_depth.numerical_value_in(cm);   // ill-formed if not a length
REQUIRE_THAT(got_cm, WithinRel(expected_cm, 1e-6));
```

`numerical_value_in(cm)` **won't compile** if `skin_depth` isn't a length, so a whole class of
unit-mismatch test bugs (comparing metres to cm) is impossible, and there's no temptation to bake unit
factors into the tolerance. See **03-…mp-units.md** §"extracting raw values".

> [!IMPORTANT]
> A vector file says `w[mm]`, the calculator returns a `length`, and the test extracts `in(mm)`. The
> unit is declared once in the header and enforced by the type system end to end, so a comparison can
> never be silently off by a unit factor.

---

## 5. Compile-time tests (`constexpr` + `static_assert`)

C++23 makes `<cmath>` `constexpr` (`std::sqrt`, `std::exp`, `std::log`, `std::pow`), so the simpler
closed-form calculators can be evaluated **at compile time** and asserted with `static_assert`. A wrong
constant then fails the *build*, not a test run — the strongest possible regression guard.

### 5.1 Constants

```c++
// tests/constants_compiletime.test.cpp
#include "emc/constants/constants.hpp"
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

static_assert(emc::constants::pi > 3.14159 && emc::constants::pi < 3.14160,
              "pi must equal std::numbers::pi to full double precision");

static_assert(emc::constants::c.numerical_value_in(m/s) == 299'792'458.0,
              "speed of light is the exact SI defining constant");

static_assert(emc::constants::mu_0.numerical_value_in(si::henry/m) > 1.2566e-6,
              "vacuum permeability: single source of truth");
```

> [!TIP]
> **Why `static_assert` over `constexpr`:** the constants are defined exactly once
> (**04-constants-and-material-database.md**), and a compile-time assertion makes any regression in their
> value break the build everywhere at once — far stronger than a runtime check that only fires when a
> test happens to run.

### 5.2 A closed-form calculator at compile time

If `calculate()` is `constexpr` (and it is for the pure calculators — see **06**), a known analytic
point can be a `static_assert`:

```c++
// Skin depth of copper at 1 MHz ~ 66.1 um (analytic). delta = 1/sqrt(pi*f*mu*sigma)
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

> [!NOTE]
> C++23 `constexpr <cmath>` + `constexpr std::expected` let the *exact same* `calculate()` run at compile
> time and runtime — there is no separate "test formula". The compile-time path also proves the function
> is genuinely pure (no global state, no I/O), one of the locked design goals. mp-units quantity
> arithmetic is already `constexpr`; just ensure the calculator body avoids non-`constexpr` calls (no
> logging, no allocation).

---

## 6. Property-based & round-trip tests

Known-value tests prove "matches the reference"; property tests prove "internally consistent" — they
catch bugs no single reference point exercises. Three families fit the EMC calculators directly.

### 6.1 Round-trip identity (the converters)

`Wavelength<->Frequency` and `Energy<->Frequency` are exact inverses, so λ→f→λ must return the input
within tolerance.

```c++
TEST_CASE("wavelength<->frequency is an involution", "[converter][property][roundtrip]") {
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

The round-trip confirms the converter pair is self-consistent under the exact SI value of `c`,
independent of any single known-value check.

### 6.2 Solve-forward / solve-back consistency (MicrostripTrace)

The microstrip solvers compute Z0 from (H,T,W), or W from (H,T,Z0). The property: solving forward then
back returns the original geometry.

```c++
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

Cheap, high-value sanity laws that hold for *any* input — generate in-range inputs and assert:

```c++
TEST_CASE("skin depth shrinks as frequency rises", "[basic][property][monotonic]") {
    auto sigma = 5.8e7 * (si::siemens / si::metre);
    auto d = [&](double f_MHz) {
        return emc::basic::calculate({.frequency = f_MHz*MHz, .conductivity = sigma})
                 .value().skin_depth.numerical_value_in(si::micro<si::metre>);
    };
    REQUIRE(d(1.0)  > d(10.0));     // delta proportional to 1/sqrt(f) -> strictly decreasing
    REQUIRE(d(10.0) > d(100.0));
    REQUIRE(d(1.0)  > 0.0);         // physically positive
}
```

Other useful invariants: `decibel(x)` is monotone in `x`; VSWR ≥ 1 always; shielding effectiveness ≥ 0 dB
for a real shield; resonant-mode frequencies are ordered (`f110 ≤ f111 ≤ …` for a rectangular
enclosure). These need *no* reference file — they encode physics.

---

## 7. CI integration, sanitizers, coverage, fuzzing

### 7.1 CTest wiring (CMake)

Tests live in `tests/` and register with CTest via Catch2's discovery (full setup in
**08-build-system-cmake.md**):

```cmake
# tests/CMakeLists.txt
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(emc_tests
    basic/skin_depth_known.test.cpp
    component/microstrip_property.test.cpp
    converter/wavelength_roundtrip.test.cpp
    constants_compiletime.test.cpp
    # ... one or more files per calculator
)
target_link_libraries(emc_tests PRIVATE emc::emc Catch2::Catch2WithMain)
target_compile_features(emc_tests PRIVATE cxx_std_23)

# Make the reference vectors available next to the test binary.
file(COPY ${CMAKE_SOURCE_DIR}/tests/vectors DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

catch_discover_tests(emc_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
```

### 7.2 Sanitizers (a dedicated CI build type)

```cmake
# Enable with -DEMC_SANITIZE=address,undefined on a CI job.
if(EMC_SANITIZE)
    target_compile_options(emc_tests PRIVATE -fsanitize=${EMC_SANITIZE} -fno-omit-frame-pointer -g)
    target_link_options(emc_tests    PRIVATE -fsanitize=${EMC_SANITIZE})
endif()
```

Run the suite under **ASan+UBSan** (catches any divide-by-zero in `1/(pi*f*…)` and integer/float
overflow) and, in a separate job, under **TSan** to prove the "thread-safe by construction / no global
mutable state" design goal — a regression that adds a `static` cache would trip TSan.

### 7.3 GitHub Actions matrix (sketch)

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

> [!WARNING]
> C++23 `constexpr <cmath>`, `std::expected`, and `std::print` support differ across GCC and Clang
> versions. The compile-time tests in Section 5 are the canary — if a toolchain can't `constexpr`-evaluate
> `calculate()`, that job fails fast. Pin minimums that ship the needed library features (and mp-units's
> requirements; see **03**).

### 7.4 Coverage targets

```cmake
if(EMC_COVERAGE)
    target_compile_options(emc PUBLIC --coverage -O0 -g)
    target_link_options(emc    PUBLIC --coverage)
endif()
```

Use `gcovr`/`llvm-cov` to report **line + branch** coverage:

| Layer                                | Target line coverage |
|--------------------------------------|----------------------|
| `emc::*` calculators (the math ones) | ≥ 95 %               |
| `emc::materials` / `emc::constants`  | 100 % (tiny, data)   |
| `emc::detail` helpers                | ≥ 90 %               |

Branch coverage matters most for the `validate()` functions — every rejected-range branch should have a
row in Section 3.2. Gate the PR if coverage drops below threshold.

### 7.5 Optional: fuzzing the parser & benchmarking

The CSV/`from_chars` parser is the only place that touches untrusted-ish text; a small libFuzzer target
hardens it (a natural ASan companion):

```c++
// tests/fuzz/fuzz_csv.cpp  — built only with -DEMC_FUZZ=ON, clang + -fsanitize=fuzzer,address
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string_view sv{reinterpret_cast<const char*>(data), size};
    try { (void)emc::test::split(sv); } catch (...) {}   // must never crash/UB
    return 0;
}
```

Optionally add micro-benchmarks (Catch2 `BENCHMARK`) on the hottest calculators to catch accidental
pessimization (e.g. reintroducing per-call allocation). Benchmarks are informational, not a CI gate.

---

## 8. Test directory layout

```text
emcpp/
├── tests/
│   ├── CMakeLists.txt
│   ├── support/
│   │   ├── csv.hpp                 # ranges/string_view parser (Section 2.1)
│   │   └── vector_runner.hpp       # generic engine + close() (Section 2.2, 4.1)
│   ├── vectors/                    # self-describing reference CSVs (Section 1)
│   │   ├── skin_depth.csv
│   │   ├── microstrip_trace.csv
│   │   ├── wavelength_freq.csv
│   │   └── … (one per calculator)
│   ├── basic/      *.test.cpp      # known-value + property + validation per calc
│   ├── component/  *.test.cpp
│   ├── converter/  *.test.cpp
│   ├── …                           # one subdir per category namespace
│   ├── constants_compiletime.test.cpp
│   └── fuzz/fuzz_csv.cpp           # optional
```

Each calculator yields at minimum **(1)** a known-value table test, plus where applicable **(2)** a
property/round-trip test and **(3)** an edge/validation test. That is the **definition of "done" for a
calculator's tests** referenced by **10-roadmap.md**.

---

## 9. Per-calculator test exit criteria

- [ ] Reference vectors in `tests/vectors/<calc>.csv` with explicit unit headers, expected values
      hand-computed or cited to a textbook.
- [ ] Known-value table test passes at the chosen tolerance (Section 4.2).
- [ ] Validation test covers every `validate()` rejection branch.
- [ ] Round-trip / property test where the calculator is bidirectional or has a known invariant.
- [ ] At least one `static_assert` compile-time check if `calculate()` is `constexpr`.
- [ ] Green under ASan+UBSan; ≥ 95 % line coverage for the calculator.

---

## Cross-references

- **02-modern-cpp-feature-catalog.md** — `std::ranges`/`views::split`, `std::string_view`,
  `std::from_chars`, `constexpr`, `static_assert` used throughout the harness.
- **03-quantities-and-units-mp-units.md** — `numerical_value_in(unit)` and fixed-unit comparison
  (Section 4.3).
- **04-constants-and-material-database.md** — the single `constexpr` constants + material DB asserted by
  the compile-time tests (Section 5) and `materials::by_name` lookups (Section 1.1).
- **05-error-handling-and-validation.md** — `emc::Error` / `ErrorCode` checked by the validation tests
  (Section 3.2).
- **06-calculator-design-pattern.md** — the Input/Result/`calculate`/`validate`/`Calculator`-concept
  contract the generic runner (Section 2.2) depends on.
- **07-calculator-inventory.md** — the work-list mapping each calculator to its reference vectors.
- **08-build-system-cmake.md** — full CMake/CTest/Catch2 wiring, presets, sanitizer/coverage options.
- **10-roadmap.md** — uses the per-calculator test checklist (Section 9) as a phase exit criterion.
