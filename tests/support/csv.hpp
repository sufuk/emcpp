// tests/support/csv.hpp   (test-only; header-only is fine)
#pragma once

#include <charconv>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace emc::test {

// One row keeps its own copy of the line text. Each field is a view into that
// text, so reading fields costs no extra copies.
struct Row {
    std::string line;                       // owns the bytes
    std::vector<std::string_view> fields;   // zero-copy views into `line`

    // [[nodiscard]]: do not ignore the returned field; dropping it is a bug, so the compiler warns.
    // string_view return: we only READ the field, we do not own it, so a view avoids a copy.
    [[nodiscard]] std::string_view at(std::size_t i) const { return fields.at(i); }

    // Parse field i as a double. from_chars is locale-independent, so a '.' always
    // means a decimal point no matter what region the test machine is set to.
    [[nodiscard]] double num(std::size_t i) const {
        const std::string_view sv = fields.at(i);   // view: just reading, no copy
        double v{};
        const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        if (ec != std::errc{})
            throw std::runtime_error("bad double in reference field: " + std::string{sv});
        return v;
    }

    // Make an owning copy of a field when the caller really needs a std::string.
    [[nodiscard]] std::string str(std::size_t i) const { return std::string{fields.at(i)}; }
};

// Split a line on `delim` into views.
// string_view param: we only read the input text, so a view avoids copying it.
// [[nodiscard]]: the split result is the whole point; ignoring it is a bug.
[[nodiscard]] inline std::vector<std::string_view> split(std::string_view s, char delim = ',') {
    auto to_sv = [](auto&& sub) {
        return std::string_view{&*std::ranges::begin(sub),
                                static_cast<std::size_t>(std::ranges::distance(sub))};
    };
    // views::split is lazy; ranges::to turns the lazy pieces into a real vector.
    return s | std::views::split(delim)
             | std::views::transform(to_sv)
             | std::ranges::to<std::vector>();
}

// Load a CSV of reference vectors. Skips blank lines, '#' comments, and any header
// line whose first field is non-numeric (so a self-describing header like
// "frequency[MHz]" is ignored). Each returned Row owns its line, so the field
// views inside it stay valid after this function returns.
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

        Row r{.line = std::move(line)};
        r.fields = split(r.line);   // re-view the line now that it lives inside the Row
        if (r.fields.empty())
            continue;

        // Skip a header row: if the first field does not parse as a number, treat
        // the whole line as a label/header and ignore it.
        double probe{};
        const std::string_view f0 = r.fields.front();
        const auto [ptr, ec] = std::from_chars(f0.data(), f0.data() + f0.size(), probe);
        if (ec != std::errc{})
            continue;   // header / label line

        rows.push_back(std::move(r));
    }
    return rows;
}

} // namespace emc::test
