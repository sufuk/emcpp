# Calculator Catalog & Library Map 📐

Authoritative catalog of every calculator in the `emc` library, mapped to its target location. **51 calculators**
across nine categories. Conventions follow the locked design (`01-architecture-and-layout.md`,
`06-calculator-design-pattern.md`):

- Each calculator is a **free function** `calculate(const Input&) -> std::expected<Result, emc::Error>` in a
  category namespace, with aggregate `Input`/`Result` structs and — where inputs have a bounded physical
  domain — a `validate(const Input&) -> std::expected<void, emc::Error>`.
- Header mirrors the namespace: `include/emc/<category>/<snake_name>.hpp`; impl in `src/<category>/<name>.cpp`.
- All physical quantities are `mp-units` `quantity<>` types — never bare `double`. The Inputs/Outputs columns
  name the **physical** quantity; the SI unit shown is the canonical storage unit.

> [!NOTE]
> Per-category tables share columns: **Calculator | Inputs | Output(s) | Core formula | Solve directions |
> Validity constraints | Material? | Library location**. "Material?" marks calculators that read a
> conductivity/permeability/resistivity table.

Modeling each calculator as `calculate(Input) -> expected<Result>` keeps them pure functions of their inputs:
trivially thread-safe, constexpr-friendly, and drivable from a generic front end (e.g. `std::print`) with no UI
dependency.

---

## 1. BasicCalculations → `emc::basic`

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Skin Depth | frequency [Hz]; conductivity σ [S/m]; relative permeability μ_r [-] (from material) | skin depth δ [m] | δ = √(1 / (π·f·μ₀μ_r·σ)) | forward only | f, σ, μ_r > 0 | **yes** | `emc::basic::skin_depth` in `include/emc/basic/skin_depth.hpp` |
| Decibel Calculator | one of: dB; or dBm + load R [Ω]; or power [W]; or voltage [V]; or Vp sinusoid [V] | voltage gain [-]; power gain [-]; power [W]; voltage(DC) [V]; Vp of sinusoid [V] | dB = 20·log₁₀(V₁/V₂) = 10·log₁₀(P₁/P₂); dBm = 10·log₁₀(P/0.001); V = √(P·R) | **bidirectional** (any field drives the rest) | R > 0 for dBm conversions | no | `emc::basic::decibel` in `include/emc/basic/decibel.hpp` |
| Dipole Antenna | current I₀ [A]; length l [m]; distance R [m]; frequency [Hz]; angle θ [deg] | E_r [V/m]; E_θ [V/m]; H_φ [A/m] | E_r = 60·(I₀l/R²)·cosθ·√(1+(c/2πfR)²); E_θ, H_φ analogous | forward only (multi-output) | R, f > 0 | no | `emc::basic::dipole_antenna` in `include/emc/basic/dipole_antenna.hpp` |
| Loop Antenna | current I₀ [A]; loop area A [m²]; distance R [m]; frequency [Hz]; angle θ [deg] | H_r [A/m]; H_θ [A/m]; E_φ [V/m] | H_r = (f/c)(I₀A/R²)cosθ√(1+(c/2πfR)²); E_φ = 120(πf/c)²(I₀A/R)sinθ√(…) | forward only (multi-output) | R, f > 0 | no | `emc::basic::loop_antenna` in `include/emc/basic/loop_antenna.hpp` |
| Far-Field Criteria | frequency [Hz]; max dimension D [m] | wavelength λ [m]; reactive near-field [m]; radiating near-field [m] | λ = c/f; if D > λ/10: reactive = 0.62√(D³/λ), radiating = 2D²/λ; else reactive = λ/50, radiating = λ | forward only (branch on D vs λ/10) | f, D > 0 | no | `emc::basic::far_field_criteria` in `include/emc/basic/far_field_criteria.hpp` |

Typed `quantity<>` inputs span Hz..GHz and m..mils, so compile-time unit safety prevents silent scale errors;
`std::expected` reports out-of-domain inputs (non-positive f/R) as typed errors instead of `inf`/`nan`.

