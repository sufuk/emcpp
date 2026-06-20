# 🧮 Physical Constants & Material Database (single source of truth)

> [!IMPORTANT]
> **Purpose:** define exactly **one** place for every physical constant and every material property in
> `emc`, expressed as `constexpr` mp-units quantities, so every calculator draws pi, _c_, μ₀, ε₀, or a
> material's conductivity from the *same* authoritative value.

Two compile-time databases prevent silent numerical drift between calculators that should agree to the
last digit:

1. **`emc::constants`** — `constexpr` header of exact SI/CODATA physical constants as mp-units quantities.
2. **`emc::materials`** — `constexpr` table of textbook material properties (σ, μ_r, ε_r, ρ), one row per
   material.

Both are header-declared so values work at compile time and in `static_assert`; any non-`constexpr`
helpers compile in `src/`.

---

## 1. ⚙️ `emc::constants`

### 1.1 Design rules

- **One header:** `include/emc/constants.hpp`. Everything `inline constexpr` — zero runtime cost, usable
  in `static_assert`.
- **pi from the standard library:** `std::numbers::pi_v<double>` — full precision, namespaced, no macro.
- **Physical constants are mp-units quantities**, not bare doubles, so the unit is part of the type and
  conversions are compile-checked.
- **Values are current SI / CODATA-2018.**
- **`static_assert` relations** prove the constants are mutually consistent at compile time.

> [!NOTE]
> **Provenance.** _c_, _h_, and elementary charge _e_ are **exact defining constants** of the SI (no
> uncertainty since the 2019 redefinition). μ₀ and ε₀ are **measured** post-2019: μ₀ ≈ 1.256 637 062 12×10⁻⁶
> H/m, no longer *exactly* 4π×10⁻⁷. Each literal is annotated with its category so a maintainer knows what
> may change on a future CODATA adjustment.

### 1.2 The shipped constants

| Constant | Symbol | Value | Category / note |
|---|---|---|---|
| pi | π | `std::numbers::pi_v<double>` | pure math, full precision |
| Speed of light | _c_ | 299 792 458 m/s | SI exact defining |
| Planck constant | _h_ | 6.626 070 15e-34 J·s | SI exact defining |
| Vacuum permeability | μ₀ | 1.256 637 062 12e-6 H/m | CODATA measured |
| Vacuum permittivity | ε₀ | 8.854 187 8128e-12 F/m | derived = 1/(μ₀·c²) |
| Impedance of free space | _Z₀_ | 376.730 313 668 Ω | = √(μ₀/ε₀) = μ₀·c |
| Elementary charge | _e_ | 1.602 176 634e-19 C (A·s) | SI exact; ESD/lightning |

Each value is an `inline constexpr quantity` (e.g. `speed_of_light = 299'792'458.0 * (m / s)`). `inline`
gives **one** shared definition across all TUs — without it a header `constexpr` gets internal linkage, so
its address differs per TU (an ODR foot-gun). This is the standard idiom for header constants.

### 1.3 Compile-time sanity relations

The defining relations of electromagnetism are encoded as build-time invariants. C++23 makes `std::sqrt`
usable in `constexpr` and mp-units quantities are `constexpr`-friendly, so these run during compilation —
edit a literal and break consistency, and **the build fails**:

- `c == 1 / sqrt(eps0 * mu0)`
- `Z0 == sqrt(mu0 / eps0)` and `Z0 == mu0 * c`
- `mu0` within 0.01% of `4*pi*1e-7` (documents the measured value)

Because the constants are mp-units quantities the checks are **dimension-aware** — you cannot accidentally
compare an impedance against a velocity. A self-contradictory constant set simply does not compile.

```c++
// include/emc/constants.hpp (appended)
static_assert(close(c_check, speed_of_light.numerical_value_in(si::metre / si::second)),
              "c must equal 1/sqrt(eps0*mu0)");
```

> [!NOTE]
> **C++26 forward-looking:** with **contracts** these could become pre/postconditions on constant-producing
> helpers, and **reflection** could enumerate the constants for an auto-generated units table. Today,
> `static_assert` is the right tool.

### 1.4 Why this shape

| Property | `emc::constants` |
|---|---|
| Correct SI/CODATA value | ✅ |
| Scoped / namespaced | ✅ `emc::constants::` |
| Type-checked dimension | ✅ mp-units quantity |
| Single definition (ODR-clean) | ✅ `inline constexpr` |
| Compile-time consistency | ✅ `static_assert` |
| Survives refactor | ✅ edit one place; asserts guard it |

