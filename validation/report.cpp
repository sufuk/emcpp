// validation/report.cpp
//
// Generates a self-contained HTML dashboard that shows, per calculator, how emc's
// output compares against the trusted Excel reference vectors (max / average
// relative error, pass/fail, and every individual case). This is the modern,
// reproducible equivalent of the original spreadsheets' error columns.
//
//   usage: emc_validation_report [output.html]   (default: validation.html)
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "validators.hpp"

namespace {

std::string esc(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            default:  o += ch;
        }
    }
    return o;
}

std::string num(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.6g", v);
    return std::string{b};
}

std::string sci(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.2e", v);
    return std::string{b};
}

} // namespace

int main(int argc, char** argv) {
    const std::string out_path = (argc > 1) ? argv[1] : "validation.html";
    const auto reports = emc::validation::all_reports();

    std::size_t total_cmp = 0;
    double overall_max = 0.0;
    bool all_pass = true;
    for (const auto& r : reports) {
        total_cmp += r.comparison_count();
        if (r.max_error() > overall_max) overall_max = r.max_error();
        if (!r.passed()) all_pass = false;
    }

    std::ofstream o{out_path};
    if (!o) { std::cerr << "cannot write " << out_path << "\n"; return 1; }

    o << R"(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>emc - validation against Excel reference</title>
<style>
:root{--bg:#0f1419;--card:#1a2029;--ink:#e6e9ef;--mut:#8a93a3;--line:#2b3340;
--ok:#2ecc71;--okbg:#10301f;--bad:#ff5d5d;--badbg:#3a1414;--accent:#4aa3ff;}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
font:14px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:22px 26px;border-bottom:1px solid var(--line);
background:linear-gradient(180deg,#161b22,#0f1419)}
h1{margin:0 0 4px;font-size:20px}
.sub{color:var(--mut);font-size:13px}
.wrap{max-width:1100px;margin:0 auto;padding:22px 26px}
.cards{display:flex;gap:14px;flex-wrap:wrap;margin:0 0 20px}
.kpi{background:var(--card);border:1px solid var(--line);border-radius:10px;
padding:14px 18px;min-width:150px}
.kpi .v{font-size:22px;font-weight:600}.kpi .l{color:var(--mut);font-size:12px}
table{width:100%;border-collapse:collapse;background:var(--card);
border:1px solid var(--line);border-radius:10px;overflow:hidden}
th,td{padding:8px 10px;text-align:left;border-bottom:1px solid var(--line);font-size:13px}
th{color:var(--mut);font-weight:600;background:#141a22}
td.n,th.n{text-align:right;font-variant-numeric:tabular-nums;
font-family:ui-monospace,SFMono-Regular,Consolas,monospace}
tr:last-child td{border-bottom:none}
.pill{display:inline-block;padding:2px 9px;border-radius:999px;font-size:12px;font-weight:600}
.pill.ok{color:var(--ok);background:var(--okbg)}
.pill.bad{color:var(--bad);background:var(--badbg)}
.err-ok{color:var(--ok)}.err-bad{color:var(--bad)}
details{background:var(--card);border:1px solid var(--line);border-radius:10px;
margin:12px 0;overflow:hidden}
summary{cursor:pointer;padding:12px 16px;font-weight:600;display:flex;
align-items:center;gap:12px;list-style:none}
summary::-webkit-details-marker{display:none}
summary:hover{background:#141a22}
.meta{color:var(--mut);font-size:12px;font-weight:400}
.formula{font-family:ui-monospace,Consolas,monospace;font-size:12px;color:#b9c2d0;
padding:8px 16px;border-bottom:1px solid var(--line);background:#11161d;word-break:break-word}
.note{color:#ffcf7a;font-size:12px;padding:6px 16px;background:#241d10;border-bottom:1px solid var(--line)}
.search{margin:0 0 14px}
.search input{width:100%;padding:9px 12px;border-radius:8px;border:1px solid var(--line);
background:var(--card);color:var(--ink);font-size:14px}
a{color:var(--accent)}
</style></head><body>
<header><h1>emc &mdash; numerical validation against Excel reference</h1>
<div class="sub">Each calculator is run over the input vectors from the trusted spreadsheets; emc's output is compared to the spreadsheet's formula result (ground truth). Pass = max relative error within the per-calculator tolerance.</div>
</header><div class="wrap">
)";

    // KPI cards
    o << "<div class=\"cards\">";
    o << "<div class=\"kpi\"><div class=\"v\">" << reports.size() << "</div><div class=\"l\">calculators</div></div>";
    o << "<div class=\"kpi\"><div class=\"v\">" << total_cmp << "</div><div class=\"l\">value comparisons</div></div>";
    o << "<div class=\"kpi\"><div class=\"v\">" << sci(overall_max) << "</div><div class=\"l\">worst relative error</div></div>";
    o << "<div class=\"kpi\"><div class=\"v\">"
      << (all_pass ? "<span class=\"pill ok\">ALL PASS</span>" : "<span class=\"pill bad\">FAIL</span>")
      << "</div><div class=\"l\">overall</div></div>";
    o << "</div>";

    // Summary table
    o << "<table><thead><tr><th>Calculator</th><th>Domain</th><th>Excel sheet</th>"
         "<th class=\"n\">cases</th><th class=\"n\">comparisons</th><th class=\"n\">max err</th>"
         "<th class=\"n\">avg err</th><th class=\"n\">tol</th><th>status</th></tr></thead><tbody>";
    for (const auto& r : reports) {
        o << "<tr><td>" << esc(r.name) << "</td><td>" << esc(r.domain) << "</td><td>"
          << esc(r.excel_file) << "</td><td class=\"n\">" << r.cases.size()
          << "</td><td class=\"n\">" << r.comparison_count()
          << "</td><td class=\"n\">" << sci(r.max_error())
          << "</td><td class=\"n\">" << sci(r.avg_error())
          << "</td><td class=\"n\">" << sci(r.tolerance) << "</td><td>"
          << (r.passed() ? "<span class=\"pill ok\">PASS</span>" : "<span class=\"pill bad\">FAIL</span>")
          << "</td></tr>";
    }
    o << "</tbody></table>";

    // Search + per-calculator detail
    o << "<h2 style=\"margin:26px 0 10px;font-size:16px\">Per-calculator detail</h2>";
    o << "<div class=\"search\"><input id=\"q\" placeholder=\"filter calculators...\" "
         "oninput=\"var v=this.value.toLowerCase();document.querySelectorAll('details').forEach(function(d){"
         "d.style.display=d.dataset.k.indexOf(v)>=0?'':'none';});\"></div>";

    for (const auto& r : reports) {
        const std::string key = r.name + " " + r.domain + " " + r.excel_file;
        std::string keyl;
        for (char ch : key) keyl += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        o << "<details data-k=\"" << esc(keyl) << "\"><summary>"
          << (r.passed() ? "<span class=\"pill ok\">PASS</span>" : "<span class=\"pill bad\">FAIL</span>")
          << "<span>" << esc(r.name) << "</span>"
          << "<span class=\"meta\">" << esc(r.excel_file) << " &middot; " << r.comparison_count()
          << " comparisons &middot; max " << sci(r.max_error()) << "</span></summary>";
        o << "<div class=\"formula\">" << esc(r.formula) << "</div>";
        if (!r.note.empty()) o << "<div class=\"note\">" << esc(r.note) << "</div>";
        o << "<table><thead><tr><th>Inputs</th><th>Quantity</th><th class=\"n\">emc</th>"
             "<th class=\"n\">Excel</th><th class=\"n\">unit</th><th class=\"n\">rel error</th></tr></thead><tbody>";
        for (const auto& c : r.cases) {
            bool first = true;
            for (const auto& cmp : c.outputs) {
                const double e = cmp.rel_error();
                const bool ok = cmp.within(r.tolerance, r.abs_floor);
                o << "<tr><td>" << (first ? esc(c.inputs) : std::string{}) << "</td><td>"
                  << esc(cmp.quantity) << "</td><td class=\"n\">" << num(cmp.computed)
                  << "</td><td class=\"n\">" << num(cmp.expected) << "</td><td class=\"n\">"
                  << esc(cmp.unit) << "</td><td class=\"n " << (ok ? "err-ok" : "err-bad") << "\">"
                  << sci(e) << "</td></tr>";
                first = false;
            }
        }
        o << "</tbody></table></details>";
    }

    o << "</div></body></html>\n";
    o.close();

    std::cout << "wrote " << out_path << ": " << reports.size() << " calculators, "
              << total_cmp << " comparisons, worst rel error " << sci(overall_max)
              << (all_pass ? " (ALL PASS)\n" : " (FAIL)\n");
    return all_pass ? 0 : 2;
}
