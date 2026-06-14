# Calculator Inventory & Library Mapping (the master work-list)

> Purpose: the authoritative, source-verified catalog of every calculator in the NinjaEMC app, mapped to its
> target location in the `emc` library. This document doubles as the migration checklist used by `10-migration-roadmap.md`.

This inventory was built by enumerating the source tree at `/Users/sufuk/CLionProjects/emc-prediction/src`
and reading each leaf calculator's `*.cpp` (formula comments and the button-clicked lambdas), then
cross-checking against the 51 golden fixtures in `resources/data/*.csv`. Every row below was confirmed
against the actual code; nothing is invented.

Conventions used in the **Proposed library location** column follow the locked design decisions and the
canonical conventions (see `01-architecture-and-layout.md` and `06-calculator-design-pattern.md`):

- Each calculator becomes a **free function** `calculate(const Input&) -> std::expected<Result, emc::Error>`
  inside a category namespace, with an aggregate `Input` struct, a `Result` struct, and (where the old code
  validated ranges) a `validate(const Input&) -> std::expected<void, emc::Error>`.
- Header path mirrors the namespace: `include/emc/<category>/<snake_name>.hpp`; implementation in
  `src/<category>/<snake_name>.cpp`.
- All physical quantities are `mp-units` `quantity<>` types (see `03-quantities-and-units-mp-units.md`),
  never bare `double`. The "Inputs/Outputs (with units)" columns name the **physical** quantity; the SI unit
  shown is the canonical storage unit the library will use.

---

## How to read the columns

| Column | Meaning |
|---|---|
| **Calculator** | Friendly name (matches the app screen). |
| **Source file** | Path relative to `emc-prediction/src/`. |
| **Inputs (with units)** | Physical inputs and their natural quantity kind. |
| **Output(s) (with units)** | Computed results. |
| **Core formula** | Brief formula taken from the code/`///` LaTeX comment. |
| **Solve directions** | `forward only` or `solves for X,Y,Z` (bidirectional / multi-target). |
| **Validation ranges** | Range checks present in the old code (mostly inline `QMessageBox`). |
| **Mat?** | Material-dependent (uses a conductivity/permeability/resistivity table). |
| **CSV?** | Golden CSV fixture present in `resources/data/`. |
| **Cx** | Complexity to port: **S** = single formula; **M** = a few branches / multi-output; **L** = bidirectional or many branches/modes. |
| **Proposed library location** | namespace + header path + function name. |
| **Status** | Migration tracking: `TODO` / `In-progress` / `Done` (start all at TODO). |

---

## SUMMARY TABLE

| Category | Math-bearing leaf calculators | Navigation-only widgets |
|---|---:|---:|
| BasicCalculations | 5 | 3 (`BasicCalculationsWidget`, `AntennaCalculatorWidget`, plus `MainWindow`) |
| Converter | 5 | 1 (`ConverterWidget`) |
| ComponentCalculations | 25 | 6 (`ComponentsCalculationsWidget`, `Capacitance/…Widget`, `Inductance/InductanceWidget`, `Resistance/ResistanceWidget`, `CircuitBoardTraImp/…Widget`, `TransmissionLineParameters/…Widget`) |
| EMCPredictions | 4 | 3 (`EMCPredictionsWidget`, `ESDand…CalculatorWidget`, `RFFieldCalculatorWidget`) |
| Shielding | 7 | 3 (`ShieldingWidget`, `CavityResonanceCalculatorWidget`, `EMShieldingEffectivenessCalculatorWidget`) |
| Filtering | 1 | 2 (`FilteringWidget`, `FerriteCalculatorWidget`) |
| Cabling | 2 | 1 (`CablingWidget`) |
| Grounding | 1 | 1 (`GroundingWidget`) |
| Testing | 1 | 1 (`TestingWidget`) |
| **TOTAL** | **51** | **21** |

**Totals:** 51 math-bearing leaf calculators; 21 navigation-only widgets (pure `QStackedWidget` index
switchers, no domain logic — **not ported**, they become consumer-side UI when the app is rewired). 51 golden
CSV fixtures exist; coverage notes are in the per-calculator tables and `09-testing-and-golden-vectors.md`.

> **Why these are free functions, not classes:** every old "calculator" is a lambda welded to a widget
> constructor, reading `ui->spinbox->value()` and writing `ui->result->setValue()`. There is no state to keep
> between calls — they are pure functions of their inputs. Modeling them as `calculate(Input) -> expected<Result>`
> removes ~70 widget classes' worth of incidental object lifetime, makes them trivially thread-safe, and lets
> the golden CSVs drive them directly with no Qt event loop.

---

## 1. BasicCalculations → `emc::basic`