---

## 2. Converter → `emc::converter`

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Antenna Factor → Gain | frequency [Hz]; antenna factor AF [dB/m] | gain [dBi] | λ = c/f; gain = 10·log₁₀((9.73/(λ·10^(AF/20)))²) | forward only | f > 0 | no | `emc::converter::antenna_factor_to_gain` in `include/emc/converter/antenna_factor.hpp` |
| E-Field → Power Density | electric field E [V/m]; wave impedance η [Ω] (default 377) | power density P_D [W/m²] | P_D = E²/η | forward only | η > 0 | no | `emc::converter::efield_to_power_density` in `include/emc/converter/efield_power_density.hpp` |
| Energy ↔ Frequency | energy [eV] **or** frequency [Hz] | frequency [Hz] **or** energy [eV] | E = h·f | **bidirectional** | inputs > 0 | no | `emc::converter::energy_frequency` in `include/emc/converter/energy_frequency.hpp` |
| Wavelength ↔ Frequency | wavelength λ [m] **or** frequency [Hz] | frequency [Hz] **or** wavelength λ [m] | λ = c/f and f = c/λ | **bidirectional** | inputs > 0 | no | `emc::converter::wavelength_frequency` in `include/emc/converter/wavelength_frequency.hpp` |
| VSWR / RC / RL / ML / IL | VSWR [-] | reflection coeff Γ [-]; return loss [dB]; mismatch loss [dB]; insertion loss [dB] | Γ = (VSWR−1)/(VSWR+1); RL = −20·log₁₀\|Γ\|; ML = −10·log₁₀(1−Γ²); IL = −10·log₁₀(\|1+Γ\|²) | forward only (multi-output) | VSWR ≥ 1 | no | `emc::converter::vswr` in `include/emc/converter/vswr.hpp` |

> [!WARNING]
> The VSWR family needs `validate` to enforce `VSWR ≥ 1`: at VSWR = 1, Γ = 0 and `log₁₀\|Γ\|` diverges to −∞.
> The constraint makes the singular case a typed error instead of `-inf`.

Bidirectional converters carry `std::optional<quantity>` for the unknown side, so one function solves both
directions with no duplicated code.

---

## 3. ComponentCalculations → `emc::component`

The largest category — 25 leaf calculators across capacitance, inductance, resistance, board-trace impedance,
transmission lines, and a harmonic trap.

### 3a. Capacitance

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Parallel Plate | area A [m²]; distance d [m] | capacitance C [F] | C = ε₀·A/d | forward only | d > 0 | no | `emc::component::parallel_plate_capacitance` in `include/emc/component/capacitance.hpp` |
| Sphere | radius r [m] | capacitance C [F] | C = 4πε₀·r | forward only | r > 0 | no | `emc::component::sphere_capacitance` in `include/emc/component/capacitance.hpp` |

Writing the constants as `ε₀` keeps the formula dimensionally exact; the result carries farads natively, so no
manual pico-scaling.

### 3b. Inductance

