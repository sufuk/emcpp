// include/emc/core/calculator.hpp
#pragma once  // include this header once per translation unit

#include <concepts>
#include <expected>

#include <emc/core/error.hpp>   // emc::Error, emc::Result

namespace emc {

// ---------------------------------------------------------------------------
//  Calculator
//
//  A "calculator" is a tag type C that names two helper types (Input and
//  Result) and provides a static calculate(const Input&) -> Result<Result>.
//  The real math lives in free functions inside a category namespace. Each
//  guide adds a thin, zero-data tag struct that forwards to that free
//  calculate(). This lets generic code (the reference runner, batch sweeps,
//  future reflection) treat the (Input, Result, calculate) triple as one
//  named thing.
//
//  Why a concept and not a virtual base class:
//  A concept is a compile-time contract. Types that do not match simply fail
//  to compile, with no runtime cost. The OO alternative
//  (class Calculator { virtual Result solve() = 0; }) would add a vtable,
//  force heap allocation and type erasure, and slow down math that is pure
//  and fully known at the call site.
// ---------------------------------------------------------------------------
template <class C>
concept Calculator =
    requires {
        typename C::Input;
        typename C::Result;
    } &&
    requires(const typename C::Input& in) {
        { C::calculate(in) } -> std::same_as<emc::Result<typename C::Result>>;
    };

// A stricter version: also requires validate(const Input&), which returns
// either nothing (success) or an Error. std::expected<void, Error> models
// "succeeded, or here is why it failed" as a plain return value, no exceptions.
template <class C>
concept ValidatedCalculator =
    Calculator<C> &&
    requires(const typename C::Input& in) {
        { C::validate(in) } -> std::same_as<std::expected<void, Error>>;
    };

} // namespace emc