The `AntennaCalculatorWidget` is a 3-button navigation hub (Dipole / Loop / FarField). Its three children are
the real calculators. `BasicCalculationsWidget` is navigation-only.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validation ranges | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Skin Depth | `BasicCalculations/SkinDepth/SkinDepthWidget.cpp` | frequency [Hz]; conductivity σ [S/m]; relative permeability μ_r [-] (from material) | skin depth δ [m] | δ = √(1 / (π·f·μ₀μ_r·σ)) | forward only | none | **yes** (Copper/Aluminum/Gold/Silver/Nickel: σ + μ_r inline) | `SkinDepthWidget.csv` (`freq,material`) | S | `emc::basic::skin_depth` in `include/emc/basic/skin_depth.hpp` | TODO |
| Decibel Calculator | `BasicCalculations/DecibelCalculator/DecibelCalculatorWidget.cpp` | one of: dB; or dBm + load R [Ω]; or power [W]; or voltage [V]; or Vp sinusoid [V] | voltage gain [-]; power gain [-]; power [W]; voltage(DC) [V]; Vp of sinusoid [V] | dB = 20·log₁₀(V₁/V₂) = 10·log₁₀(P₁/P₂); dBm = 10·log₁₀(P/0.001); V = √(P·R) | **bidirectional** (any field drives the rest; live-update) | none | no | `DecibelCalculatorWidget.csv` (`dB,dBm,load`) | M | `emc::basic::decibel` in `include/emc/basic/decibel.hpp` | TODO |
| Dipole Antenna | `BasicCalculations/AntennaCalculator/DipoleAntenna/DipoleAntennaWidget.cpp` | current I₀ [A]; length l [m]; distance R [m]; frequency [Hz]; angle θ [deg] | E_r [V/m]; E_θ [V/m]; H_φ [A/m] | E_r=60·(I₀l/R²)·cosθ·√(1+(c/2πfR)²); E_θ, H_φ per `///` | forward only (multi-output) | none | no | `DipoleAntennaWidget.csv` (5 cols) | M | `emc::basic::dipole_antenna` in `include/emc/basic/dipole_antenna.hpp` | TODO |
| Loop Antenna | `BasicCalculations/AntennaCalculator/LoopAntenna/LoopAntennaWidget.cpp` | current I₀ [A]; loop area A [m²]; distance R [m]; frequency [Hz]; angle θ [deg] | H_r [A/m]; H_θ [A/m]; E_φ [V/m] | H_r=(f/c)(I₀A/R²)cosθ√(1+(c/2πfR)²); E_φ=120(πf/c)²(I₀A/R)sinθ√(…) | forward only (multi-output) | none | no | `LoopAntennaWidget.csv` (5 cols) | M | `emc::basic::loop_antenna` in `include/emc/basic/loop_antenna.hpp` | TODO |
| Far-Field Criteria | `BasicCalculations/AntennaCalculator/FarFieldCriteria/FarFieldCriteriaWidget.cpp` | frequency [Hz]; max dimension D [m] | wavelength λ [m]; reactive near-field [m]; radiating near-field [m] | λ=c/f; if D>λ/10: reactive=0.62√(D³/λ), radiating=2D²/λ; else reactive=λ/50, radiating=λ | forward only (branch on D vs λ/10) | none | no | `FarFieldCriteriaWidget.csv` (`freq,length`) | M | `emc::basic::far_field_criteria` in `include/emc/basic/far_field_criteria.hpp` | TODO |

> **Note (orphan fixture):** `resources/data/MicroStripAntennaWidget.csv` exists but has **no source widget**
> (the `MicroStripAntenna*` symbol appears nowhere in `src/`). It is a dead fixture (rows `1,1,1` / `2,2,2` /
> `3,3,3` look like placeholders). Do **not** create a calculator for it — flag it for deletion (see Bugs list).

---

## 2. Converter → `emc::converter`

`ConverterWidget` is a 5-button navigation hub. Note: the converter source files do **not** end in `Widget.cpp`
(class names are `AntennaFactorvsAntennaGain`, `EnergyVsFrequency`, `WavelengthvsFrequency`, `VSWR_RC_RL_ML_TL`),
which is why a naïve `*Widget.cpp` glob misses them — they are real calculators.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validation ranges | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Antenna Factor ↔ Gain | `Converter/AntennaFactorvsAntennaGain/AntennaFactorvsAntennaGain.cpp` | frequency [Hz]; antenna factor AF [dB/m] | gain [dBi] | λ=c/f; gain=10·log₁₀((9.73/(λ·10^(AF/20)))²) | forward only (despite name; only AF→gain implemented) | none | no | `AntennaFactorvsAntennaGain.csv` (`freq,AF`) | S | `emc::converter::antenna_factor_to_gain` in `include/emc/converter/antenna_factor.hpp` | TODO |
| E-Field ↔ Power Density | `Converter/EFieldvsPowerDensity/EFieldvsPowerDensityWidget.cpp` | electric field E [V/m]; wave impedance η [Ω] (default 377) | power density P_D [W/m²] | P_D = E²/η | forward only | none | no | `EFieldvsPowerDensityWidget.csv` (`E,η`) | S | `emc::converter::efield_to_power_density` in `include/emc/converter/efield_power_density.hpp` | TODO |
| Energy ↔ Frequency | `Converter/EnergyvsFrequency/EnergyVsFrequency.cpp` | energy [eV] **or** frequency [Hz] | frequency [Hz] **or** energy [eV] | E=h·f (with eV↔J factor 6.242e18) | **bidirectional** (live-update both ways) | none | no | `EnergyVsFrequency.csv` (`energy`→freq) | M | `emc::converter::energy_frequency` in `include/emc/converter/energy_frequency.hpp` | TODO |
| Wavelength ↔ Frequency | `Converter/WavelengthvsFrequency/WavelengthvsFrequency.cpp` | wavelength λ [m] **or** frequency [Hz] | frequency [Hz] **or** wavelength λ [m] | λ=c/f and f=c/λ | **bidirectional** (live-update both ways) | none | no | `WavelengthvsFrequency.csv` (`λ`→freq) | M | `emc::converter::wavelength_frequency` in `include/emc/converter/wavelength_frequency.hpp` | TODO |
| VSWR / RC / RL / ML / IL | `Converter/VSWR_RC_RL_ML_TL/VSWR_RC_RL_ML_TL.cpp` | VSWR [-] | reflection coeff Γ [-]; return loss [dB]; mismatch loss [dB]; insertion loss [dB] | Γ=(VSWR-1)/(VSWR+1); RL=-20·log₁₀|Γ|; ML=-10·log₁₀(1-Γ²); IL=-10·log₁₀(|1+Γ|²) | forward only (multi-output) | none | no | `VSWR_RC_RL_ML_TL.csv` (`VSWR`) | M | `emc::converter::vswr` in `include/emc/converter/vswr.hpp` | TODO |

> **Bug found:** `VSWR_RC_RL_ML_TL.cpp` documents return loss as `RL=-10·log(|Γ|²)` in the `/** */` block but
> the code computes `-20·log10(Γ)`. These are algebraically equal for Γ>0, but the comment/code disagree and the
> code path breaks for VSWR=1 (Γ=0 → log of 0 → −∞). Add `validate` (VSWR ≥ 1) and re-bless the golden vector.

---

## 3. ComponentCalculations → `emc::component`