All seven use the single `emc::constants::mu0`.

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Circular Loop | turns N; radius R [m]; wire radius a [m]; μ_r | inductance L [H] | L = N²Rμ₀μ_r·(ln(8R/a)−2) | forward only | a > 0, R > a | no | `emc::component::circular_loop_inductance` in `include/emc/component/inductance.hpp` |
| Connector Pin | length l [m]; radius r [m]; spacing s [m] | self-L [H]; mutual M_p [H] | L = (μ₀l/2π)(ln(2l/r)−¾); M = (μ₀l/2π)(ln(2l/s)−1) | forward only (2 outputs) | r, s > 0 | no | `emc::component::connector_pin_inductance` in `include/emc/component/inductance.hpp` |
| Rectangular Loop | turns N; width w [m]; height h [m]; wire radius a [m]; μ_r | inductance L [H] | L = N²(μ₀μ_r/π)·[−2(w+h)+2√(w²+h²)−h·ln(…)−w·ln(…)+h·ln(2h/a)+w·ln(2w/a)] | forward only | a > 0 | no | `emc::component::rectangular_loop_inductance` in `include/emc/component/inductance.hpp` |
| Solenoid | turns N; radius r [m]; length l [m] | inductance L [H] | L = μ₀N²πr²/l | forward only | l > 0 | no | `emc::component::solenoid_inductance` in `include/emc/component/inductance.hpp` |
| Square Loop | turns N; side w [m]; wire radius a [m]; μ_r | inductance L [H] | L = N²(2μ₀μ_rw/π)(ln(w/a)−0.774) | forward only | a > 0, w > a | no | `emc::component::square_loop_inductance` in `include/emc/component/inductance.hpp` |
| Toroid | turns N; height h [m]; outer b [m]; inner a [m] | inductance L [H] | L = (μ₀N²h/2π)·ln(b/a) | forward only | a > 0, b > a | no | `emc::component::toroid_inductance` in `include/emc/component/inductance.hpp` |
| Via | height h [m]; diameter d [m] | inductance L [H] | L = (μ₀h/2π)(ln(4h/d)−1) | forward only | d > 0 | no | `emc::component::via_inductance` in `include/emc/component/inductance.hpp` |

### 3c. Resistance

These share a material enum {Custom, Copper, Silver, Gold, Aluminium, Tungsten, Platinum, Lead, Graphite} and a
common resistivity lookup. All compute AC resistance via skin depth.

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Circuit Board Trace | frequency [Hz]; length l; width w; thickness t; ρ [Ω·m] | R per unit [Ω/len]; R total [Ω] | δ = 1/√(πfμ₀/ρ); R_HF vs R_LF via δ vs geometry | forward only | f, geometry, ρ > 0 | resistivity input | `emc::component::trace_resistance` in `include/emc/component/resistance.hpp` |
| Cylindrical Conductor | frequency [Hz]; length l; diameter d; material → ρ; μ_r | R per unit [Ω/len]; R total [Ω] | A = π(d/2)²; δ = 1/√(πfμ_rμ₀/ρ); R via δ vs d/4 | forward only | valid material; d > 0 | **yes** | `emc::component::cylindrical_conductor_resistance` in `include/emc/component/resistance.hpp` |
| Rectangular Conductor | frequency [Hz]; length l; width w; thickness t; material → ρ; μ_r | R per unit [Ω/len]; R total [Ω] | as cylindrical with rectangular A_eff | forward only | valid material; w, t > 0 | **yes** | `emc::component::rectangular_conductor_resistance` in `include/emc/component/resistance.hpp` |
| Standard Gauge Wire | frequency [Hz]; length l; AWG gauge; material → ρ; μ_r | R per unit [Ω/len]; R total [Ω]; ρ, σ, μ | d_m = 0.0254·0.005·92^((36−g)/39); A = π(d_m/2)²; δ; R | forward only | valid material; parseable gauge | **yes** | `emc::component::standard_gauge_wire_resistance` in `include/emc/component/resistance.hpp` |

> [!NOTE]
> The AWG gauge string (`OOOO`→−3, `OOO`→−2, `OO`→−1, `O`→0, else integer) is parsed by a small helper
> returning `std::expected<int, Error>`, so a malformed gauge becomes a typed error.

A bad material returns a typed error from the lookup rather than a numeric sentinel; a shared `emc::materials`
table is the one source of truth for σ/ρ/μ_r, so all resistance calculators agree on copper, aluminium, etc.

### 3d. Circuit Board Trace Impedance

