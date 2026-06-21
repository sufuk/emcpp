// include/emc/core/error.hpp
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>      // std::pair, std::move

#include <emc/export.hpp>   // generated header that defines EMC_API (export macro)

namespace emc {

// ---------------------------------------------------------------------------
//  ErrorCode — names the kind of failure for the machine to read.
// ---------------------------------------------------------------------------
// enum class: scoped, so it will not silently turn into an int. An error code
// can never be mixed up with a status number, a resistivity, or an index.
// The ": std::uint8_t" fixes the size at one byte to keep Error small.
enum class ErrorCode : std::uint8_t {
    OutOfRange,        // a quantity is outside its physically/numerically valid interval
    InvalidInput,      // structurally bad input (NaN, empty selection, wrong combination)
    DomainError,       // math-domain violation (log of <= 0, sqrt of negative, acos > 1, ...)
    DivisionByZero,    // a denominator the formula cannot tolerate evaluated to 0
    UnknownMaterial,   // Material id has no table row (or Custom queried without props)
    NotConverged,      // an iterative solver hit its iteration cap without a result
    Unsupported,       // a valid-but-not-implemented case
};

// Stable, lowercase text token for logs / CSV diffs / std::format.
// [[nodiscard]]: do not ignore the return value; dropping it is a bug, so the
//                compiler warns.
// constexpr: can run at compile time, so it works inside compile-time tables.
// std::string_view return: we hand back a view of a fixed string literal that
//                we do not own, so there is no copy or allocation.
// noexcept: promises not to throw; lets the compiler optimize and callers rely on it.
[[nodiscard]] constexpr std::string_view to_string(ErrorCode c) noexcept {
    switch (c) {
    case ErrorCode::OutOfRange:      return "out_of_range";
    case ErrorCode::InvalidInput:    return "invalid_input";
    case ErrorCode::DomainError:     return "domain_error";
    case ErrorCode::DivisionByZero:  return "division_by_zero";
    case ErrorCode::UnknownMaterial: return "unknown_material";
    case ErrorCode::NotConverged:    return "not_converged";
    case ErrorCode::Unsupported:     return "unsupported";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
//  Error — the full failure payload. A front end or a test can read each field
//  on its own instead of parsing one free-text string.
//
//  NOTE: Error is kept an aggregate (it declares no constructor and has no
//  private data). That gives us two simple things: the factory helpers below
//  can build it with designated initializers (named fields like .code/.message),
//  and call sites read .message / .code as plain public members — no getter
//  needed. The accessor methods further down do NOT break aggregate-ness; only a
//  user-declared constructor or private data would.
// ---------------------------------------------------------------------------
struct Error {
    ErrorCode                            code;
    std::string                          message;
    // std::string_view: we only READ this label and do not own it, so a view
    // avoids a copy/allocation. It points at a literal that outlives the Error.
    std::string_view                     field{};
    // std::optional: models "maybe there is a valid range" as a type, instead of
    // a magic sentinel pair. (lo, hi) are in the field's display unit.
    std::optional<std::pair<double, double>> range{};
    // std::source_location: auto-captures where the Error was raised, for
    // diagnostics. It is free and never shown to end users.
    std::source_location                 where = std::source_location::current();

    // Method-style accessors that read the same storage as the public members.
    // Calculators may use either err.code or err.code_of(); both are fine.
    // noexcept: simple reads, never throw.
    [[nodiscard]] ErrorCode        code_of()  const noexcept { return code; }
    [[nodiscard]] const std::string& message_of() const noexcept { return message; }

    // One-line summary for CLI/CI logs. A real front end builds its own localized
    // message from the structured fields; this is just a fallback.
    // Declared here, defined out-of-line in src/core/error.cpp.
    // EMC_API: marks it for export so other modules can link to it.
    [[nodiscard]] EMC_API std::string what() const;
};

// ---------------------------------------------------------------------------
//  Result<T> — the one success-or-Error channel used across the whole library.
//  Every calculate()/solve_*()/properties() returns emc::Result<...>.
// ---------------------------------------------------------------------------
// std::expected<T, Error>: returns either a value or an Error as a normal value
// — no exceptions and no sentinel numbers. The caller must handle both.
template <class T>
using Result = std::expected<T, Error>;

// ---------------------------------------------------------------------------
//  Error factories — short helpers so calculators stay terse. The default
//  std::source_location records the CALL SITE, so each Error notes where it was
//  raised for free.
// ---------------------------------------------------------------------------
// [[nodiscard]]: building an Error and then ignoring it is a bug; the compiler warns.
// std::string_view field/why: we only READ this text, so a view avoids a copy.
[[nodiscard]] inline Error out_of_range(double lo, double hi, std::string_view field,
                                        std::source_location w = std::source_location::current()) {
    return Error{
        .code    = ErrorCode::OutOfRange,
        .message = std::string{field}.append(" is out of range"),
        .field   = field,
        .range   = std::pair{lo, hi},
        .where   = w,
    };
}

[[nodiscard]] inline Error invalid_input(std::string_view why, std::string_view field = {},
                                         std::source_location w = std::source_location::current()) {
    return Error{ .code = ErrorCode::InvalidInput, .message = std::string{why},
                  .field = field, .range = std::nullopt, .where = w };
}

[[nodiscard]] inline Error domain_error(std::string_view why, std::string_view field = {},
                                        std::source_location w = std::source_location::current()) {
    return Error{ .code = ErrorCode::DomainError, .message = std::string{why},
                  .field = field, .range = std::nullopt, .where = w };
}

[[nodiscard]] inline Error division_by_zero(std::string_view field,
                                            std::source_location w = std::source_location::current()) {
    return Error{ .code = ErrorCode::DivisionByZero,
                  .message = std::string{field}.append(" would divide by zero"),
                  .field = field, .range = std::nullopt, .where = w };
}

// ---------------------------------------------------------------------------
//  Validators — the three checks that every calculator's validate() is built
//  from. They take RAW doubles: a calculator pulls the numeric value of an
//  mp-units quantity in its display unit (q.numerical_value_in(u)) and passes it
//  here, so the reported [lo, hi] is in the user's unit.
//
//  std::expected<void, Error> is the "ok, or the first failure" shape: an empty
//  {} means success.
//  [[nodiscard]]: ignoring the check result defeats the point, so the compiler warns.
//  constexpr: lets reference tables and static_assert checks run these at compile time.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::expected<void, Error>
in_range(double v, double lo, double hi, std::string_view field,
         std::source_location w = std::source_location::current()) {
    if (v < lo || v > hi)
        return std::unexpected(out_of_range(lo, hi, field, w));
    return {};
}

[[nodiscard]] constexpr std::expected<void, Error>
require_positive(double v, std::string_view field,
                 std::source_location w = std::source_location::current()) {
    if (!(v > 0.0))     // also rejects NaN (NaN > 0 is false)
        return std::unexpected(Error{ .code = ErrorCode::OutOfRange,
                                      .message = std::string{field}.append(" must be > 0"),
                                      .field = field, .range = std::nullopt, .where = w });
    return {};
}

[[nodiscard]] constexpr std::expected<void, Error>
require_nonzero(double v, std::string_view field,
                std::source_location w = std::source_location::current()) {
    if (v == 0.0)
        return std::unexpected(division_by_zero(field, w));
    return {};
}

} // namespace emc