This is the largest category. Five navigation hubs (`ComponentsCalculationsWidget`, `Capacitance/…`,
`Inductance/InductanceWidget`, `Resistance/ResistanceWidget`, `CircuitBoardTraImp/…`,
`TransmissionLineParameters/…`) route to 25 leaf calculators.

### 3a. Capacitance → `emc::component`

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Parallel Plate | `…/Capacitance/ParallelPlate/ParallelPlateWidget.cpp` | area A [m²]; distance d [m] | capacitance C [F] | C = 8.85·A/d (pF, ε₀ folded into 8.85) | forward only | none | no | `ParallelPlateWidget.csv` (`A,d`) | S | `emc::component::parallel_plate_capacitance` in `include/emc/component/capacitance.hpp` | TODO |
| Sphere | `…/Capacitance/Sphere/SphereWidget.cpp` | radius r [m] | capacitance C [F] | C = 111·r (pF) | forward only | none | no | `SphereWidget.csv` (`r`) | S | `emc::component::sphere_capacitance` in `include/emc/component/capacitance.hpp` | TODO |

> **Bug found (units):** both capacitance widgets compute in pF then do `setValue((C / unit) * 1e-12)`, double-applying
> the pF factor; the displayed value is correct only because `8.85`/`111` already bake in 1e-12. With `mp-units`
> the constants become `ε₀` and the geometric formula stays dimensionally exact, eliminating the fudge factors.

### 3b. Inductance → `emc::component`

All seven re-define free-space permeability locally via `#define PermofFreeSpace ((4*M_PI)/1e7)` (Toroid uses an
inline `4*M_PI*1e-7`). All collapse to one `emc::constants::mu0`.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Circular Loop | `…/Inductance/CircularLoop/CircularLoopWidget.cpp` | turns N; radius R [m]; wire radius a [m]; μ_r | inductance L [H] | L=N²Rμ₀μ_r·(ln(8R/a)−2) | forward only | none | no | `CircularLoopWidget.csv` | S | `emc::component::circular_loop_inductance` in `include/emc/component/inductance.hpp` | TODO |
| Connector Pin | `…/Inductance/ConnectorPin/ConnectorPinWidget.cpp` | length l [m]; radius r [m]; spacing s [m] | self-L [H]; mutual M_p [H] | L=(μ₀l/2π)(ln(2l/r)−¾); M=(μ₀l/2π)(ln(2l/s)−1) | forward only (2 outputs) | none | no | `ConnectorPinWidget.csv` | S | `emc::component::connector_pin_inductance` in `include/emc/component/inductance.hpp` | TODO |
| Rectangular Loop | `…/Inductance/RectangularLoop/RectangularLoopWidget.cpp` | turns N; width w [m]; height h [m]; wire radius a [m]; μ_r | inductance L [H] | L=N²(μ₀μ_r/π)·[−2(w+h)+2√(w²+h²)−h·ln(…)−w·ln(…)+h·ln(2h/a)+w·ln(2w/a)] | forward only | none | no | `RectangularLoopWidget.csv` | M | `emc::component::rectangular_loop_inductance` in `include/emc/component/inductance.hpp` | TODO |
| Solenoid | `…/Inductance/Solenoid/SolenoidWidget.cpp` | turns N; radius r [m]; length l [m] | inductance L [H] | L=μ₀N²πr²/l | forward only | none | no | `SolenoidWidget.csv` | S | `emc::component::solenoid_inductance` in `include/emc/component/inductance.hpp` | TODO |
| Square Loop | `…/Inductance/SquareLoop/SquareLoopWidget.cpp` | turns N; side w [m]; wire radius a [m]; μ_r | inductance L [H] | L=N²(2μ₀μ_rw/π)(ln(w/a)−0.774) | forward only | none | no | `SquareLoopWidget.csv` | S | `emc::component::square_loop_inductance` in `include/emc/component/inductance.hpp` | TODO |
| Toroid | `…/Inductance/Toroid/ToroidWidget.cpp` | turns N; height h [m]; outer b [m]; inner a [m] | inductance L [H] | L=(μ₀N²h/2π)·ln(b/a) | forward only | none | no | `ToroidWidget.csv` | S | `emc::component::toroid_inductance` in `include/emc/component/inductance.hpp` | TODO |
| Via | `…/Inductance/Via/ViaWidget.cpp` | height h [m]; diameter d [m] | inductance L [H] | L=(μ₀h/2π)(ln(4h/d)−1) | forward only | none | no | `ViaWidget.csv` | S | `emc::component::via_inductance` in `include/emc/component/inductance.hpp` | TODO |

### 3c. Resistance → `emc::component`

These share the `enum Material { Custom, Copper, Silver, Gold, Aluminium, Tungsten, Platinum, Lead, Graphite }`
and a copy-pasted `GetResistivity()` (identical in Cylindrical & Rectangular & StandardGaugeWire) that returns
`EXIT_FAILURE` as a sentinel on bad input. All compute AC resistance via skin depth.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Circuit Board Trace | `…/Resistance/CircuitBoardTrace/CircuitBoardTraceWidget.cpp` | frequency [Hz]; length l; width w; thickness t; ρ [Ω·m] | R per unit [Ω/len]; R total [Ω] | δ=1/√(πfμ₀/ρ); R_HF vs R_LF via δ vs geometry | forward only | none (ρ via spinbox) | (resistivity input) | `CircuitBoardTraceWidget.csv` | M | `emc::component::trace_resistance` in `include/emc/component/resistance.hpp` | TODO |
| Cylindrical Conductor | `…/Resistance/CylindricalConductor/CylindricalConductorWidget.cpp` | frequency [Hz]; length l; diameter d; material → ρ; μ_r | R per unit [Ω/len]; R total [Ω] | A=π(d/2)²; δ=1/√(πfμ_rμ₀/ρ); R via δ vs d/4 | forward only | `EXIT_FAILURE` sentinel on bad material | **yes** | `CylindricalConductorWidget.csv` | M | `emc::component::cylindrical_conductor_resistance` in `include/emc/component/resistance.hpp` | TODO |
| Rectangular Conductor | `…/Resistance/RectangularConductor/RectangularConductorWidget.cpp` | frequency [Hz]; length l; width w; thickness t; material → ρ; μ_r | R per unit [Ω/len]; R total [Ω] | as cylindrical with rectangular A_eff | forward only | `EXIT_FAILURE` sentinel | **yes** | `RectangularConductorWidget.csv` | M | `emc::component::rectangular_conductor_resistance` in `include/emc/component/resistance.hpp` | TODO |
| Standard Gauge Wire | `…/Resistance/StandardGaugeWire/StandardGaugeWireWidget.cpp` | frequency [Hz]; length l; AWG gauge (OOOO..); material → ρ; μ_r; (ρ or σ override) | R per unit [Ω/len]; R total [Ω]; back-fills ρ, σ, μ | d_m=0.0254·0.005·92^((36−g)/39); A=π(d_m/2)²; δ; R | forward only | `EXIT_FAILURE` sentinel; gauge string parse | **yes** | `StandardGaugeWireWidget.csv` (`f,l,gauge,matIdx,μ_r`) | L | `emc::component::standard_gauge_wire_resistance` in `include/emc/component/resistance.hpp` | TODO |

