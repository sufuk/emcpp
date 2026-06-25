// tests/support/csv.hpp   (test-only; header-only is fine)
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace emc::test {

// Parse a leading double out of `sv`; returns false if it doesn't start with a
// number. libc++ (the clang build) does not implement floating-point
// std::from_chars, so we use strtod. The reference CSVs use '.' decimals and the
// tests run in the default "C" locale, so strtod reads them deterministically.
[[nodiscard]] inline bool parse_double(std::string_view sv, double& out) {
    const std::string s{sv};            // null-terminate for strtod
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end != s.c_str();            // at least one character consumed -> a number
}

// Each row OWNS its fields as separate strings. Earlier the fields were views
// into a sibling `line` member, but Rows are moved into a vector (which may
// reallocate), and a view into a moved-from std::string dangles for small (SSO)
// strings. libc++'s SSO buffer is larger than libstdc++'s, so the view-based
// design silently broke only under clang. Owning the fields avoids that entirely.
struct Row {
    std::vector<std::string> fields;        // owns each field's bytes

    [[nodiscard]] std::string_view at(std::size_t i) const { return fields.at(i); }

    // Parse field i as a double (locale-independent in the test's "C" locale).
    [[nodiscard]] double num(std::size_t i) const {
        const std::string& s = fields.at(i);
        double v{};
        if (!parse_double(s, v))
            throw std::runtime_error("bad double in reference field: " + s);
        return v;
    }

    // A copy of a field when the caller really needs a std::string.
    [[nodiscard]] std::string str(std::size_t i) const { return fields.at(i); }
};

// Split a line into OWNED field strings. A plain find/substr loop (not
// std::views::split) so the field boundaries are identical under libstdc++ and
// libc++; owning the bytes keeps each field valid after Rows are moved/realloc'd.
// [[nodiscard]]: the split result is the whole point; ignoring it is a bug.
[[nodiscard]] inline std::vector<std::string> split(std::string_view s, char delim = ',') {
    std::vector<std::string> out;
    for (std::size_t start = 0;;) {
        const std::size_t pos = s.find(delim, start);
        if (pos == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            return out;
        }
        out.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
}

// Load a CSV of reference vectors. Skips blank lines, '#' comments, and any header
// line whose first field is non-numeric (so a self-describing header like
// "frequency[MHz]" is ignored). Each returned Row owns its fields outright.
// [[nodiscard]]: the loaded rows are the result; ignoring them is a bug.
[[nodiscard]] inline std::vector<Row> load_csv(const std::filesystem::path& path) {
    std::ifstream in{path};
    if (!in)
        throw std::runtime_error("cannot open reference file: " + path.string());

    std::vector<Row> rows;
    for (std::string line; std::getline(in, line);) {
        // Drop a trailing '\r' so CRLF (Windows) files parse the same as LF files.
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string_view sv{line};   // view for the cheap blank/comment checks
        if (sv.empty() || sv.starts_with('#'))
            continue;

        std::vector<std::string> fields = split(sv);
        if (fields.empty())
            continue;

        // Skip a header row: if the first field does not parse as a number, treat
        // the whole line as a label/header and ignore it.
        double probe{};
        if (!parse_double(fields.front(), probe))
            continue;   // header / label line

        rows.push_back(Row{.fields = std::move(fields)});
    }
    return rows;
}

} // namespace emc::test
