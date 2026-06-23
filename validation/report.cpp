// validation/report.cpp
//
// Generates a self-contained, single-file HTML dashboard for the Excel-reference
// validation: a left sidebar lists every calculator (grouped by domain, with a
// pass/fail dot and a search box); the main panel shows the selected calculator's
// formula, its repo reference CSV, and the full input -> emc -> Excel comparison
// table. The "worst error" metric is floored at abs_floor so a near-zero expected
// value (e.g. a field/gain crossing zero) cannot inflate it.
//
//   usage: emc_validation_report [output.html]   (default: validation.html)
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

std::string lower(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char ch : s) o += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
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

std::string pct(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.3g", v * 100.0);
    return std::string{b} + "%";
}

// Error display: scientific form plus a percentage, e.g. "6.31e-02 (6.31%)".
std::string ep(double v) {
    return sci(v) + " (" + pct(v) + ")";
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
        if (r.display_max() > overall_max) overall_max = r.display_max();
        if (!r.passed()) all_pass = false;
    }

    // Domains in display order.
    const std::vector<std::string> domains = {
        "basic", "converter", "component", "cabling", "grounding",
        "filtering", "prediction", "shielding", "testing"
    };

    std::ofstream o{out_path};
    if (!o) { std::cerr << "cannot write " << out_path << "\n"; return 1; }

    o << R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>emc - validation against Excel reference</title>