> AWG gauge string mapping (`OOOO`→−3, `OOO`→−2, `OO`→−1, `O`→0, else `toInt()`) becomes a small parser returning
> `std::expected<int, Error>` so the EXIT_FAILURE sentinel disappears (see Bugs list).

### 3d. Circuit Board Trace Impedance → `emc::component`

The four most complex calculators. Each is **bidirectional**: a `Z0` solve plus `calH`/`calT`/`calW` (and
`calC` for dual) solvers, each containing an identical ~8-branch nested `if` tree to apply mm-vs-mils unit
conversions, plus inline `QMessageBox::warning` validation.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula (Z0 form) | Solve directions | Validation ranges | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Microstrip Trace | `…/CircuitBoardTraImp/MicrostripTrace/MicrostripTraceWidget.cpp` | h, t, w [mm/mils]; ε_r | Z₀ [Ω]; C₀ [pF/len]; T_pd [ps/len] | Z₀=87·ln(5.98H/(0.8W+T))/√(ε_r+1.41) | **solves for Z0, H, T, W** | `1≤ε_r≤15`; `0.1≤W/H≤3`; H,W,T,Z>0 (QMessageBox) | no | `MicrostripTraceWidget.csv` (`h,t,w,ε_r`) | L | `emc::component::microstrip_trace` in `include/emc/component/microstrip_trace.hpp` | TODO |
| Stripline Trace | `…/CircuitBoardTraImp/StriplineTrace/StriplineTraceWidget.cpp` | h, t, w [mm/mils]; ε_r | Z₀ [Ω]; C₀ [pF/len]; T_pd [ps/len] | Z₀=60·ln(4(2H+T)/(0.67π(0.8W+T)))/√ε_r | **solves for Z0, H, T, W** | ε_r range; QMessageBox on input error | no | `StriplineTraceWidget.csv` | L | `emc::component::stripline_trace` in `include/emc/component/stripline_trace.hpp` | TODO |
| Dual Stripline Trace | `…/CircuitBoardTraImp/DualStriplineTrace/DualStriplineTraceWidget.cpp` | h, c, t, w [mm/mils]; ε_r | Z₀ [Ω]; C₀ [pF/len]; T_pd [ps/len] | Z₀=½[60·ln(8H/(0.67π(0.8W+T)))/√ε_r + 60·ln(8(H+C)/(0.67π(0.8W+T)))/√ε_r] | **solves for Z0, H, C, T, W** | `1≤ε_r≤15`; QMessageBox | no | `DualStriplineTraceWidget.csv` | L | `emc::component::dual_stripline_trace` in `include/emc/component/dual_stripline_trace.hpp` | TODO |
| Embedded Microstrip | `…/CircuitBoardTraImp/EmbeddedMicrostripTrace/EmbeddedMicrostripTraceWidget.cpp` | h1, h, t, w [mm/mils]; ε_r | Z₀ [Ω]; C₀ [pF/len]; T_pd [ps/len] | Z₀=87·ln(5.98H/(0.8W+T))·(1−(h−H−T)/0.1)/√(ε_r+1.41) | **solves for Z0 (+ H/T/W variants)** | parameters-out-of-range QMessageBox | no | `EmbeddedMicrostripTraceWidget.csv` | L | `emc::component::embedded_microstrip_trace` in `include/emc/component/embedded_microstrip_trace.hpp` | TODO |

> **The 8-branch unit tree** in each `calH/calT/calW` is purely mm↔mils bookkeeping. With `mp-units`, inputs
> arrive already typed (e.g. `quantity<si::milli<si::metre>>`); the conversion is one implicit cast and the
> branches vanish (this is the headline example cited throughout `02-modern-cpp-feature-catalog.md` and
> `03-quantities-and-units-mp-units.md`). Model the four solve targets with a single `Input` carrying
> `std::optional<quantity>` for the unknown, or four named functions sharing one `detail::` core.

### 3e. Transmission Line Parameters → `emc::component`

