# Architecture

Every calculator in emcpp follows the same shape, so once you know one you know them all.

```cpp
namespace emc::basic {                       // category namespace mirrors the include path

  struct SkinDepthInput  { /* fields, designated-initializer friendly, with defaults */ };
  struct SkinDepthResult { /* computed quantities */ };

  // Free functions — no objects to construct, no virtual calls.
  std::expected<void, emc::Error> validate(const SkinDepthInput&);
  emc::Result<SkinDepthResult>    calculate(const SkinDepthInput&);   // validates, then computes

  // A zero-data tag binds the (Input, Result, calculate) triple to a concept.
  struct SkinDepth { using Input = SkinDepthInput; using Result = SkinDepthResult; /* ... */ };
  static_assert(emc::ValidatedCalculator<SkinDepth>);                 // contract proven at compile time
}
```

## The building blocks

### Typed quantities (`emc::units`)

All inputs and outputs are [mp-units](https://github.com/mpusz/mp-units) quantities exposed through curated aliases (`Frequency` in Hz, `Length` in m, `Impedance` in Ω, `Conductivity` in S/m, …). The unit lives in the **type**, so wrong-unit math does not compile:

```cpp
emc::units::Frequency f = 1.0 * MHz;     // ok
emc::units::Length    L = 1.5 * mm;      // ok
// emc::units::Frequency bad = 1.5 * mm; // compile error
```

Dimensionless empirical ratios — relative permeability `μ_r` and permittivity `ε_r` — are deliberately **plain `double`**, because they appear unqualified in the empirical formulas.

### Logarithmic types are distinct

`emc::units::Decibel` and `emc::units::Dbm` are typed wrappers, **never** linear units, so a dB value can never be added to a power by accident. Crossing the linear↔log boundary is always an explicit `to_power` / `to_dbm` / `to_ratio` / `to_decibel` call.

### Errors are values

```cpp
using Result<T> = std::expected<T, emc::Error>;
```

Every `calculate()` / `solve_*()` / `properties()` returns `emc::Result<T>`. No exceptions. An `emc::Error` carries:

- a structured **`ErrorCode`** (`OutOfRange`, `InvalidInput`, `DomainError`, `DivisionByZero`, `UnknownMaterial`, `NotConverged`, `Unsupported`),
- a human **message**, the offending **field** name, an optional valid **range**, and a captured **`std::source_location`**.

```cpp
if (const auto r = emc::basic::calculate(in)) {
    use(r->skin_depth);
} else {
    log(r.error().code, r.error().what());
}
```

### Compile-time contracts (concepts)

Each `(Input, Result, calculate, validate)` quadruple is wrapped in a zero-data tag and `static_assert`-ed against the `Calculator` / `ValidatedCalculator` concept. Generic code can treat a calculator as one named thing — **no virtual dispatch, zero runtime cost** — and a signature drift becomes a compile error.

### Materials & constants

- **`emc::materials`** — a `constexpr` table of built-in conductors (copper, silver, gold, aluminium, nickel, …) with conductivity, `μ_r`, `ε_r`; looked up via `properties(Material) -> Result<MaterialProperties>`. Calculators with a material input also accept `Material::Custom` + explicit `σ`/`μ_r`.
- **`emc::constants`** — exact 2019-SI EM constants (`c`, `μ₀`, `ε₀`, `Z₀`, elementary charge, `π`) whose physical identities (e.g. `c = 1/√(ε₀μ₀)`) are checked at **compile time** via `static_assert`.

## Bidirectional calculators

Some calculators are invertible. The controlled-impedance traces (microstrip / stripline / dual-stripline) add inverse `solve_height` / `solve_width` / `solve_thickness` (and `solve_gap`) functions next to the forward `calculate`.

See the full per-type detail in the [API reference](api/index.html).
