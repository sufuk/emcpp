# Testing Strategy 🧪

A fast, deterministic, GUI-free regression suite for the pure `emc::*::calculate()` functions, proving
every formula correct against established EMC references. Four pillars:

1. **Known-value** — hand-computed / textbook closed-form results.
2. **Property** — round-trip, monotonicity, physical-sign invariants.
3. **Compile-time** — `static_assert` over `constexpr` math; a wrong constant fails the build.
4. **Edge / validation** — out-of-domain inputs rejected with a specific `ErrorCode`.

All four run under Catch2 v3 with explicit tolerances and a CI matrix.

> [!NOTE]
> The calculator contract (`Input`/`Result`, `calculate`/`validate`, `Calculator` concept) is defined in
> **06**; the error type in **05**; units in **03**. This document consumes all three.

---

## 1. Reference vectors: self-describing, units explicit

Vectors live in `tests/vectors/<calc>.csv` — each a self-describing file whose header names the unit of
every column. Expected outputs are hand-computed from the closed form or taken from a textbook worked
example.

```text
# vectors/skin_depth.csv — expected from delta = 1/sqrt(pi*f*mu*sigma)
frequency[MHz],material,expected_skin_depth[cm]
27,Nickel,0.000559
6.2,Aluminum,0.003391
```

Encoding each column's unit in the header removes tribal knowledge: the test reads `frequency[MHz]` and
constructs `27.0 * MHz` directly. A `material` column resolves through `emc::materials::by_name(...)`
(the single source of truth for `conductivity` and `relative_permeability`, from **04**) into the
`Input` struct; mp-units then lets the test assert in *exactly* the unit the reference uses (Section 4).

> [!IMPORTANT]
> Expected outputs are reference values, never captured from a running implementation. Each is a hand
> computation or a documented textbook example, so a passing test means the formula matches the
> established EMC engineering result.

---

## 2. A data-driven harness (GUI-free)

Pure standard C++: parse CSV with `std::views::split` over `std::string_view`, convert fields with
`std::from_chars`, build the `Input` with mp-units, call `calculate()`, compare within tolerance.

- **Zero-copy parsing.** `views::split` yields allocation-free fields; a `Row` owns its line string and
  the `string_view` fields point into it (valid only while the `Row` is alive).
- **Locale-independent.** `from_chars` always reads `.` as the decimal point regardless of host locale,
  eliminating `,` vs `.` bugs.
- **Skip rule.** `read_fixture()` skips blank lines, `#` comments, and the unit-header row (any line
  containing `[`).

The `Calculator` concept (from **06**) lets one template engine drive *every* calculator. Per calculator
you supply only two lambdas — `make_input(row)` and `check(row, result)`:

```c++
// tests/support/vector_runner.hpp
template <class MakeInput, class Check>
void run_vectors(const std::filesystem::path& fixture, MakeInput make_input, Check check) {
    for (const auto& row : read_fixture(fixture)) {
        const auto out = emc::calculate(make_input(row));   // ADL picks the right calculate()
        REQUIRE(out.has_value());
        check(row, *out);
    }
}
```

The `close()` helper compares two quantities in a *fixed unit* via `numerical_value_in(unit)` (Section
4). The engine is framework-agnostic — only the assertion macros change if the framework does.

---

## 3. Framework choice: Catch2 v3

| Need                     | Catch2 v3                                      | GoogleTest                          |
|--------------------------|------------------------------------------------|-------------------------------------|
| Data-driven rows         | `GENERATE(from_range(...))` inline             | `TEST_P` + `INSTANTIATE_…` (verbose)|
| FP comparisons           | `WithinRel` / `WithinAbs` / `WithinULP`        | `EXPECT_NEAR` only (no ULP/rel)     |
| CMake integration        | `find_package(Catch2 3)` + `catch_discover_tests` | `gtest_discover_tests`           |
| Round-trip setup sharing | `SECTION`s share setup naturally               | fixtures, more boilerplate          |

`WithinRel`/`WithinAbs`/`WithinULP` map directly onto Section 4's tolerance model, and
`GENERATE(from_range(...))` feeds parsed rows as individually-reported test cases so a single bad row is
pinpointed (`CAPTURE` prints the offending inputs) instead of failing the whole file.

### 3.1 Known-value table test (the shape)

```c++
TEST_CASE("skin depth matches reference values", "[basic][skin_depth][known]") {
    auto row = GENERATE_REF(from_range(load_rows("vectors/skin_depth.csv")));
    CAPTURE(row.freq_MHz, row.material);

    const auto mat = emc::materials::by_name(row.material);
    REQUIRE(mat.has_value());

    const auto res = emc::basic::calculate({
        .frequency = row.freq_MHz * MHz,
        .conductivity = mat->conductivity,
        .relative_permeability = mat->relative_permeability });
    REQUIRE(res.has_value());

    // Compare in the SAME unit the reference used (cm); mp-units guarantees the conversion.
    REQUIRE_THAT(res->skin_depth.numerical_value_in(cm), WithinRel(row.expected_cm, 1e-6));
}
```

