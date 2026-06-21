// src/core/error.cpp
//
// This is the one compiled file for the Error type. The only real work here is
// building a human-readable string from the structured error fields. We keep it
// out of the header so the header stays lightweight (no <string> formatting code
// pulled into every translation unit that just wants to declare an Error).

#include <emc/core/error.hpp>

#include <string>   // std::string, std::to_string

namespace emc {

// Build a one-line summary like: "[out_of_range] message (field: x) (allowed: [lo, hi])".
// A real front end would build its own localized text from the fields; this is a
// fallback for CLI and CI logs.
std::string Error::what() const {
    std::string s;
    s += '[';
    s += to_string(code);   // to_string is constexpr in the header; here it just returns the token
    s += "] ";
    s += message;
    // field is a string_view: we only read it, so .empty() is a cheap check, no copy.
    if (!field.empty()) { s += " (field: "; s += field; s += ')'; }
    // range is std::optional: it models "maybe no range" instead of a magic value.
    if (range) {
        s += " (allowed: [";
        s += std::to_string(range->first);
        s += ", ";
        s += std::to_string(range->second);
        s += "])";
    }
    return s;
}

}  // namespace emc