Eight calculators. Five (`NarrowTraceOverPlane`, `WideTraceOverPlane`, `WireOverPlane`, `WirePair`) re-define
`qreal mu0 = 4*M_PI*1e-7` locally and share the L/C/Z₀/R-per-length pattern.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Coaxial Line | `…/TransmissionLineParameters/CoaxialLineWidget/CoaxialLineWidget.cpp` | outer dia D; inner dia d; ε_r | Z₀ [Ω]; cutoff f [Hz]; C [pF/len]; L [nH/len] | Z₀=138·log₁₀(D/d)/√ε_r; f_c=11.8/(√ε_r·π·(D+d)/2); C=7.354ε_r/log₁₀(D/d); L=140.4·log₁₀(D/d) | forward only (4 outputs) | none | no | `CoaxialLineWidget.csv` | M | `emc::component::coaxial_line` in `include/emc/component/transmission_line.hpp` | TODO |
| Microstrip Line | `…/TransmissionLineParameters/MicroStripLineWidget/MicroStripLineWidget.cpp` | ε_r; width w; height h | ε_eff [-]; Z₀ [Ω] | ε_eff=(ε_r+1)/2+(ε_r−1)/2·f(h/w); Z₀ branches on W/H≤1 | forward only (branch) | implicit W/H branch | no | `MicroStripLineWidget.csv` | M | `emc::component::microstrip_line` in `include/emc/component/transmission_line.hpp` | TODO |
| Stripline | `…/TransmissionLineParameters/StripLineWidget/StripLineWidget.cpp` | ε_r; width w; height h; thickness t | Z₀ [Ω] | Z₀=(60/√ε_r)·ln(1.9(2h+t)/(0.8w+t)) | forward only | none | no | `StripLineWidget.csv` | S | `emc::component::stripline` in `include/emc/component/transmission_line.hpp` | TODO |
| Narrow Trace Over Plane | `…/TransmissionLineParameters/NarrowTraceOverPlaneWidget/NarrowTraceOverPlaneWidget.cpp` | frequency [Hz]; trace height h; width w; thickness t; σ; ε | L [H/len]; C [F/len]; Z₀ [Ω]; R [Ω/len] | δ=1/√(πf μ₀ σ); Z₀=√(1e6·L/C); standard PCB-trace L/C | forward only (multi-output) | none | (σ,ε inputs) | `NarrowTraceOverPlaneWidget.csv` | M | `emc::component::narrow_trace_over_plane` in `include/emc/component/transmission_line.hpp` | TODO |
| Wide Trace Over Plane | `…/TransmissionLineParameters/WideTraceOverPlaneWidget/WideTraceOverPlaneWidget.cpp` | frequency; trace height h; width w; thickness t; σ; ε | L [H/len]; C [F/len]; Z₀ [Ω]; R [Ω/len] | L_pul=μ₀μ_r·h/w (`///`); Z₀=√(1e6·L/C); δ | forward only (multi-output) | comment-only `w<5h` guard (disabled) | (σ,ε inputs) | `WideTraceOverPlaneWidget.csv` | M | `emc::component::wide_trace_over_plane` in `include/emc/component/transmission_line.hpp` | TODO |
| Wire Over Plane | `…/TransmissionLineParameters/WireOverPlaneWidget/WireOverPlaneWidget.cpp` | frequency; wire height h; radius a; σ | L [H/len]; C [F/len]; Z₀ [Ω]; R [Ω/len] | δ; A_eff=πa²; Z₀=√(1e6·L/C) | forward only (multi-output) | "Input Error" text sentinel | (σ input) | `WireOverPlaneWidget.csv` | M | `emc::component::wire_over_plane` in `include/emc/component/transmission_line.hpp` | TODO |
| Wire Pair | `…/TransmissionLineParameters/WirePairWidget/WirePairWidget.cpp` | frequency; geometry; σ; ε | L [H/len]; C [F/len]; Z₀ [Ω]; R [Ω/len] | δ; A_eff=πa²; Z₀=√(1e6·L/C); m vs ft unit switch | forward only (multi-output) | none | (σ,ε inputs) | `WirePairWidget.csv` | M | `emc::component::wire_pair` in `include/emc/component/transmission_line.hpp` | TODO |

> **Why split this header:** `transmission_line.hpp` groups the seven simple/medium ones; the four bidirectional
> board-impedance solvers each get their own header (3d) because their solve-for-X surface is large.

### 3f. Harmonic Trap → `emc::component`

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Harmonic Trap (waveform) | `…/HarmTraplWF/HarmTraplWFWidget.cpp` | harmonic n; amplitude A_m [V]; transition time t_r [s]; period T [s]; duty cycle DC [%] | fundamental f₀ [Hz]; harmonic f [Hz]; amp of harmonic A_h [V_rms]; amp of envelope A_e [V_rms] | f₀=1/T; A_h=1.414·A_m·(DC/100)·|sinc(nπDC/100)|·|sinc(nπt_r/T)|; envelope breakpoints at 1/(πτ) and 1/(πt_r) | forward only (multi-output, branch) | none (but DC=0 → div/0) | no | `HarmTraplWF.csv` (5 cols) | M | `emc::component::harmonic_trap` in `include/emc/component/harmonic_trap.hpp` | TODO |

> **Note:** local `if/else` unit trees for `transitionTime` (s/ms/us/ns) and `period` units — replaced by typed
> `quantity<isq::time>` inputs.

---

## 4. EMCPredictions → `emc::prediction`

`EMCPredictionsWidget`, `ESDandLightningCouplingCalculatorWidget`, `RFFieldCalculatorWidget` are navigation hubs.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| ESD Coupling Level | `EMCPredictions/ESDandLightningCouplingCalculator/ESDCouplingLevelWidget/ESDCouplingLevelWidget.cpp` | loop height h [m]; radius r [m]; distance d [m]; peak current I_peak [A]; rise time t_r [ns] | induced voltage V_ind [mV] | V_ind=(μ₀h/2π)·ln((r+d)/r)·(I_peak/(t_r·1e−9)) | forward only | none (t_r=0 → div/0) | no | `ESDCouplingLevelWidget.csv` (5 cols) | S | `emc::prediction::esd_coupling` in `include/emc/prediction/esd_coupling.hpp` | TODO |
| Lightning Coupling Level | `EMCPredictions/ESDandLightningCouplingCalculator/LightningCouplingLevelWidget/LightningCouplingLevelWidget.cpp` | loop height h [m]; radius r [m]; distance d [m]; current slew dI/dt [A/s] | induced voltage V_ind [V] | V_ind=(μ₀h/2π)·ln((r+d)/r)·dI/dt | forward only | none | no | `LightningCouplingLevelWidget.csv` (4 cols) | S | `emc::prediction::lightning_coupling` in `include/emc/prediction/lightning_coupling.hpp` | TODO |
| RF E-Field (from EIRP) | `EMCPredictions/RFFieldCalculator/EFieldFormulaWidget/EFieldFormulaWidget.cpp` | transmit power P_t [dBm]; gain G_t [dBi]; distance d [km/…] | E-field [V/m]; H-field [A/m]; power density [W/m²] | P_t_W=10^(P_t/10)/1000; E=√(30·P_t_W·G_t_W)/d; H=E/(120π); P_D=E·H | forward only (3 outputs) | none | no | `EFieldFormulaWidget.csv` (3 cols) | M | `emc::prediction::rf_efield` in `include/emc/prediction/rf_field.hpp` | TODO |
| Friis Transmission | `EMCPredictions/RFFieldCalculator/FrissTransmission/FriisTransmissionWidget.cpp` | P_tx [W]; G_tx [dBi]; G_rx [dBi]; frequency [Hz]; range R [m] | received power P_rx [dBm] | P_rx=30+10·log₁₀(P_tx·10^(G_tx/10)·10^(G_rx/10)·(c/(4πRf))²) | forward only | none | no | `FriisTransmissionWidget.csv` (5 cols) | S | `emc::prediction::friis_transmission` in `include/emc/prediction/friis.hpp` | TODO |