The four most complex calculators. Each is **bidirectional**: a `Z0` solve plus solvers for the geometric
parameters (H/T/W, plus C for the dual case).

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula (Z0 form) | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Microstrip Trace | h, t, w [mm/mils]; ε_r | Z₀ [Ω]; C₀ [pF/len]; T_pd [ps/len] | Z₀ = 87·ln(5.98H/(0.8W+T))/√(ε_r+1.41) | **solves for Z0, H, T, W** | 1 ≤ ε_r ≤ 15; 0.1 ≤ W/H ≤ 3; H, W, T, Z > 0 | no | `emc::component::microstrip_trace` in `include/emc/component/microstrip_trace.hpp` |
| Stripline Trace | h, t, w [mm/mils]; ε_r | Z₀ [Ω]; C₀ [pF/len]; T_pd [ps/len] | Z₀ = 60·ln(4(2H+T)/(0.67π(0.8W+T)))/√ε_r | **solves for Z0, H, T, W** | 1 ≤ ε_r ≤ 15; positivity | no | `emc::component::stripline_trace` in `include/emc/component/stripline_trace.hpp` |
| Dual Stripline Trace | h, c, t, w [mm/mils]; ε_r | Z₀ [Ω]; C₀ [pF/len]; T_pd [ps/len] | Z₀ = ½[60·ln(8H/(0.67π(0.8W+T)))/√ε_r + 60·ln(8(H+C)/(0.67π(0.8W+T)))/√ε_r] | **solves for Z0, H, C, T, W** | 1 ≤ ε_r ≤ 15; positivity | no | `emc::component::dual_stripline_trace` in `include/emc/component/dual_stripline_trace.hpp` |
| Embedded Microstrip | h1, h, t, w [mm/mils]; ε_r | Z₀ [Ω]; C₀ [pF/len]; T_pd [ps/len] | Z₀ = 87·ln(5.98H/(0.8W+T))·(1−(h−H−T)/0.1)/√(ε_r+1.41) | **solves for Z0 (+ H/T/W variants)** | parameters in range; positivity | no | `emc::component::embedded_microstrip_trace` in `include/emc/component/embedded_microstrip_trace.hpp` |

> [!TIP]
> Because inputs arrive already typed (e.g. `quantity<si::milli<si::metre>>`), the mm-vs-mils conversion is a
> single implicit cast — no per-direction unit branching. Model the solve targets with one `Input` carrying
> `std::optional<quantity>` for the unknown, sharing a single `detail::` core. The ε_r and W/H ranges are
> physical bounds reported via `validate(...) -> std::expected<void, Error>`.

### 3e. Transmission Line Parameters

Seven calculators sharing the L/C/Z₀/R-per-length pattern.

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Coaxial Line | outer dia D; inner dia d; ε_r | Z₀ [Ω]; cutoff f [Hz]; C [pF/len]; L [nH/len] | Z₀ = 138·log₁₀(D/d)/√ε_r; f_c = 11.8/(√ε_r·π·(D+d)/2); C = 7.354ε_r/log₁₀(D/d); L = 140.4·log₁₀(D/d) | forward only (4 outputs) | D > d > 0 | no | `emc::component::coaxial_line` in `include/emc/component/transmission_line.hpp` |
| Microstrip Line | ε_r; width w; height h | ε_eff [-]; Z₀ [Ω] | ε_eff = (ε_r+1)/2+(ε_r−1)/2·f(h/w); Z₀ branches on W/H ≤ 1 | forward only (branch) | w, h > 0 | no | `emc::component::microstrip_line` in `include/emc/component/transmission_line.hpp` |
| Stripline | ε_r; width w; height h; thickness t | Z₀ [Ω] | Z₀ = (60/√ε_r)·ln(1.9(2h+t)/(0.8w+t)) | forward only | w, h > 0 | no | `emc::component::stripline` in `include/emc/component/transmission_line.hpp` |
| Narrow Trace Over Plane | frequency [Hz]; trace height h; width w; thickness t; σ; ε | L [H/len]; C [F/len]; Z₀ [Ω]; R [Ω/len] | δ = 1/√(πf μ₀ σ); Z₀ = √(L/C); standard PCB-trace L/C | forward only (multi-output) | f, σ > 0 | σ, ε inputs | `emc::component::narrow_trace_over_plane` in `include/emc/component/transmission_line.hpp` |
| Wide Trace Over Plane | frequency; trace height h; width w; thickness t; σ; ε | L [H/len]; C [F/len]; Z₀ [Ω]; R [Ω/len] | L_pul = μ₀μ_r·h/w; Z₀ = √(L/C); δ | forward only (multi-output) | w ≥ 5h (wide-trace regime) | σ, ε inputs | `emc::component::wide_trace_over_plane` in `include/emc/component/transmission_line.hpp` |
| Wire Over Plane | frequency; wire height h; radius a; σ | L [H/len]; C [F/len]; Z₀ [Ω]; R [Ω/len] | δ; A_eff = πa²; Z₀ = √(L/C) | forward only (multi-output) | a > 0, h > a | σ input | `emc::component::wire_over_plane` in `include/emc/component/transmission_line.hpp` |
| Wire Pair | frequency; geometry; σ; ε | L [H/len]; C [F/len]; Z₀ [Ω]; R [Ω/len] | δ; A_eff = πa²; Z₀ = √(L/C) | forward only (multi-output) | geometry > 0 | σ, ε inputs | `emc::component::wire_pair` in `include/emc/component/transmission_line.hpp` |

