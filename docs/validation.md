# Validation

Every emcpp calculator is validated against **trusted reference spreadsheets** — one Excel "widget" per formula, each carrying ~100 hand-picked test points computed by the spreadsheet's own formula (the ground truth).

[:material-television-guide: **Open the live validation dashboard**](dashboard.html){ .md-button .md-button--primary }

## How it works

```
Excel sheets          tests/reference/*.csv        validation/ (one shared core)
(Expected =     ──►   input + expected,       ──►  run each row through emc,
 ground truth)        SI units, ~100 rows           record emc vs Excel
                                                          │
                                          ┌───────────────┴───────────────┐
                                          ▼                               ▼
                                  Catch2 test (CI gate)          report → validation.html
                                  error ≤ tolerance               (the dashboard)
```

1. **Extract** — each sheet's *Expected Results* (formula) columns and inputs are extracted to an SI-unit CSV under `tests/reference/` (the spreadsheets themselves are not in the repo).
2. **Run** — `validation/validators.cpp` feeds every row to the real `emc` library and records `{inputs, emc value, Excel value, relative error}`.
3. **Two consumers, one core** — the same `all_reports()` drives both the Catch2 **CI gate** (`tests/validation/reference_test.cpp`) and the HTML **dashboard** (`validation/report.cpp`), so they can never drift apart.

## Coverage

**49 calculators · 10,080 value comparisons · all within tolerance.** Material-lookup cases feed `Material::Custom` so the *formula* is tested independently of the material table. Most calculators agree to 3–10 significant figures; the residuals are explained per calculator in the dashboard.

!!! info "emcpp uses exact constants"
    emcpp uses the exact 2019-SI constants (`c = 299 792 458 m/s`, exact `π`); the spreadsheets use `c ≈ 3×10⁸` and `π = 3.14`. For `c`/`π`-dependent outputs this produces a ~0.07% systematic offset — emcpp is the *more* accurate one. Tolerances absorb it and the dashboard surfaces it.

## Findings in the "trusted" references

The harness is honest enough to flag issues in the references themselves (emcpp is correct in each case):

- **Narrow trace** R/length — the sheet uses the mm unit factor for the conducting area while the geometry is in cm, ~10× too large (R excluded; L/C/Z₀ validated).
- **Circuit-board planes** "dominant" — the sheet labels f10 as dominant, but the true minimum is f01 when *W > L* (re-derived as the minimum over all modes).
- **Antenna factor ↔ gain** — a ~0.4 dB constant-convention difference.
- **Loop antenna** — the sheet `ROUND`s every output to 5 decimals, so at H ≈ 1e-5 A/m only ~1 significant figure survives (compared absolutely there).

## Skipped (5, no recoverable ground truth)

Embedded Microstrip and Harmonic Trap (Excel cells are uncached `Loading…` UDFs); the I/O-Coupling, Powerbus and Differential-mode **EMI** sheets (dBµV/m radiated-emission models with no emc counterpart).