---

## 2. 🗂️ `emc::materials`

Skin depth, resistance, and transmission-line calculators all draw from the same small set of metals.
Rather than let each calculator embed its own conductivity literal — which is how two calculators end up
disagreeing in the third digit for "the same" copper — the library publishes **one** canonical table.

### 2.1 The property values the library ships

Standardized on **conductivity σ (S/m) at 20 °C**, **relative permeability** μ_r, **relative permittivity**
ε_r (≈ 1 for these conductors), and a cached **resistivity ρ = 1/σ**. Values are standard
engineering-grade (IACS-consistent) reference figures; magnetic metals such as Nickel carry their
characteristic high μ_r.

| Material   | σ (S/m)   | ρ = 1/σ (Ω·m) | μ_r       | ε_r |
|---|---|---|---|---|
| Copper     | 5.96e7    | 1.678e-8 | 0.999991 | 1 |
| Silver     | 6.30e7    | 1.587e-8 | 0.99998  | 1 |
| Gold       | 4.10e7    | 2.439e-8 | 1.0      | 1 |
| Aluminum   | 3.77e7    | 2.653e-8 | 1.00002  | 1 |
| Nickel     | 1.43e7    | 6.993e-8 | 600      | 1 |
| Tungsten   | 1.79e7    | 5.587e-8 | 1.0      | 1 |
| Platinum   | 9.43e6    | 1.060e-7 | 1.0      | 1 |
| Lead       | 4.55e6    | 2.198e-7 | 1.0      | 1 |
| Graphite   | 1.00e5    | 1.000e-5 | 1.0      | 1 |

> [!NOTE]
> **Provenance.** Standard textbook/handbook values for bulk metals at 20 °C; each code row carries a
> source comment. Exactly **one row per material**, so revising a value is a one-line reviewable change —
> and the §4.2 invariants guarantee each row stays consistent (ρ = 1/σ, all values positive).

### 2.2 Structure

- One scoped `enum class Material` (no implicit `int` conversions, explicit `Custom = 0` slot, `Count`
  sentinel) — a stray UI index can never silently become a `Material`.
- One `MaterialProperties` struct holding **mp-units quantities** (σ, μ_r, ε_r, ρ) — units live in the
  TYPE (`isq::electrical_conductivity`, `isq::resistivity`, dimensionless `one`), not in comments.
- One `constexpr std::array table` — the *only* place values live, built by a `consteval make()` so a typo
  that breaks constant evaluation is a hard error, not a silent runtime cost. The whole table folds at
  compile time, lives in read-only data, and is trivially thread-safe.
- A `constexpr` `names` array for name↔enum, and `constexpr` lookups `properties()`, `from_name()`,
  `to_name()`.

`properties(Material)` returns `std::expected<MaterialProperties, Error>`: a typed error for `Custom` or an
out-of-range cast, never a magic sentinel (a sentinel `1.0` is a *plausible* resistivity, so a caller could
not tell "unknown" from "ρ = 1 Ω·m"). `[[nodiscard]]` makes ignoring it a warning.

```c++
[[nodiscard]] constexpr std::expected<MaterialProperties, Error>
properties(Material m) noexcept {
    if (m == Material::Custom)
        return std::unexpected(Error{ErrorCode::CustomMaterialHasNoTableEntry, "supply properties explicitly"});
    const auto idx = static_cast<std::size_t>(m);
    if (idx == 0 || idx >= static_cast<std::size_t>(Material::Count))
        return std::unexpected(Error{ErrorCode::UnknownMaterial, "Material enum out of range"});
    return table[idx - 1];   // -1 because Custom occupies slot 0
}
```

### 2.3 Custom materials

`Custom` deliberately has **no table row**: `properties(Material::Custom)` returns an error telling the
caller to provide values directly. A calculator can legitimately be called with an alloy or composite not
in the catalog, so callers bypass the database with fully type/unit-checked `MaterialProperties`. A
`MaterialSpec = std::variant<Material, MaterialProperties>` and a `resolve(spec)` helper handle "either a
catalog id or explicit props" in one place.

### 2.4 Name↔enum auto-generation (forward-looking)

The hand-written `names` array duplicates the enum. Two ways to remove the duplication:

- **Today (optional): `magic_enum`.** `enum_name` / `enum_cast` give name↔enum at compile time with no
  parallel table; `to_name`/`from_name`/`names` collapse to thin wrappers. (Limitation: enum range must fit
  its default span.)
