// validation/validators.hpp
#pragma once

#include <vector>

#include "validation.hpp"

namespace emc::validation {

// Runs every registered calculator against its reference CSV and returns one
// CalcReport each. Defined in validators.cpp; consumed by the reference test
// (CI gate) and by the HTML report tool.
[[nodiscard]] std::vector<CalcReport> all_reports();

} // namespace emc::validation