> [!NOTE]
> `transmission_line.hpp` groups these seven simple/medium calculators; the four bidirectional board-impedance
> solvers (§3d) each get their own header because their solve-for-X surface is large.

### 3f. Harmonic Trap

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Harmonic Trap (waveform) | harmonic n; amplitude A_m [V]; transition time t_r [s]; period T [s]; duty cycle DC [%] | fundamental f₀ [Hz]; harmonic f [Hz]; amp of harmonic A_h [V_rms]; amp of envelope A_e [V_rms] | f₀ = 1/T; A_h = 1.414·A_m·(DC/100)·\|sinc(nπDC/100)\|·\|sinc(nπt_r/T)\|; envelope breakpoints at 1/(πτ) and 1/(πt_r) | forward only (multi-output, branch) | T > 0; 0 < DC ≤ 100 | no | `emc::component::harmonic_trap` in `include/emc/component/harmonic_trap.hpp` |

`t_r` and `T` are typed `quantity<isq::time>` (s/ms/µs/ns convert implicitly); `validate` rejects `DC = 0`.

---

## 4. EMCPredictions → `emc::prediction`

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| ESD Coupling Level | loop height h [m]; radius r [m]; distance d [m]; peak current I_peak [A]; rise time t_r [ns] | induced voltage V_ind [mV] | V_ind = (μ₀h/2π)·ln((r+d)/r)·(I_peak/t_r) | forward only | r > 0; t_r > 0 | no | `emc::prediction::esd_coupling` in `include/emc/prediction/esd_coupling.hpp` |
| Lightning Coupling Level | loop height h [m]; radius r [m]; distance d [m]; current slew dI/dt [A/s] | induced voltage V_ind [V] | V_ind = (μ₀h/2π)·ln((r+d)/r)·dI/dt | forward only | r > 0 | no | `emc::prediction::lightning_coupling` in `include/emc/prediction/lightning_coupling.hpp` |
| RF E-Field (from EIRP) | transmit power P_t [dBm]; gain G_t [dBi]; distance d [m] | E-field [V/m]; H-field [A/m]; power density [W/m²] | P_t_W = 10^(P_t/10)/1000; E = √(30·P_t_W·G_t_W)/d; H = E/(120π); P_D = E·H | forward only (3 outputs) | d > 0 | no | `emc::prediction::rf_efield` in `include/emc/prediction/rf_field.hpp` |
| Friis Transmission | P_tx [W]; G_tx [dBi]; G_rx [dBi]; frequency [Hz]; range R [m] | received power P_rx [dBm] | P_rx = 30+10·log₁₀(P_tx·10^(G_tx/10)·10^(G_rx/10)·(c/(4πRf))²) | forward only | R, f > 0 | no | `emc::prediction::friis_transmission` in `include/emc/prediction/friis.hpp` |

