// src/core/materials.cpp
//
// This translation unit anchors the compiled library target for the material
// database. The database itself (the table, lookups, named constants) is all
// constexpr and lives entirely in the header, so this file is small on purpose.
// It exists so the linker has a real object to build, and so any FUTURE
// material helper that cannot be constexpr has a natural home here.

#include <emc/core/materials.hpp>

namespace emc::materials {

// How many built-in materials we ship (Custom is not counted; it has no row).
// noexcept: promises not to throw, so callers can rely on that and the compiler
// can optimize. We just read the size of the compile-time table from the header.
std::size_t builtin_count() noexcept { return table.size(); }

} // namespace emc::materials