---

## 5. Shielding → `emc::shielding`

Two navigation hubs (`CavityResonanceCalculatorWidget`, `EMShieldingEffectivenessCalculatorWidget`).

### 5a. Cavity Resonance → `emc::shielding`

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Rectangular Enclosure | `Shielding/CavityResonanceCalculatorWidget/RectangularEnclosureWidget/RectangularEnclosureWidget.cpp` | length l [m]; width w [m]; height h [m]; ε_r | **12 resonant modes** f₁₁₀,f₁₁₁,f₂₁₁,f₂₂₀,f₁₀₁,f₂₀₁,f₂₁₀,f₂₂₁,f₀₁₁,f₁₂₀,f₀₂₁,… [Hz] | f_mnp=(1.5e8/√ε_r)·√((m/l)²+(n/w)²+(p/h)²) | forward only (12 outputs) | none | no | `RectangularEnclosureWidget.csv` (4 cols) | L | `emc::shielding::rectangular_cavity_modes` in `include/emc/shielding/cavity_resonance.hpp` | TODO |
| Cylindrical Enclosure | `Shielding/CavityResonanceCalculatorWidget/CylindricalEnclosureWidget/CylindricalEnclosureWidget.cpp` | length l [m]; radius r [m]; ε_r | many TM (mf…) and TE (ef…) mode frequencies [Hz] | f=(4.7714e7/√ε_r)·√((x_mn/r)²+(pπ/l)²) with Bessel roots 2.405/3.832/5.520/7.016/5.135/… | forward only (multi-output) | none | no | `CylindricalEnclosureWidget.csv` | L | `emc::shielding::cylindrical_cavity_modes` in `include/emc/shielding/cavity_resonance.hpp` | TODO |
| Circuit Board Planes | `Shielding/CavityResonanceCalculatorWidget/CircuitBoardPlanesWidget/CircuitBoardPlanesWidget.cpp` | length l [m]; width w [m]; thickness [m]; ε_r | mode frequencies f₁₀,f₀₁,f₁₁,f₂₀,… [Hz] | f=(1.5e8/√ε_r)·√((m/l)²+(n/w)²) with thin-plane guards | forward only (multi-output) | `ε_r<1` or thickness>0.1·dim → "Input Error" | no | `CircuitBoardPlanesWidget.csv` | L | `emc::shielding::circuit_board_plane_modes` in `include/emc/shielding/cavity_resonance.hpp` | TODO |

> The `f110..f221` modes become a `std::array<ModeFrequency, N>` or `std::mdspan` over (m,n,p) instead of 12+
> separately-named spinboxes (see `02-modern-cpp-feature-catalog.md`, `std::mdspan` / `std::array` entry).

### 5b. EM Shielding Effectiveness → `emc::shielding`

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Aperture | `Shielding/EMShieldingEffectivenessCalculatorWidget/ApertureWidget/ApertureWidget.cpp` | depth d [in]; for slot: width w; for round: diameter D | absorption loss AL [dB] | slot: AL=27.3·d/w; round: AL=32·d/D | forward only (shape branch) | none | no | `ApertureWidget.csv` (`d,D,w`) | S | `emc::shielding::aperture_loss` in `include/emc/shielding/aperture.hpp` | TODO |
| Near-Field SE | `Shielding/EMShieldingEffectivenessCalculatorWidget/NearFieldShieldingEffectivenessWidget/NearFieldShieldingEffectivenessWidget.cpp` | material (Cu/Al/Au/Other) → σ,μ_r; thickness t; distance r; frequency [Hz]; field type (E/H) | absorption AL [dB]; reflection RL [dB]; total SE [dB] | δ=1/√(π²·4e−7·μ_r·σ·f); AL=8.7·t/δ; Z_w E or H; RL=20·M_LOG10E·ln(Z_w/(4·N_s)) | forward only (E/H branch) | none | **yes** (σ: Cu 5.80, Al 3.78, Au 4.52 ×10⁷) | `NearFieldShieldingEffectivenessWidget.csv` | M | `emc::shielding::near_field_se` in `include/emc/shielding/shielding_effectiveness.hpp` | TODO |
| Plane-Wave SE | `Shielding/EMShieldingEffectivenessCalculatorWidget/PlaneWaveShieldingEffectivenessWidget/PlaneWaveShieldingEffectivenessWidget.cpp` | material → σ,μ_r; thickness t; frequency [Hz] | absorption AL [dB]; reflection RL [dB]; total SE [dB] | δ; AL=8.7·t/δ; RL=20·M_LOG10E·ln(377/(4·N_s)) | forward only | none | **yes** (same σ table) | `PlaneWaveShieldingEffectivenessWidget.csv` | M | `emc::shielding::plane_wave_se` in `include/emc/shielding/shielding_effectiveness.hpp` | TODO |
| Slot (λ/2 resonance) | `Shielding/EMShieldingEffectivenessCalculatorWidget/SlotWidget/SlotWidget.cpp` | frequency [Hz]; slot length [m] | wavelength λ [m]; SE [dB] | λ=c/f; SE=20·log₁₀(λ/(2·length)) | forward only | none | no | `SlotWidget.csv` (`f,length`) | S | `emc::shielding::slot_se` in `include/emc/shielding/slot.hpp` | TODO |