> [!WARNING]
> ESD coupling divides by rise time, so `validate` rejects `t_r = 0`. With `t_r` typed as `quantity<isq::time>`
> the ns scaling is implicit and the formula stays unit-safe.

---

## 5. Shielding → `emc::shielding`

### 5a. Cavity Resonance

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Rectangular Enclosure | length l [m]; width w [m]; height h [m]; ε_r | resonant mode set f_mnp [Hz] (TE/TM modes) | f_mnp = (c/2√ε_r)·√((m/l)²+(n/w)²+(p/h)²) | forward only (multi-output) | l, w, h > 0 | no | `emc::shielding::rectangular_cavity_modes` in `include/emc/shielding/cavity_resonance.hpp` |
| Cylindrical Enclosure | length l [m]; radius r [m]; ε_r | TM (x_mn) and TE mode frequencies [Hz] | f = (c/2π√ε_r)·√((x_mn/r)²+(pπ/l)²) with Bessel roots 2.405/3.832/5.520/7.016/5.135/… | forward only (multi-output) | l, r > 0 | no | `emc::shielding::cylindrical_cavity_modes` in `include/emc/shielding/cavity_resonance.hpp` |
| Circuit Board Planes | length l [m]; width w [m]; thickness [m]; ε_r | mode frequencies f_mn [Hz] | f = (c/2√ε_r)·√((m/l)²+(n/w)²) with thin-plane guards | forward only (multi-output) | ε_r ≥ 1; thickness ≤ 0.1·dim | no | `emc::shielding::circuit_board_plane_modes` in `include/emc/shielding/cavity_resonance.hpp` |

The mode set is returned as a `std::array<ModeFrequency, N>` (or an `std::mdspan` indexed by (m,n,p)) rather than
a fixed list of named outputs, giving a clean, iterable result.

### 5b. EM Shielding Effectiveness

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Aperture | depth d [in]; for slot: width w; for round: diameter D | absorption loss AL [dB] | slot: AL = 27.3·d/w; round: AL = 32·d/D | forward only (shape branch) | w or D > 0 | no | `emc::shielding::aperture_loss` in `include/emc/shielding/aperture.hpp` |
| Near-Field SE | material (Cu/Al/Au/Other) → σ, μ_r; thickness t; distance r; frequency [Hz]; field type (E/H) | absorption AL [dB]; reflection RL [dB]; total SE [dB] | δ = 1/√(π·f·μ₀μ_r·σ); AL = 8.7·t/δ; Z_w (E or H); RL = 20·log₁₀(Z_w/(4·N_s)) | forward only (E/H branch) | f, t, r > 0 | **yes** | `emc::shielding::near_field_se` in `include/emc/shielding/shielding_effectiveness.hpp` |
| Plane-Wave SE | material → σ, μ_r; thickness t; frequency [Hz] | absorption AL [dB]; reflection RL [dB]; total SE [dB] | δ; AL = 8.7·t/δ; RL = 20·log₁₀(377/(4·N_s)) | forward only | f, t > 0 | **yes** | `emc::shielding::plane_wave_se` in `include/emc/shielding/shielding_effectiveness.hpp` |
| Slot (λ/2 resonance) | frequency [Hz]; slot length [m] | wavelength λ [m]; SE [dB] | λ = c/f; SE = 20·log₁₀(λ/(2·length)) | forward only | f, length > 0 | no | `emc::shielding::slot_se` in `include/emc/shielding/slot.hpp` |

> [!IMPORTANT]
> Near-field and plane-wave SE share the same skin-depth and barrier-impedance physics, so both draw π and the
> material σ/μ_r from the single `emc::constants` / `emc::materials` source, keeping the two numerically
> consistent.

---