<style>
:root{--bg:#0f1419;--card:#161c25;--card2:#1b222d;--ink:#e6e9ef;--mut:#8a93a3;
--line:#2b3340;--ok:#2ecc71;--okbg:#10301f;--bad:#ff5d5d;--badbg:#3a1414;--accent:#4aa3ff;}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
font:14px/1.55 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:16px 24px;border-bottom:1px solid var(--line);display:flex;
align-items:center;gap:18px;flex-wrap:wrap;position:sticky;top:0;background:var(--bg);z-index:5}
header h1{margin:0;font-size:17px;font-weight:650}
.chips{display:flex;gap:10px;flex-wrap:wrap;margin-left:auto}
.chip{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:5px 11px;font-size:12px}
.chip b{font-size:14px}
.pill{display:inline-block;padding:2px 9px;border-radius:999px;font-size:12px;font-weight:600}
.pill.ok{color:var(--ok);background:var(--okbg)}.pill.bad{color:var(--bad);background:var(--badbg)}
.app{display:flex;align-items:flex-start}
.side{width:262px;flex:none;position:sticky;top:54px;height:calc(100vh - 54px);
overflow:auto;border-right:1px solid var(--line);padding:12px 10px}
.side input{width:100%;padding:8px 10px;border-radius:8px;border:1px solid var(--line);
background:var(--card);color:var(--ink);font-size:13px;margin-bottom:8px}
.side .grp{color:var(--mut);font-size:11px;text-transform:uppercase;letter-spacing:.04em;
font-weight:700;padding:10px 8px 4px}
.side a{display:flex;align-items:center;gap:8px;padding:6px 8px;border-radius:7px;
color:var(--ink);text-decoration:none;font-size:13px;cursor:pointer}
.side a:hover{background:var(--card)}
.side a.active{background:var(--card2);color:#fff;font-weight:600}
.dot{width:8px;height:8px;border-radius:50%;flex:none;background:var(--ok)}
.dot.bad{background:var(--bad)}
main{flex:1;min-width:0;padding:22px 26px;max-width:940px}
h2{font-size:16px;margin:0 0 12px}
.dim{color:var(--mut);font-size:13px}
table{width:100%;border-collapse:collapse;background:var(--card);
border:1px solid var(--line);border-radius:10px;overflow:hidden;margin:6px 0 18px}
th,td{padding:8px 11px;text-align:left;border-bottom:1px solid var(--line);font-size:13px}
th{color:var(--mut);font-weight:600;background:#11161d}
td.n,th.n{text-align:right;font-variant-numeric:tabular-nums;
font-family:ui-monospace,SFMono-Regular,Consolas,monospace}
tr:last-child td{border-bottom:none}
tbody tr.clk{cursor:pointer}tbody tr.clk:hover{background:var(--card2)}
.err-ok{color:var(--ok)}.err-bad{color:var(--bad)}
.calc-head{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin:0 0 6px}
.calc-head h2{margin:0}
.tag{font-size:11px;color:var(--mut);border:1px solid var(--line);border-radius:6px;padding:2px 7px}
.meta{color:var(--mut);font-size:12.5px;margin:2px 0 14px}
.meta code{background:var(--card);border:1px solid var(--line);border-radius:5px;padding:1px 6px;color:#cdd6e2}
.formula{font-family:ui-monospace,Consolas,monospace;font-size:12.5px;color:#bcc6d4;
background:var(--card);border:1px solid var(--line);border-radius:9px;padding:10px 13px;
margin:0 0 12px;word-break:break-word}
.note{color:#ffcf7a;font-size:12.5px;background:#241d10;border:1px solid #3a2f14;
border-radius:9px;padding:9px 13px;margin:0 0 14px}
a.lnk{color:var(--accent)}
</style></head><body>
)HTML";

    // ---- header ----
    o << "<header><h1>emc &mdash; validation vs Excel reference</h1><div class=\"chips\">";
    o << "<div class=\"chip\"><b>" << reports.size() << "</b> calculators</div>";
    o << "<div class=\"chip\"><b>" << total_cmp << "</b> comparisons</div>";
    o << "<div class=\"chip\">worst error <b>" << ep(overall_max) << "</b></div>";
    o << "<div class=\"chip\">"
      << (all_pass ? "<span class=\"pill ok\">ALL PASS</span>" : "<span class=\"pill bad\">FAIL</span>")
      << "</div></div></header>";

    o << "<div class=\"app\">";

    // ---- sidebar ----
    o << "<nav class=\"side\"><input id=\"q\" placeholder=\"search calculators...\" "
         "oninput=\"flt(this.value)\">";
    o << "<a data-id=\"overview\" data-c=\"overview\" class=\"active\" onclick=\"show('overview')\">"
         "<span class=\"dot\"></span>Overview</a>";
    for (const auto& dom : domains) {
        bool grp = false;
        for (std::size_t i = 0; i < reports.size(); ++i) {
            if (reports[i].domain != dom) continue;
            if (!grp) { o << "<div class=\"grp\">" << esc(dom) << "</div>"; grp = true; }
            const auto& r = reports[i];
            o << "<a data-id=\"c" << i << "\" data-c=\"" << esc(lower(r.name)) << "\" onclick=\"show('c"
              << i << "')\"><span class=\"dot " << (r.passed() ? "" : "bad") << "\"></span>"
              << esc(r.name) << "</a>";
        }
    }
    o << "</nav><main>";

    // ---- overview section ----
    o << "<section id=\"overview\"><h2>Overview</h2>"
         "<p class=\"dim\">Each calculator is run over the input vectors extracted from the trusted Excel "
         "sheets; emc's output is compared against the sheet's own formula result (ground truth). "
         "Pick a calculator from the list on the left. The \"max error\" is floored so a value "
         "crossing zero (or the sheet's own rounding) cannot inflate it.</p>";
    o << "<table><thead><tr><th>Calculator</th><th>Reference CSV</th><th class=\"n\">count</th>"
         "<th class=\"n\">max error</th><th class=\"n\">tol</th><th>status</th></tr></thead><tbody>";
    for (const auto& dom : domains) {
        bool grp = false;
        for (std::size_t i = 0; i < reports.size(); ++i) {
            if (reports[i].domain != dom) continue;
            if (!grp) { o << "<tr><td colspan=\"6\" style=\"color:var(--mut);background:#11161d;"
                            "text-transform:uppercase;font-size:11px;letter-spacing:.04em\">"
                         << esc(dom) << "</td></tr>"; grp = true; }
            const auto& r = reports[i];
            o << "<tr class=\"clk\" onclick=\"show('c" << i << "')\"><td>" << esc(r.name)
              << "</td><td class=\"n\"><code style=\"font-size:11px\">" << esc(r.csv_file)
              << "</code></td><td class=\"n\">" << r.comparison_count()
              << "</td><td class=\"n\">" << ep(r.display_max())
              << "</td><td class=\"n\">" << sci(r.tolerance) << "</td><td>"
              << (r.passed() ? "<span class=\"pill ok\">PASS</span>" : "<span class=\"pill bad\">FAIL</span>")
              << "</td></tr>";
        }
    }
    o << "</tbody></table></section>";

    // ---- per-calculator sections ----
    for (std::size_t i = 0; i < reports.size(); ++i) {
        const auto& r = reports[i];
        o << "<section id=\"c" << i << "\" hidden><div class=\"calc-head\"><h2>" << esc(r.name)
          << "</h2><span class=\"tag\">" << esc(r.domain) << "</span>"
          << (r.passed() ? "<span class=\"pill ok\">PASS</span>" : "<span class=\"pill bad\">FAIL</span>")
          << "</div>";
        o << "<div class=\"meta\">Reference: <code>tests/reference/" << esc(r.csv_file) << "</code>"
          << " &middot; " << r.comparison_count() << " comparisons"
          << " &middot; max error " << ep(r.display_max())
          << " &middot; avg " << ep(r.display_avg())
          << " &middot; tolerance " << sci(r.tolerance) << "</div>";
        o << "<div class=\"formula\">" << esc(r.formula) << "</div>";
        if (!r.note.empty()) o << "<div class=\"note\">" << esc(r.note) << "</div>";
        o << "<table><thead><tr><th>Inputs</th><th>Quantity</th><th class=\"n\">emc</th>"
             "<th class=\"n\">Excel</th><th class=\"n\">unit</th><th class=\"n\">error</th></tr></thead><tbody>";
        for (const auto& c : r.cases) {
            bool first = true;
            for (const auto& cmp : c.outputs) {
                const bool ok = cmp.within(r.tolerance, r.abs_floor);
                o << "<tr><td>" << (first ? esc(c.inputs) : std::string{}) << "</td><td>"
                  << esc(cmp.quantity) << "</td><td class=\"n\">" << num(cmp.computed)
                  << "</td><td class=\"n\">" << num(cmp.expected) << "</td><td class=\"n\">"
                  << esc(cmp.unit) << "</td><td class=\"n " << (ok ? "err-ok" : "err-bad") << "\">"
                  << ep(cmp.display_error(r.abs_floor)) << "</td></tr>";
                first = false;
            }
        }
        o << "</tbody></table></section>";
    }

    o << "</main></div><script>\n"
         "function show(id){document.querySelectorAll('main>section').forEach(function(s){s.hidden=s.id!==id;});"
         "document.querySelectorAll('.side a').forEach(function(a){a.classList.toggle('active',a.dataset.id===id);});"
         "window.scrollTo(0,0);}\n"
         "function flt(v){v=v.toLowerCase();document.querySelectorAll('.side a[data-c]').forEach(function(a){"
         "a.style.display=a.dataset.c.indexOf(v)>=0?'':'none';});}\n"
         "</script></body></html>\n";
    o.close();

    std::cout << "wrote " << out_path << ": " << reports.size() << " calculators, "
              << total_cmp << " comparisons, worst error " << ep(overall_max)
              << (all_pass ? " (ALL PASS)\n" : " (FAIL)\n");
    return all_pass ? 0 : 2;
}
