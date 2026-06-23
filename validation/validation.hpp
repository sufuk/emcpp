// validation/validation.hpp
//
// Plain data model shared by the reference TEST (a CI gate) and the HTML REPORT
// (a human-facing dashboard). It has no dependency on Catch2 or on emc — the
// per-calculator runners in validators.cpp fill these structs by calling emc and
// comparing against the Excel-derived reference vectors in tests/reference/*.csv.
#pragma once

#include <algorithm>   // std::max
#include <cmath>       // std::abs
#include <cstddef>
#include <string>
#include <vector>

namespace emc::validation {

// One scalar output of a calculator, compared in a single display unit.
struct Comparison {
    std::string quantity;          // e.g. "Z0"
    std::string unit;              // e.g. "ohm"
    double      computed = 0.0;    // emc's value, expressed in `unit`
    double      expected = 0.0;    // Excel ground truth, expressed in `unit`

    // Relative error, with an absolute fallback when the expected value is ~0
    // (so a near-zero reference does not blow the ratio up to infinity).
    [[nodiscard]] double rel_error() const {
        const double diff  = std::abs(computed - expected);
        const double denom = std::abs(expected);
        return denom > 1e-300 ? diff / denom : diff;
    }
    // Pass test: small ABSOLUTE difference (for values near zero, where relative
    // error explodes) OR small relative error.
    [[nodiscard]] bool within(double rel_tol, double abs_floor) const {
        return std::abs(computed - expected) <= abs_floor || rel_error() <= rel_tol;
    }
    // Like rel_error, but the denominator is floored at abs_floor so a near-zero
    // expected value cannot inflate it. Used for the headline / summary metrics.
    [[nodiscard]] double display_error(double abs_floor) const {
        // Within the absolute floor (ground-truth rounding / a known convention gap)
        // the values are identical at the available precision -> report exact (0).
        if (std::abs(computed - expected) <= abs_floor) return 0.0;
        return rel_error();
    }
};

// One reference row: the inputs (as a human-readable label) and every output
// compared for that row.
struct Case {
    std::string inputs;                  // e.g. "VSWR=2"
    std::vector<Comparison> outputs;
};

// Everything known about one calculator's validation against its Excel sheet.
struct CalcReport {
    std::string name;        // "Coaxial Line"
    std::string domain;      // "component"
    std::string excel_file;  // "CoaxialLineWidget.xlsx" (provenance)
    std::string csv_file;    // "coaxial_line.csv" (the repo reference file)
    std::string formula;     // human-readable formula(s)
    std::string note;        // optional caveat (e.g. constants difference)
    double      tolerance = 1e-6;   // pass gate: relative error must be <= this
    double      abs_floor = 0.0;    // ...unless the absolute difference is <= this (near-zero values)
    std::vector<Case> cases;

    [[nodiscard]] double max_error() const {
        double m = 0.0;
        for (const auto& c : cases)
            for (const auto& o : c.outputs) m = std::max(m, o.rel_error());
        return m;
    }
    [[nodiscard]] double avg_error() const {
        double sum = 0.0;
        std::size_t n = 0;
        for (const auto& c : cases)
            for (const auto& o : c.outputs) { sum += o.rel_error(); ++n; }
        return n ? sum / static_cast<double>(n) : 0.0;
    }
    [[nodiscard]] double display_max() const {
        double m = 0.0;
        for (const auto& c : cases)
            for (const auto& o : c.outputs) m = std::max(m, o.display_error(abs_floor));
        return m;
    }
    [[nodiscard]] double display_avg() const {
        double sum = 0.0; std::size_t n = 0;
        for (const auto& c : cases)
            for (const auto& o : c.outputs) { sum += o.display_error(abs_floor); ++n; }
        return n ? sum / static_cast<double>(n) : 0.0;
    }
    [[nodiscard]] std::size_t comparison_count() const {
        std::size_t n = 0;
        for (const auto& c : cases) n += c.outputs.size();
        return n;
    }
    [[nodiscard]] bool passed() const {
        for (const auto& c : cases)
            for (const auto& o : c.outputs)
                if (!o.within(tolerance, abs_floor)) return false;
        return true;
    }
};

} // namespace emc::validation
