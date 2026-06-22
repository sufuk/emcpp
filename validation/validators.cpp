// validation/validators.cpp
//
// Per-calculator runners: load a reference CSV (extracted from the trusted Excel
// sheets), feed each row to the matching emc calculator, and record emc's output
// next to the Excel ground truth. EMC_REFERENCE_DIR is injected by CMake and
// points at the source-tree tests/reference directory.
#include "validators.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

#include "support/csv.hpp"   // emc::test::load_csv, Row

#include <emc/basic/decibel.hpp>
#include <emc/basic/skin_depth.hpp>
#include <emc/component/transmission_line.hpp>
#include <emc/converter/vswr.hpp>

#include <mp-units/systems/si.h>

namespace {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, m, ohm, S, V, W, F, H, ...
using emc::test::load_csv;
using emc::test::Row;

std::filesystem::path ref(const char* file) {
    return std::filesystem::path{EMC_REFERENCE_DIR} / file;
}

// Compact label for an input value (4 significant figures).
std::string g4(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4g", v);
    return std::string{buf};
}

// ---- Skin depth ----------------------------------------------------------
// f_Hz, mu_r, sigma_Spm -> expected delta [m]. Fed via Material::Custom so the
// comparison tests the FORMULA, independent of emc's material table values.
emc::validation::CalcReport skin_depth() {
    emc::validation::CalcReport rep;
    rep.name = "Skin Depth";
    rep.domain = "basic";
    rep.excel_file = "SkinDepth.xlsx";
    rep.formula = "delta = sqrt(1 / (pi * f * mu0 * mu_r * sigma))";
    rep.tolerance = 1e-6;
    for (const Row& r : load_csv(ref("skin_depth.csv"))) {
        const double f = r.num(0), mu_r = r.num(1), sigma = r.num(2), exp = r.num(3);
        const auto out = emc::basic::calculate(emc::basic::SkinDepthInput{
            .frequency = f * Hz,
            .material = emc::materials::Material::Custom,
            .conductivity = sigma * (S / m),
            .relative_permeability = mu_r });
        if (!out) continue;
        emc::validation::Case c;
        c.inputs = "f=" + g4(f) + " Hz, mu_r=" + g4(mu_r) + ", sigma=" + g4(sigma) + " S/m";
        c.outputs.push_back({ "skin depth", "m", out->skin_depth.numerical_value_in(m), exp });
        rep.cases.push_back(std::move(c));
    }
    return rep;
}

// ---- VSWR (4 outputs) ----------------------------------------------------
emc::validation::CalcReport vswr() {
    emc::validation::CalcReport rep;
    rep.name = "VSWR mismatch figures";
    rep.domain = "converter";
    rep.excel_file = "VSWR_RC_RL_ML_TL.xlsx";
    rep.formula = "Gamma=(v-1)/(v+1); RL=-20log10(Gamma); ML=-10log10(1-Gamma^2); IL=-10log10((1+Gamma)^2)";
    rep.tolerance = 1e-6;
    for (const Row& r : load_csv(ref("vswr.csv"))) {
        const double v = r.num(0), rc = r.num(1), rl = r.num(2), ml = r.num(3), il = r.num(4);
        const auto out = emc::converter::calculate(emc::converter::VswrInput{ .vswr = v * one });
        if (!out) continue;
        emc::validation::Case c;
        c.inputs = "VSWR=" + g4(v);
        c.outputs.push_back({ "reflection coeff", "-", out->reflection_coefficient.numerical_value_in(one), rc });
        c.outputs.push_back({ "return loss", "dB", out->return_loss.value, rl });
        c.outputs.push_back({ "mismatch loss", "dB", out->mismatch_loss.value, ml });
        c.outputs.push_back({ "insertion loss", "dB", out->insertion_loss.value, il });
        rep.cases.push_back(std::move(c));
    }
    return rep;
}

// ---- Coaxial line (4 outputs) -------------------------------------------
emc::validation::CalcReport coaxial() {
    emc::validation::CalcReport rep;
    rep.name = "Coaxial Line";
    rep.domain = "component";
    rep.excel_file = "CoaxialLineWidget.xlsx";
    rep.formula = "Z0=138 log10(D/d)/sqrt(eps); fc=11.8/(sqrt(eps) pi (D+d)/2); C=7.354 eps/log10(D/d); L=140.4 log10(D/d)";
    rep.tolerance = 1e-4;   // cutoff folds in c; the empirical 11.8/etc. constants set the floor
    for (const Row& r : load_csv(ref("coaxial_line.csv"))) {
        const double D = r.num(0), d = r.num(1), eps = r.num(2);
        const double eZ = r.num(3), efc = r.num(4), eC = r.num(5), eL = r.num(6);
        const auto out = emc::component::calculate(emc::component::CoaxialLineInput{
            .outer_diameter = D * m, .inner_diameter = d * m, .relative_permittivity = eps });
        if (!out) continue;
        emc::validation::Case c;
        c.inputs = "D=" + g4(D) + " m, d=" + g4(d) + " m, eps_r=" + g4(eps);
        c.outputs.push_back({ "Z0", "ohm", out->impedance.numerical_value_in(ohm), eZ });
        c.outputs.push_back({ "cutoff freq", "Hz", out->cutoff_frequency.numerical_value_in(Hz), efc });
        c.outputs.push_back({ "C per length", "F/m", out->capacitance.numerical_value_in(F / m), eC });
        c.outputs.push_back({ "L per length", "H/m", out->inductance.numerical_value_in(H / m), eL });
        rep.cases.push_back(std::move(c));
    }
    return rep;
}

// ---- Decibel calculator (two panels, 5 outputs) -------------------------
emc::validation::CalcReport decibel() {
    emc::validation::CalcReport rep;
    rep.name = "Decibel Calculator";
    rep.domain = "basic";
    rep.excel_file = "DecibelCalculatorWidget.xlsx";
    rep.formula = "Vgain=10^(dB/20); Pgain=10^(dB/10); P=10^((dBm-30)/10); Vrms=sqrt(P*R); Vpeak=Vrms*sqrt(2)";
    rep.tolerance = 1e-6;
    for (const Row& r : load_csv(ref("decibel.csv"))) {
        const double db = r.num(0), load = r.num(1), dbm = r.num(2);
        const double eVg = r.num(3), ePg = r.num(4), eP = r.num(5), eVr = r.num(6), eVp = r.num(7);
        const auto gain  = emc::basic::gain_from_db(emc::units::Decibel{db});
        const auto level = emc::basic::level_from_dbm(emc::units::Dbm{dbm}, load * ohm);
        if (!gain || !level) continue;
        emc::validation::Case c;
        c.inputs = "dB=" + g4(db) + ", dBm=" + g4(dbm) + ", R=" + g4(load) + " ohm";
        c.outputs.push_back({ "voltage gain", "-", gain->voltage_gain, eVg });
        c.outputs.push_back({ "power gain", "-", gain->power_gain, ePg });
        c.outputs.push_back({ "power", "W", level->power.numerical_value_in(W), eP });
        c.outputs.push_back({ "Vrms", "V", level->voltage.numerical_value_in(V), eVr });
        c.outputs.push_back({ "Vpeak", "V", level->peak.numerical_value_in(V), eVp });
        rep.cases.push_back(std::move(c));
    }
    return rep;
}

} // namespace

namespace emc::validation {

std::vector<CalcReport> all_reports() {
    return { skin_depth(), vswr(), coaxial(), decibel() };
}

} // namespace emc::validation
