// tests/validation/reference_test.cpp
//
// CI gate: every Excel-derived reference vector must match emc's output within
// the calculator's tolerance. It drives the SAME emc::validation::all_reports()
// that the HTML dashboard (validation/report.cpp) uses, so the gate and the
// human-facing report can never disagree.
#include <catch2/catch_test_macros.hpp>

#include "validators.hpp"

TEST_CASE("reference vectors match the Excel ground truth", "[reference][excel]") {
    const auto reports = emc::validation::all_reports();
    REQUIRE_FALSE(reports.empty());

    for (const auto& rep : reports) {
        INFO("calculator: " << rep.name << " (" << rep.excel_file << "), tol=" << rep.tolerance);
        CHECK(rep.comparison_count() > 0);   // the reference CSV actually loaded

        for (const auto& c : rep.cases) {
            for (const auto& cmp : c.outputs) {
                INFO(rep.name << "  [" << c.inputs << "]  " << cmp.quantity
                     << ": emc=" << cmp.computed << "  excel=" << cmp.expected
                     << " " << cmp.unit << "  rel_err=" << cmp.rel_error()
                     << "  tol=" << rep.tolerance);
                CHECK(cmp.within(rep.tolerance, rep.abs_floor));
            }
        }
    }
}