### 3.2 Edge / validation tests

Known-value rows test the happy path; the `validate()` half of the contract (from **05**) gets its own
table of values that must be **rejected** with a specific `ErrorCode`:

```c++
auto c = GENERATE(values<Case>({
    { 0.0 * Hz,  ErrorCode::OutOfRange },   // f must be > 0; 1/(pi*f*…) undefined at 0
    { -1.0 * Hz, ErrorCode::OutOfRange },
}));
const auto res = emc::basic::calculate({ .frequency = c.f, .conductivity = sigma });
REQUIRE_FALSE(res.has_value());
REQUIRE(res.error().code == c.code);
```

Every rejection branch (e.g. permittivity range, W:H ratio) gets a row.

---

## 4. Floating-point comparison

### 4.1 The model

Never compare floats with `==`. Use **relative tolerance with an absolute floor** so values near zero
don't blow up the relative term:

```text
pass  <=>  |got - want| <= abs_tol                          (handles values near 0)
      OR   |got - want| <= rel_tol * max(|got|, |want|)
```

This is the `close()` helper and Catch2's `WithinAbs || WithinRel`. For results that must be *bit-stable*
across platforms (pure constants, simple closed forms) use `WithinULP(want, maxUlpDiff)`.

### 4.2 Choosing the tolerance

| Situation                                            | Tolerance                  |
|------------------------------------------------------|----------------------------|
| Full-precision analytic reference (we compute it)    | `WithinRel(1e-12)`         |
| Textbook value quoted to ~6 sig figs                 | `WithinRel(1e-6)`          |
| Multi-step `exp`/`log`/`pow` (Shielding, ESD)        | `WithinRel(1e-9)`          |
| Sum/cancellation-prone (resonant-mode diffs)         | add `WithinAbs(1e-9)` floor|
| Bit-stable constants / closed form                   | `WithinULP(2..4)`          |

A textbook value at six sig figs cannot support a `1e-12` demand without false failures; tight tolerances
are reserved for references we compute ourselves at full `double` precision.

### 4.3 mp-units makes FP comparison safer

Compare quantities in a **fixed unit, with the conversion compile-checked**:

```c++
REQUIRE_THAT(res->skin_depth.numerical_value_in(cm), WithinRel(expected_cm, 1e-6));
```

`numerical_value_in(cm)` **won't compile** if `skin_depth` isn't a length, so a whole class of
unit-mismatch test bugs (metres vs cm) is impossible, and there's no temptation to bake unit factors into
the tolerance.

> [!IMPORTANT]
> A vector file says `w[mm]`, the calculator returns a `length`, the test extracts `in(mm)`. The unit is
> declared once in the header and enforced by the type system end to end — a comparison can never be
> silently off by a unit factor.

---

## 5. Compile-time tests (`constexpr` + `static_assert`)

C++23 makes `<cmath>` `constexpr` (`sqrt`, `exp`, `log`, `pow`), so the simpler closed-form calculators
evaluate **at compile time**. A wrong constant then fails the *build*, not a test run — the strongest
regression guard. Constants are defined exactly once (**04**); asserting them at compile time breaks the
build everywhere at once on any regression.

```c++
static_assert(emc::constants::c.numerical_value_in(m/s) == 299'792'458.0,
              "speed of light is the exact SI defining constant");

// Skin depth of copper at 1 MHz ~ 66.1 um (analytic). calculate() is constexpr (see 06).
constexpr auto copper = emc::basic::calculate({
    .frequency = 1.0 * si::mega<si::hertz>,
    .conductivity = 5.8005e7 * (si::siemens / si::metre),
    .relative_permeability = 0.999991 * one }).value().skin_depth;

static_assert(copper.numerical_value_in(si::micro<si::metre>) > 66.0 &&
              copper.numerical_value_in(si::micro<si::metre>) < 66.3, "copper @1 MHz ~66 um");
```

> [!NOTE]
> C++23 `constexpr <cmath>` + `constexpr std::expected` let the *exact same* `calculate()` run at compile
> time and runtime — there is no separate "test formula". The compile-time path also proves the function
> is genuinely pure (no global state, no I/O), a locked design goal. Just keep the calculator body free of
> non-`constexpr` calls (no logging, no allocation).

---

## 6. Property-based & round-trip tests

Known-value tests prove "matches the reference"; property tests prove "internally consistent", catching
bugs no single reference point exercises. Three families fit the EMC calculators:

| Family               | Property                                              | Example                                   |
|----------------------|------------------------------------------------------|-------------------------------------------|
| **Round-trip**       | exact inverses recover the input (tol `1e-12`)        | `Wavelength<->Frequency`, `Energy<->Frequency` |
| **Solve fwd/back**   | forward then back returns original geometry (tol `1e-4`) | MicrostripTrace `Z0(W)` then `W(Z0)`   |
| **Monotone / sign**  | physical laws holding for *any* in-range input        | δ shrinks as f rises; δ > 0               |

