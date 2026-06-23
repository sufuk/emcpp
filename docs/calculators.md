# Calculator catalog

60+ calculators across 9 EMC domains. Headers live under `include/emc/<domain>/`. Full signatures are in the [API reference](api/index.html).

## `basic` — fundamentals
| Calculator | What it computes |
|---|---|
| Skin depth | `δ = √(1/(π·f·μ₀·μ_r·σ))` from frequency + material (or custom σ, μ_r). |
| Decibel / level conversions | dB ↔ voltage gain ↔ power gain; dBm ↔ power ↔ RMS/peak voltage across a load. |
| Dipole near-field | Short-dipole `E_r`, `E_θ` [V/m] and `H_φ` [A/m]. |
| Loop near-field | Small-loop `H_r`, `H_θ` [A/m] and `E_φ` [V/m]. |
| Far-field criteria | Wavelength + reactive/radiating near-field boundaries. |

## `converter`
| Calculator | What it computes |
|---|---|
| Antenna factor ↔ gain | Antenna factor [dB/m] at frequency → realized gain [dBi]. |
| E-field → power density | `P_D = E²/η` [W/m²]. |
| Energy ↔ frequency | Planck relation `E = h·f`, plus eV ↔ joule. |
| VSWR mismatch | Γ, return loss, mismatch loss, insertion loss from one VSWR. |
| Wavelength ↔ frequency | `f = c/λ` and `λ = c/f`. |

## `component` — lumped parts & transmission lines
| Calculator | What it computes |
|---|---|
| Capacitance | Parallel-plate and isolated-sphere. |
| Inductance | Circular/rectangular/square loop, solenoid, toroid, via, connector pin. |
| Resistance | AC/DC resistance + skin depth of traces, round/rectangular conductors, AWG wires. |
| Harmonic trap | Trapezoidal pulse-train spectrum (fundamental, harmonic line, envelope). |
| Transmission lines | Z₀ + per-length L/C of coax, microstrip, stripline, traces/wires over a plane, wire pairs. |
| Controlled-impedance traces | Microstrip / embedded microstrip / stripline / dual-stripline (Z₀, C₀, delay) — **forward + inverse**. |

## `cabling`
| Calculator | What it computes |
|---|---|
| Braid optical coverage | Shield coverage, weave angle, fill factor from braid geometry. |
| Crosstalk | Near-end / far-end coupled voltages [dB] from terminations and mutual L/C. |

## `shielding`
| Calculator | What it computes |
|---|---|
| Aperture absorption | Below-cutoff waveguide loss [dB] of a slot or round hole. |
| Slot SE | λ/2-resonant slot shielding effectiveness. |
| Shielding effectiveness | Near-field (E/H) and plane-wave absorption + reflection + total SE [dB]. |
| Cavity resonance | Rectangular, cylindrical, and circuit-board plane-pair resonant modes. |

## `prediction`
| Calculator | What it computes |
|---|---|
| ESD coupling | Loop voltage induced by an ESD current ramp. |
| Friis link budget | Received power [dBm] over a free-space link. |
| Lightning coupling | Loop voltage induced by a lightning slew rate. |
| RF far-field | Far-field `E`, `H`, power density from EIRP. |

## `grounding` · `filtering` · `testing`
| Calculator | What it computes |
|---|---|
| Microstrip return current | Lateral ground-plane current density beneath a trace. |
| Ferrite toroid impedance | Inductance, reactance, lossy resistance, \|Z\| from complex permeability. |
| Cascade noise figure | Overall receiver-chain NF [dB] and total gain (Friis cascade). |