> **Bug found:** `PlaneWaveShieldingEffectivenessWidget.cpp` line 97 uses the broken macro `PI` (= 3.14) inside
> `N_s = √(2·PI²·4e−7·μ_r·f/σ)`, while the sibling `NearField` file uses `M_PI` for the same quantity. This
> makes the two calculators disagree on otherwise-identical physics. Both must use `emc::constants::pi`.

---

## 6. Filtering → `emc::filtering`

`FilteringWidget` and `FerriteCalculatorWidget` are navigation hubs.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Ferrite Toroid Impedance | `Filtering/FerriteCalculatorWidget/FerriteToroidWidget/FerriteToroidWidget.cpp` | turns N; μ_r′ (real); μ_r″ (imag); height h [m]; inner a [m]; outer b [m]; frequency [Hz] | L [H]; reactance X [Ω]; resistance R [Ω]; impedance |Z| [Ω] | L=(N²μ₀h/2π)·ln(b/a); X=2πf·μ_r′·L; R=2πf·μ_r″·L; Z=√(R²+X²) | forward only (4 outputs) | none (a=0 → ln domain error) | (complex μ_r input) | `FerriteToroidWidget.csv` (7 cols) | M | `emc::filtering::ferrite_toroid` in `include/emc/filtering/ferrite_toroid.hpp` | TODO |

---

## 7. Cabling → `emc::cabling`

`CablingWidget` is navigation-only.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Cable Braid Optical Coverage | `Cabling/CableBraidOpticalCoverageWidget/CableBraidOpticalCoverageWidget.cpp` | strand dia d [m]; braid OD D [m]; picks P [/len]; carriers C; ends N | optical coverage OC [%] | θ=atan(2π(D+2d)P/C); F=(P·N·d)/sin θ; OC=100·(2F−F²) | forward only | none | no | `CableBraidOpticalCoverageWidget.csv` (5 cols) | S | `emc::cabling::braid_optical_coverage` in `include/emc/cabling/braid_coverage.hpp` | TODO |
| Crosstalk (NEXT/FEXT) | `Cabling/KrosstalkCalculatorWidget/KrosstalkCalculatorWidget.cpp` | frequency [Hz]; R_L; R_S; R_NE; R_FE [Ω]; mutual L_m [H]; mutual C_m [F] | near-end V_NE [dB]; far-end V_FE [dB] | V_NE=20·log₁₀(2πf·[(R_NE/(R_NE+R_FE))·L_m/(R_S+R_L)+(R_NE·R_FE/(R_NE+R_FE))·R_L·C_m/(R_S+R_L)]); V_FE analogous (sign flip) | forward only (2 outputs) | inner term ≤0 → clamp V_FE=−200 | no | `KrosstalkCalculatorWidget.csv` (7 cols) | M | `emc::cabling::crosstalk` in `include/emc/cabling/crosstalk.hpp` | TODO |

---

## 8. Grounding → `emc::grounding`

`GroundingWidget` is navigation-only.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Microstrip Line Current Distribution | `Grounding/MicrostripLineCurrentDistributionWidget/MicrostripLineCurrentDistributionWidget.cpp` | source current I₀ [A]; trace width w [m]; height h [m]; lateral position x [m] | ground-plane current density J [A/m] | J=(I₀/(πw))·1/(1+(x/h)²) | forward only | none | no | `MicrostripLineCurrentDistributionWidget.csv` (4 cols) | S | `emc::grounding::microstrip_current_distribution` in `include/emc/grounding/microstrip_current.hpp` | TODO |

---

## 9. Testing → `emc::testing`

`TestingWidget` is navigation-only.

| Calculator | Source file | Inputs (with units) | Output(s) (with units) | Core formula | Solve | Validation | Mat? | CSV? | Cx | Proposed library location | Status |
|---|---|---|---|---|---|---|:--:|:--:|:--:|---|---|
| Noise Figure of an RF Receiver | `Testing/NoiseFigureofanRFReceiverWidget/NoiseFigureofanRFReceiverWidget.cpp` | per-stage NF [dB] and gain [dB] (up to 3 stages) | cascade noise figure [dB]; total gain [dB] | Friis cascade: F=F₁+(F₂−1)/G₁+(F₃−1)/(G₁G₂)…; NF=10·log₁₀(F); G_total=ΣG_i (dB) | forward only (N-stage) | none | no | `NoiseFigureofanRFReceiverWidget.csv` (6 cols: 3×(NF,G)) | M | `emc::testing::noise_figure` in `include/emc/testing/noise_figure.hpp` | TODO |

> **Modeling note:** the cascade naturally takes a `std::span<const Stage>` (each `{ nf_dB, gain_dB }`) instead
> of three hard-wired spinbox pairs, generalizing 3-stage → N-stage for free (see `06-calculator-design-pattern.md`).

---

## Notable bugs / inconsistencies to fix during migration

These were observed directly in the source while building this inventory. Each must be addressed (and any
affected golden vector **re-blessed**) during the port — tracked in `09-testing-and-golden-vectors.md` and
`10-migration-roadmap.md`. Constants/materials fixes are detailed in `04-constants-and-material-database.md`.

1. **`#define PI 3.14`** in `src/Utilites/HelperTypes.h:12` — a real precision bug. Used (via the `PI` token)
   in `PlaneWaveShieldingEffectivenessWidget.cpp` (`N_s`) and elsewhere; the literal `3.1415926` is hard-coded
   in `StandardGaugeWireWidget.cpp`, `CircuitBoardTraceWidget.cpp`, `Cylindrical`/`RectangularConductorWidget.cpp`,
   the transmission-line widgets, etc. Replace all with `emc::constants::pi` (full `double`/`long double`).