```c++
TEST_CASE("skin depth shrinks as frequency rises", "[basic][property][monotonic]") {
    auto d = [&](double f) { return emc::basic::calculate(
        {.frequency = f*MHz, .conductivity = sigma}).value()
        .skin_depth.numerical_value_in(si::micro<si::metre>); };
    REQUIRE(d(1.0) > d(10.0));   // delta proportional to 1/sqrt(f) -> strictly decreasing
    REQUIRE(d(1.0) > 0.0);       // physically positive
}
```

Other invariants encode physics with *no* reference file: VSWR ≥ 1, shielding effectiveness ≥ 0 dB for a
real shield, ordered resonant-mode frequencies (`f110 ≤ f111 ≤ …`).

---

## 7. CI integration, sanitizers, coverage, fuzzing

### 7.1 CTest wiring

Tests live in `tests/` and register with CTest via Catch2 discovery (full setup in **08**):

```cmake
find_package(Catch2 3 REQUIRED)
include(Catch)
target_link_libraries(emc_tests PRIVATE emc::emc Catch2::Catch2WithMain)
target_compile_features(emc_tests PRIVATE cxx_std_23)
file(COPY ${CMAKE_SOURCE_DIR}/tests/vectors DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
catch_discover_tests(emc_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
```

### 7.2 Sanitizers

Run the suite under **ASan+UBSan** (catches divide-by-zero in `1/(pi*f*…)` and float overflow), and in a
separate job under **TSan** to prove the "no global mutable state" goal — a regression adding a `static`
cache would trip TSan. Gated behind `-DEMC_SANITIZE=address,undefined` on a CI job.

### 7.3 GitHub Actions matrix

```yaml
strategy:
  matrix:
    compiler: [gcc-14, clang-19]          # both need full C++23 + constexpr <cmath>
    sanitize: ["", "address,undefined"]
```

> [!WARNING]
> C++23 `constexpr <cmath>`, `std::expected`, and `std::print` support differ across GCC and Clang. The
> Section 5 compile-time tests are the canary — if a toolchain can't `constexpr`-evaluate `calculate()`,
> that job fails fast. Pin minimums that ship the needed library features (and mp-units's; see **03**).

### 7.4 Coverage

Report **line + branch** coverage with `gcovr`/`llvm-cov`; gate the PR below threshold.

| Layer                                | Target line coverage |
|--------------------------------------|----------------------|
| `emc::*` calculators (the math)      | ≥ 95 %               |
| `emc::materials` / `emc::constants`  | 100 % (tiny, data)   |
| `emc::detail` helpers                | ≥ 90 %               |

Branch coverage matters most for `validate()` — every rejected-range branch needs a row in Section 3.2.

### 7.5 Optional: fuzzing & benchmarking

The CSV/`from_chars` parser is the only place touching untrusted-ish text; a small libFuzzer target
(`-DEMC_FUZZ=ON`, clang `-fsanitize=fuzzer,address`) hardens it. Optional Catch2 `BENCHMARK` micro-bench
on the hottest calculators catches accidental pessimization (e.g. reintroduced per-call allocation) —
informational, not a CI gate.

---

## 8. Test directory layout

```text
tests/
├── CMakeLists.txt
├── support/
│   ├── csv.hpp                 # ranges/string_view parser (Section 2)
│   └── vector_runner.hpp       # generic engine + close() (Section 2, 4)
├── vectors/                    # self-describing reference CSVs (Section 1)
│   ├── skin_depth.csv
│   └── … (one per calculator)
├── basic/      *.test.cpp      # known-value + property + validation per calc
├── component/  *.test.cpp
├── converter/  *.test.cpp      # one subdir per category namespace
├── constants_compiletime.test.cpp
└── fuzz/fuzz_csv.cpp           # optional
```

---

## 9. Per-calculator test exit criteria

The "done" definition referenced by **10-roadmap.md**:

- [ ] Reference vectors in `tests/vectors/<calc>.csv` with explicit unit headers, hand-computed or cited.
- [ ] Known-value table test passes at the chosen tolerance (Section 4.2).
- [ ] Validation test covers every `validate()` rejection branch.
- [ ] Round-trip / property test where the calculator is bidirectional or has a known invariant.
- [ ] At least one `static_assert` compile-time check if `calculate()` is `constexpr`.
- [ ] Green under ASan+UBSan; ≥ 95 % line coverage.

---

## Cross-references

**03** unit-fixed comparison · **04** constants/material DB · **05** `Error`/`ErrorCode` · **06**
Input/Result/`calculate`/`Calculator` contract · **07** calculator→vector work-list · **08** CMake/CTest
wiring · **10** roadmap exit criteria.