- **C++26: static reflection.** `std::meta::enumerators_of` can *generate* the name table, a field-by-field
  printer, and a units report directly from the enum/struct. Until then, keep the `constexpr names` array
  and let the §4.2 `static_assert` keep it in sync.

---

## 3. 🔌 Usage in calculators

Calculators pull σ, μ_r, ρ from `emc::materials` and μ₀, pi from `emc::constants`, then let mp-units carry
the units — no literals, no local copies. Example: **skin depth** `delta = 1 / sqrt(pi * f * mu * sigma)`
with `mu = mu_r * mu_0`:

```c++
auto props = emc::materials::properties(emc::materials::Material::Copper);  // expected<...>
if (!props) return std::unexpected(props.error());
const auto sigma = props->conductivity;                                 // S/m, typed
const auto mu    = props->relative_permeability * emc::constants::mu_0;  // H/m, typed
const quantity delta = 1.0 / sqrt(emc::constants::pi * f * mu * sigma);  // metres, dimension-checked
```

**Conductor resistance** does the same with `props->resistivity` (Ω·m) and honours the material's actual
μ_r, getting a typed error on an unknown material.

> [!CAUTION]
> Because every conductor calculator calls the *one* `properties()`, "the same" copper resistivity is
> byte-identical everywhere. For a non-standard value (specific alloy, temperature ≠ 20 °C), use a `Custom`
> material via `resolve()` rather than editing the shared table.

---

## 4. 🛡️ Extensibility & validation

### 4.1 Adding a material in exactly one place

To add **Brass**: (1) add `Brass` to `enum class Material` before `Count`; (2) add one `detail::make(...)`
row to `table` at the same position; (3) add `{Material::Brass, "Brass"}` to `names`. Every calculator
taking a `Material` sees it immediately. With reflection or `magic_enum`, step 3 disappears.

### 4.2 Compile-time validation invariants

`static_assert` / `consteval` enforce the table's internal consistency at build time, so the database
cannot merge in a broken state:

- **(a)** `table.size()` == number of non-`Custom` materials (adding an enumerator without a row → build error).
- **(b)** `names.size()` == `Custom` + every real material.
- **(c)** every value strictly positive and self-consistent (`rho * sigma ≈ 1`).
- **(d)** lookup ordering spot-checked: `properties(Material::Gold)` has a value, `to_name(Aluminum) ==
  "Aluminum"`, `from_name("Aluminum") == Aluminum`, `from_name("Aluminium")` is `nullopt` (only the
  canonical spelling).

```c++
static_assert(table.size() == static_cast<std::size_t>(Material::Count) - 1,
              "table size must equal number of non-Custom materials");
static_assert(all_values_sane(), "every material must have positive, self-consistent properties");
```

### 4.3 Keeping the table indexed by enum

The contract is **`table` row order mirrors `enum class Material`** (Custom excluded), so the O(1)
`properties()` index is correct. Invariant (a) catches a size mismatch; ordering is documented at the
`table` definition and spot-checked by (d). If reflection is adopted, the table can be built *from* the
enum so ordering cannot drift.

---

## 5. 🧪 Testing

- **Compile-time invariants (free):** the §1.3 and §4.2 `static_assert` blocks are themselves tests — green
  build means they pass.
- **Known-value constants:** `speed_of_light == 299'792'458 m/s` and `elementary_charge ==
  1.602'176'634e-19 C` exactly (defined SI values); `pi` to 8 digits.
- **Physical identities as round-trips:** check _Z₀_ = μ₀·_c_ and _c_ = 1/√(μ₀·ε₀) in Catch2 with rel
  tolerance ~1e-6, exercising the public quantity API.
- **Database known values:** a `GENERATE` table of `{Material → expected σ}` pairs from §2.1; assert
  `properties(m)->conductivity` matches and `resistivity * conductivity ≈ 1`.
- **Edge cases:** `properties(Custom)` → `CustomMaterialHasNoTableEntry`; out-of-range cast →
  `UnknownMaterial`; `from_name("Aluminium")` → `nullopt`.

---

## 🔗 Cross-references

See `03-quantities-and-units-mp-units.md` (mp-units types, `numerical_value_in`),
`05-error-handling-and-validation.md` (`Error` / `std::expected`), `06-calculator-design-pattern.md`
(Input/Result/`calculate` consumers), and `09-testing-and-golden-vectors.md` (testing strategy).