2. **`#define SPEEDOFLIGHT 300000000.0`** (`HelperTypes.h:10`) — imprecise; true c = 299 792 458 m/s. Affects
   every wavelength/Friis/cavity/far-field result. `FarFieldCriteriaWidget.cpp` independently uses `3*qPow(10,8)`.
   Cavity widgets bake in `1.5e8` (= c/2 with the wrong c) and `4.7714e7`. Replace with `emc::constants::c`.
3. **`#define PLANCK_CONSTANT 6.62606957e-34`** (`HelperTypes.h:11`) — superseded 2014 CODATA value; the SI
   definition fixes h = 6.626 070 15e−34. Used by `EnergyVsFrequency.cpp`. Adopt `emc::constants::h`.
4. **`qreal mu0 = 4*M_PI*1e-7` redefined in 8+ files** — `SkinDepthWidget.h:25`, `FerriteToroidWidget`,
   `ESDCouplingLevelWidget`, `LightningCouplingLevelWidget`, and the four `*OverPlane`/`WirePair` transmission
   widgets each declare their own. Collapse to one `emc::constants::mu0`.
5. **`#define PermofFreeSpace ((4*M_PI)/10000000.0)` copy-pasted across 6 Inductance headers**
   (CircularLoop, ConnectorPin, RectangularLoop, Solenoid, SquareLoop, Via); `ToroidWidget.cpp` instead inlines
   `4*M_PI*1e-7`. Same constant, two spellings → one `emc::constants::mu0`.
6. **`EXIT_FAILURE` returned as a numeric sentinel** from `GetResistivity()` in `StandardGaugeWireWidget.cpp`,
   `CylindricalConductorWidget.cpp`, `RectangularConductorWidget.cpp`. A bad material silently yields resistivity
   = 1 (the value of `EXIT_FAILURE`), corrupting the result. Replace with `std::expected<…, emc::Error>`.
7. **Duplicated, inconsistent material tables.** Resistance widgets share one `enum Material`
   {Custom,Copper,Silver,Gold,Aluminium,Tungsten,Platinum,Lead,Graphite} with resistivities (Cu 1.72e−8,
   Ag 1.59e−8, Au 2.40e−8, Al 2.82e−8, …). But `SkinDepthWidget.cpp` hardcodes a **different** material set by
   *conductivity* (Cu 5.8005e7, Al 3.5386e7, Au 4.0984e7, Ag 6.1728e7, Nickel 1.4493e7 with μ_r=600), and the
   shielding widgets use yet another scaled set (Cu 5.80, Al 3.78, Au 4.52 ×10⁷). σ and 1/ρ disagree across
   files (e.g. 1/1.72e−8 = 5.81e7 ≈ SkinDepth's 5.8005e7, but Al 1/2.82e−8 = 3.55e7 ≠ shielding's 3.78e7).
   Consolidate into one `emc::materials` database (see `04-constants-and-material-database.md`).
8. **Inline `QMessageBox::warning` validation mixed into math** in the four CircuitBoardTraImp widgets
   (`MicrostripTraceWidget.cpp` checks `1≤ε_r≤15`, `0.1≤W/H≤3`, positivity), `Stripline`, `DualStripline`,
   `EmbeddedMicrostrip`. Move to `validate(const Input&) -> std::expected<void, Error>`.
9. **"Input Error" written into result fields as strings** (`WireOverPlaneWidget.cpp`,
   `CircuitBoardPlanesWidget.cpp`) — a UI-side error channel that corrupts the numeric output. Becomes an
   `emc::Error`.
10. **8-branch mm/mils unit `if`-trees duplicated 4× per file** across `calH/calT/calW` in every
    CircuitBoardTraImp widget — eliminated by `mp-units` typed inputs.
11. **VSWR return-loss comment/code mismatch + log(0)** at VSWR=1 (`VSWR_RC_RL_ML_TL.cpp`, see §2). Add
    `validate` and re-bless.
12. **Division-by-zero risks unguarded:** `ESDCouplingLevelWidget` (`t_r=0`), `HarmTraplWFWidget` (`DC=0`),
    `FerriteToroidWidget`/`Toroid`/`CircularLoop` (`a=0` → `ln` domain error), capacitance (`d=0`). The new
    `validate()` step must reject these with a descriptive `emc::Error` instead of producing `inf`/`nan`.
13. **Orphan golden fixture** `MicroStripAntennaWidget.csv` with no source (§1). Delete it; it cannot be a
    regression vector for anything.
14. **Capacitance double-pico-scaling fudge** (`ParallelPlate`/`Sphere`, §3a) — masked by magic constants
    8.85 and 111; `mp-units` + `ε₀` makes it dimensionally exact.
15. **`GLOB_RECURSE` build** means new files are silently picked up; the new library lists sources explicitly
    (see `08-build-system-cmake.md`).

---

## Cross-references

- `00-overview-and-goals.md` — vision, scope, before/after, success criteria.
- `01-architecture-and-layout.md` — namespace/directory layout these `Proposed library location` cells follow.
- `02-modern-cpp-feature-catalog.md` — the feature→pain-point catalog (mdspan for cavity modes, span for noise
  cascade, expected for the EXIT_FAILURE sentinel, etc.).
- `03-quantities-and-units-mp-units.md` — how the 541 `addItem(unit, factor)` conversions and the mm/mils
  `if`-trees collapse into typed quantities.
- `04-constants-and-material-database.md` — the single source of truth for the constants and material tables
  listed in the Bugs section.
- `05-error-handling-and-validation.md` — `emc::Error` / `std::expected`, replacing QMessageBox/EXIT_FAILURE/"Input Error".
- `06-calculator-design-pattern.md` — the Input/Result/calculate/validate pattern; two of these calculators are
  the full worked examples there (Skin Depth + Microstrip Trace).
- `09-testing-and-golden-vectors.md` — reusing the 51 CSVs (minus the orphan) as Qt-free regression vectors.
- `10-migration-roadmap.md` — uses the **Status** column of every table above as the phased checklist.