## 6. Filtering → `emc::filtering`

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Ferrite Toroid Impedance | turns N; μ_r′ (real); μ_r″ (imag); height h [m]; inner a [m]; outer b [m]; frequency [Hz] | L [H]; reactance X [Ω]; resistance R [Ω]; impedance \|Z\| [Ω] | L = (N²μ₀h/2π)·ln(b/a); X = 2πf·μ_r′·L; R = 2πf·μ_r″·L; Z = √(R²+X²) | forward only (4 outputs) | a > 0, b > a | complex μ_r input | `emc::filtering::ferrite_toroid` in `include/emc/filtering/ferrite_toroid.hpp` |

---

## 7. Cabling → `emc::cabling`

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Cable Braid Optical Coverage | strand dia d [m]; braid OD D [m]; picks P [/len]; carriers C; ends N | optical coverage OC [%] | θ = atan(2π(D+2d)P/C); F = (P·N·d)/sin θ; OC = 100·(2F−F²) | forward only | C > 0; sin θ ≠ 0 | no | `emc::cabling::braid_optical_coverage` in `include/emc/cabling/braid_coverage.hpp` |
| Crosstalk (NEXT/FEXT) | frequency [Hz]; R_L; R_S; R_NE; R_FE [Ω]; mutual L_m [H]; mutual C_m [F] | near-end V_NE [dB]; far-end V_FE [dB] | V_NE = 20·log₁₀(2πf·[(R_NE/(R_NE+R_FE))·L_m/(R_S+R_L)+(R_NE·R_FE/(R_NE+R_FE))·R_L·C_m/(R_S+R_L)]); V_FE analogous | forward only (2 outputs) | resistances > 0 | no | `emc::cabling::crosstalk` in `include/emc/cabling/crosstalk.hpp` |

---

## 8. Grounding → `emc::grounding`

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Microstrip Line Current Distribution | source current I₀ [A]; trace width w [m]; height h [m]; lateral position x [m] | ground-plane current density J [A/m] | J = (I₀/(πw))·1/(1+(x/h)²) | forward only | w, h > 0 | no | `emc::grounding::microstrip_current_distribution` in `include/emc/grounding/microstrip_current.hpp` |

---

## 9. Testing → `emc::testing`

| Calculator | Inputs (with units) | Output(s) (with units) | Core formula | Solve directions | Validity constraints | Material? | Library location |
|---|---|---|---|---|---|:--:|---|
| Noise Figure of an RF Receiver | per-stage NF [dB] and gain [dB] (N stages) | cascade noise figure [dB]; total gain [dB] | Friis cascade: F = F₁+(F₂−1)/G₁+(F₃−1)/(G₁G₂)…; NF = 10·log₁₀(F); G_total = ΣG_i (dB) | forward only (N-stage) | ≥ 1 stage | no | `emc::testing::noise_figure` in `include/emc/testing/noise_figure.hpp` |

The cascade is naturally variable-length, so the input is a `std::span<const Stage>` (each `{ nf_dB, gain_dB }`),
generalizing the receiver to N stages with no fixed-arity API.

---

## Summary

| Category | Calculators |
|---|---:|
| BasicCalculations | 5 |
| Converter | 5 |
| ComponentCalculations | 25 |
| EMCPredictions | 4 |
| Shielding | 7 |
| Filtering | 1 |
| Cabling | 2 |
| Grounding | 1 |
| Testing | 1 |
| **TOTAL** | **51** |

> [!NOTE]
> Tests derive expected values from hand computation and textbook closed-form examples, plus round-trip checks
> on bidirectional converters/solvers, monotonicity/property checks, edge-case domain rejection, and `constexpr`
> evaluation where the formula is constant-foldable. See `09-testing-and-golden-vectors.md`.

## Cross-references

`01-architecture-and-layout.md` (layout), `03-quantities-and-units-mp-units.md` (typed quantities),
`04-constants-and-material-database.md` (constants/materials), `05-error-handling-and-validation.md`
(`expected`/`validate`), `06-calculator-design-pattern.md` (Input/Result/calculate/validate pattern),
`09-testing-and-golden-vectors.md` (reference vectors).
