# Build System (Modern CMake, Compiled Library) ⚙️

> Purpose: specify the complete CMake build for the `emc` library as a **traditional compiled library**
> (static and/or shared) named `emc` exporting the alias `emc::emc`, including dependency acquisition
> (mp-units), shared-library symbol visibility, install/export and package config so downstream code can
> `find_package(emc)`, presets, and quality tooling.

---

## 0. Reading this document

This is the *mechanical* document: exactly what CMake to write so that the layer structure of
`01-architecture-and-layout.md` (Foundation / Domain / Facade, the `emc::*` namespaces), the units layer
of `03-quantities-and-units-mp-units.md` (mp-units), and the error model of
`05-error-handling-and-validation.md` (`std::expected`, C++23) compile, install, and are consumable.

Locked decisions this document obeys:

- **C++23 baseline** — `target_compile_features(emc PUBLIC cxx_std_23)`; no global `CMAKE_CXX_STANDARD`,
  no compiler-specific `-std=` strings.
- **Compiled library, not header-only, not modules-first** — public headers in `include/emc/`, compiled
  translation units in `src/`, built into a real `add_library(emc ...)` target with `install()`/`export()`
  and a generated package config. §11 notes C++20 modules as a *future* option only.
- **mp-units is the units layer** — pulled with `find_package(mp-units CONFIG)` and a `FetchContent`
  fallback, propagated `PUBLIC` because it appears in `emc`'s public headers (every quantity type).
- **GUI-free library build** — there is deliberately no UI toolkit anywhere in this project. A CI grep
  gate (§9.1) enforces it.

> [!IMPORTANT]
> Two design choices below are non-obvious and load-bearing: sources are listed **explicitly** (not
> globbed), and the C++23 requirement is a **`PUBLIC` usage requirement** on the target. Both are
> explained inline at §1.1.

---

## 1. Top-level `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.28)
# 3.28 chosen deliberately:
#  - solid C++23 feature-level handling (cxx_std_23) across GCC 14 / Clang 18 / MSVC 19.4x,
#  - first CMake with a supported `import std` path (we do NOT use it yet — see §11 — but we
#    want the floor high enough that turning modules on later is not a version bump),
#  - CMakePresets schema v6 (used in §6).

project(emc
    VERSION 0.1.0
    DESCRIPTION "Modern-C++ EMC engineering calculation library"
    HOMEPAGE_URL "https://github.com/sufuk/emcpp"
    LANGUAGES CXX)

# ---- Project-level guard rails -------------------------------------------------------------
# Refuse in-source builds.
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a build/ directory or a preset.")
endif()

# Generate compile_commands.json for clang-tidy / clangd / IDEs (see §7.3).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default visibility hidden so the shared lib only exports what EMC_API marks (see §3).
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Sensible default build type for single-config generators.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Honor the user's choice of static vs shared via BUILD_SHARED_LIBS; default static.
option(BUILD_SHARED_LIBS "Build emc as a shared library" OFF)

# Project options.
option(EMC_BUILD_TESTS    "Build the emc test suite"        ${PROJECT_IS_TOP_LEVEL})
option(EMC_BUILD_EXAMPLES "Build the emc usage examples"    ${PROJECT_IS_TOP_LEVEL})
option(EMC_WARNINGS_AS_ERRORS "Treat warnings as errors"    ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)   # CMAKE_INSTALL_{LIBDIR,INCLUDEDIR,BINDIR,...}

# ---- Dependencies (see §2) -----------------------------------------------------------------
include(cmake/Dependencies.cmake)   # brings in mp-units (+ test deps when EMC_BUILD_TESTS)

# ---- The library target --------------------------------------------------------------------
add_library(emc)            # STATIC or SHARED chosen by BUILD_SHARED_LIBS
add_library(emc::emc ALIAS emc)   # consumers always say emc::emc, never bare emc

# C++23 as a PUBLIC usage requirement (propagates to consumers — see §1.1).
target_compile_features(emc PUBLIC cxx_std_23)

# Explicit source list (NOT glob — see §1.1). Grouped to mirror doc 01's layer/category layout.
target_sources(emc PRIVATE
    # Foundation layer
    src/error.cpp
    src/units/parse.cpp
    src/constants/constants.cpp          # mostly constexpr in headers; .cpp anchors any ODR-used defs
    src/materials/material_db.cpp
    # Domain layer — one .cpp per calculator (doc 07 work-list)
    src/basic/skin_depth.cpp
    src/basic/decibel.cpp
    src/basic/antenna.cpp
    src/converter/wavelength_frequency.cpp
    src/converter/efield_powerdensity.cpp
    # ... (the remaining ~47 calculators are added here as doc 07 items land)
    src/component/microstrip_trace.cpp
    src/component/standard_gauge_wire.cpp
    src/shielding/rectangular_enclosure.cpp
    # ...
)

# Public include directory: build-tree path while building, install path once installed.
target_include_directories(emc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# The generated export header lives in the build tree; expose it for in-tree builds.
target_include_directories(emc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)

# mp-units is in emc's PUBLIC headers (every quantity type) -> PUBLIC link (see §2).
target_link_libraries(emc PUBLIC mp-units::mp-units)

# Warnings & version/soname metadata.
include(cmake/CompilerWarnings.cmake)   # sets up the -Wall -Wextra ... interface lib (see §7)
target_link_libraries(emc PRIVATE emc_project_warnings)

set_target_properties(emc PROPERTIES
    VERSION   ${PROJECT_VERSION}        # full version on the .so file
    SOVERSION ${PROJECT_VERSION_MAJOR}  # ABI compat tracked by major (see §10)
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    EXPORT_NAME emc)                    # so the exported target is emc::emc

# ---- Export header (see §3) ----------------------------------------------------------------
include(GenerateExportHeader)
generate_export_header(emc
    BASE_NAME EMC
    EXPORT_MACRO_NAME EMC_API
    NO_EXPORT_MACRO_NAME EMC_LOCAL
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/emc/export.hpp)

# ---- Subdirectories ------------------------------------------------------------------------
if(EMC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)        # see doc 09
endif()
if(EMC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---- Install / export (see §4) -------------------------------------------------------------
include(cmake/Install.cmake)
```

### 1.1 Notes on the snippet

- **`add_library(emc)` with no `STATIC`/`SHARED`** lets `BUILD_SHARED_LIBS` decide. The plan supports
  both (static and/or shared); a consumer or packager flips one cache var. Because visibility is
  default-hidden + `EMC_API` (§3), the *static* build behaves identically and the *shared* build exports a
  clean, minimal symbol set.
- **`emc::emc` alias** is created at configure time so in-tree examples/tests link the same name a
  downstream `find_package(emc)` consumer uses — making `examples/` byte-for-byte identical in-tree or
  against the installed package, a cheap correctness check.
- **`mp-units::mp-units` is `PUBLIC`**, not `PRIVATE`. Every public `emc` header puts mp-units types in
  function signatures (e.g. `quantity<isq::frequency[si::hertz]>`), so consumers need mp-units' include
  dirs and compile flags transitively. `PUBLIC` propagates them; `PRIVATE` would compile `emc` but break
  every consumer with "mp-units/... not found". This is also why the package config must
  `find_dependency(mp-units)` (§4).
- **No `GLOB_RECURSE`.** CMake evaluates a glob *once* at configure time, so adding a new calculator
  `.cpp` would not re-run CMake and the file would silently not build until someone reconfigures. The
  doc-07 work-list adds ~52 calculators incrementally, so sources are listed explicitly via
  `target_sources`. (`CONFIGURE_DEPENDS` exists but is best-effort, slows every build, and is discouraged
  for installed libraries.)
- **C++23 is a `PUBLIC` usage requirement, not a global.** A global `CMAKE_CXX_STANDARD` does not travel
  with the target — a consumer linking `emc::emc` would not get C++23. Because `emc`'s public headers
  *require* C++23 (`std::expected`, `std::format`, mp-units' deducing-this), the requirement is a `PUBLIC`
  `target_compile_features`, which propagates to every consumer transitively.
- **`PROJECT_IS_TOP_LEVEL`** (CMake ≥ 3.21) drives test/example defaults: ON when `emc` is the root
  project, OFF when pulled in via `add_subdirectory`/`FetchContent` — so consuming the library does not
  drag in its tests.

> ### Modern C++ features used here / and why
>
> - **`target_compile_features(... PUBLIC cxx_std_23)`** — `emc`'s public API exposes `std::expected`,
>   `std::format`, and mp-units' deducing-this in header signatures, so the C++23 requirement is part of
>   the *contract*; making it a `PUBLIC` usage requirement guarantees every consumer compiles in the same
>   language mode the headers were written for.
> - **Generator expressions (`$<BUILD_INTERFACE>` / `$<INSTALL_INTERFACE>`)** — the include root differs
>   between the build tree and the install tree; generator expressions encode both in one target so the
>   same `emc::emc` works whether linked in-tree or via `find_package`.
> - **`BUILD_SHARED_LIBS`-driven `add_library(emc)`** — leaving the library kind unspecified lets one
>   cache variable select static or shared, which combined with default-hidden visibility yields identical
>   public behavior in both modes from a single source tree.

---

## 2. Dependencies (`cmake/Dependencies.cmake`)

Pattern: prefer a system/`find_package` copy; fall back to `FetchContent` so a clean checkout builds with
zero manual setup. This is the modern "find-or-fetch" idiom.

```cmake
# cmake/Dependencies.cmake
include(FetchContent)

# ---- mp-units (the units layer — doc 03) ---------------------------------------------------
# Try an installed/system copy first (fast, cached, packager-friendly).
find_package(mp-units CONFIG QUIET)
if(NOT mp-units_FOUND)
    message(STATUS "emc: mp-units not found via find_package; fetching with FetchContent")
    FetchContent_Declare(mp-units
        GIT_REPOSITORY https://github.com/mpusz/mp-units.git
        GIT_TAG        v2.5.0          # pin an exact tag for reproducibility (see §12)
        GIT_SHALLOW    TRUE
        SYSTEM)                        # treat its headers as -isystem (no warnings from deps)
    # mp-units sub-options: we only need the core + the systems we use.
    set(MP_UNITS_BUILD_CXX_MODULES OFF CACHE BOOL "" FORCE)   # headers path (see §11)
    set(MP_UNITS_BUILD_AS_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mp-units)
endif()

# ---- Test framework (only when building tests) ---------------------------------------------
if(EMC_BUILD_TESTS)
    find_package(Catch2 3 CONFIG QUIET)
    if(NOT Catch2_FOUND)
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.7.1
            GIT_SHALLOW    TRUE
            SYSTEM)
        FetchContent_MakeAvailable(Catch2)
    endif()
endif()

# ---- magic_enum (optional; only if a calculator's Input uses string<->enum at a boundary) ---
# Used e.g. to parse a material name string into emc::materials::Material at a parse boundary
# (doc 04). Kept optional and PRIVATE so it never leaks into emc's public ABI.
option(EMC_USE_MAGIC_ENUM "Use magic_enum for enum<->string at boundaries" OFF)
if(EMC_USE_MAGIC_ENUM)
    find_package(magic_enum CONFIG QUIET)
    if(NOT magic_enum_FOUND)
        FetchContent_Declare(magic_enum
            GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
            GIT_TAG        v0.9.7
            GIT_SHALLOW    TRUE
            SYSTEM)
        FetchContent_MakeAvailable(magic_enum)
    endif()
endif()
```

If `EMC_USE_MAGIC_ENUM` is on, link it **PRIVATE** in the top-level file so it stays an implementation
detail and does not become a `find_dependency` in the package config:

```cmake
if(EMC_USE_MAGIC_ENUM)
    target_link_libraries(emc PRIVATE magic_enum::magic_enum)
    target_compile_definitions(emc PRIVATE EMC_HAVE_MAGIC_ENUM=1)
endif()
```

> [!NOTE]
> **PUBLIC vs PRIVATE rule of thumb:** a dependency goes `PUBLIC` iff it appears in a public header under
> `include/emc/`. mp-units → `PUBLIC` (quantity types in every signature). Catch2 → test target only.
> magic_enum → `PRIVATE` (used only inside `.cpp` parse helpers). This rule keeps the package config's
> `find_dependency` list minimal and correct.

---

## 3. Shared-library symbol visibility (`include/emc/export.hpp`)

A shared `emc` defaults to **hidden** visibility (set in §1: `CMAKE_CXX_VISIBILITY_PRESET hidden` +
`VISIBILITY_INLINES_HIDDEN`). Only symbols explicitly marked `EMC_API` are exported. This is the modern
default-hidden discipline: smaller export tables, faster load, no accidental ABI surface, and it makes
Windows (`__declspec(dllexport/dllimport)`) and ELF/Mach-O (`__attribute__((visibility("default")))`)
behave identically from one macro.

We generate the macro with `generate_export_header` (called in §1), which writes `include/emc/export.hpp`
in the build tree. Generated content is equivalent to:

```c++
// include/emc/export.hpp  (generated by generate_export_header)
#ifndef EMC_API_H
#define EMC_API_H

#ifdef EMC_STATIC_DEFINE
#  define EMC_API
#  define EMC_LOCAL
#else
#  ifndef EMC_API
#    ifdef emc_EXPORTS                 /* defined by CMake when building the emc shared lib */
#      ifdef _WIN32
#        define EMC_API __declspec(dllexport)
#      else
#        define EMC_API __attribute__((visibility("default")))
#      endif
#    else                              /* consuming the shared lib */
#      ifdef _WIN32
#        define EMC_API __declspec(dllimport)
#      else
#        define EMC_API __attribute__((visibility("default")))
#      endif
#    endif
#  endif
#  ifndef EMC_LOCAL
#    ifdef _WIN32
#      define EMC_LOCAL
#    else
#      define EMC_LOCAL __attribute__((visibility("hidden")))
#    endif
#  endif
#endif

#endif /* EMC_API_H */
```

For a static build CMake compiles with `-DEMC_STATIC_DEFINE`, so `EMC_API` expands to nothing — the exact
same source works in both modes.

### 3.1 Where `EMC_API` goes (and where it must NOT)

Most of `emc` is templates and `constexpr`, which are **header-only and must NOT carry `EMC_API`** —
exporting a template instantiation is meaningless and on MSVC actively wrong. Mark only the
**non-template, non-inline functions compiled in `src/*.cpp`** — chiefly the few runtime boundary helpers
(string→quantity parsing, material lookup, error-message formatting):

```c++
// include/emc/units/parse.hpp
#include <emc/export.hpp>
#include <emc/error.hpp>
#include <expected>
#include <string_view>

namespace emc::units {

// Compiled in src/units/parse.cpp -> needs EMC_API so the shared lib exports it.
[[nodiscard]] EMC_API std::expected<Length, Error>
parse_length(std::string_view text);

}  // namespace emc::units
```

```c++
// include/emc/basic/skin_depth.hpp  -- a pure calculator: NO EMC_API.
// calculate() is constexpr/inline and header-resolved; nothing to export.
namespace emc::basic {

struct SkinDepthInput  { /* mp-units quantities, designated-init */ };
struct SkinDepthResult { /* ... */ };

[[nodiscard]] constexpr std::expected<SkinDepthResult, Error>
calculate(const SkinDepthInput& in) noexcept;   // no EMC_API: header-only

}  // namespace emc::basic
```

> [!TIP]
> Default-hidden visibility is the library-shaped equivalent of "expose only what you mean to": the public
> surface is exactly the set of `EMC_API`-marked functions plus the header-only calculators, nothing else.

> ### Modern C++ features used here / and why
>
> - **`constexpr` calculators in headers (no `EMC_API`)** — EMC formulas are pure numeric functions over
>   physical quantities, so making `calculate()` `constexpr` lets call sites fold known inputs at compile
>   time and keeps those functions out of the exported ABI entirely.
> - **`[[nodiscard]] std::expected<Result, Error>`** — calculator inputs have physical domains, so
>   `std::expected` reports an out-of-domain input as a recoverable typed error the caller cannot silently
>   ignore, with no exceptions crossing the shared-library boundary.

---

## 4. Install & package config (`cmake/Install.cmake` + `cmake/emcConfig.cmake.in`)

Goal: after `cmake --install`, a completely separate project does

```cmake
find_package(emc 0.1 REQUIRED)
target_link_libraries(myapp PRIVATE emc::emc)
```

and gets the headers, the compiled lib, the C++23 requirement, *and* mp-units transitively — with zero
manual include/link flags.

```cmake
# cmake/Install.cmake
include(CMakePackageConfigHelpers)

# 1. Install the library + record it in an export set, attaching include dirs from §1.
install(TARGETS emc emc_project_warnings
    EXPORT emcTargets
    LIBRARY     DESTINATION ${CMAKE_INSTALL_LIBDIR}      # .so / .dylib
    ARCHIVE     DESTINATION ${CMAKE_INSTALL_LIBDIR}      # .a / .lib
    RUNTIME     DESTINATION ${CMAKE_INSTALL_BINDIR}      # .dll (Windows)
    INCLUDES    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# 2. Install the public headers (hand-authored) ...
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/emc
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp")

# 3. ... and the *generated* export header from the build tree.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/emc/export.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/emc)

# 4. Install the export set -> creates emcTargets.cmake defining the imported emc::emc target.
install(EXPORT emcTargets
    FILE        emcTargets.cmake
    NAMESPACE   emc::                         # makes the imported target emc::emc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/emc)

# 5. Generate emcConfig.cmake from the template (which calls find_dependency(mp-units)).
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/emcConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/emc)

# 6. Generate the version file (SameMajorVersion: 0.x and 1.x are NOT compatible — see §10).
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfigVersion.cmake
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

# 7. Install the two generated config files.
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/emc)

# 8. (Optional but nice) Also export from the build tree so a sibling project can
#    find_package(emc) against the build dir without installing.
export(EXPORT emcTargets
    NAMESPACE emc::
    FILE ${CMAKE_CURRENT_BINARY_DIR}/emcTargets.cmake)
```

The package config template **must re-find mp-units**, because `emc::emc` carries a `PUBLIC` link to
`mp-units::mp-units`; without re-finding it, the consumer's `find_package(emc)` succeeds but the link step
explodes on the missing imported target:

```cmake
# cmake/emcConfig.cmake.in
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# emc::emc links mp-units::mp-units PUBLIC -> the consumer must be able to resolve it too.
find_dependency(mp-units CONFIG)

include("${CMAKE_CURRENT_LIST_DIR}/emcTargets.cmake")

check_required_components(emc)
```

> [!WARNING]
> Use `find_dependency`, not `find_package`, in the config. `find_dependency` forwards `REQUIRED` /
> `QUIET` / version from the outer `find_package(emc ...)` call and fails the whole lookup cleanly if
> mp-units is missing — the correct propagation semantics for a transitive public dependency.

After install the layout is:

```text
<prefix>/
├── include/emc/...                       # public headers + generated export.hpp
└── lib/
    ├── libemc.a   (or libemc.so.0.1.0)
    └── cmake/emc/
        ├── emcConfig.cmake               # entry point for find_package(emc)
        ├── emcConfigVersion.cmake        # SameMajorVersion check
        └── emcTargets.cmake              # defines imported emc::emc
```

---

## 5. `CMakePresets.json`

Presets pin generator, build dir, flags, and the warnings/sanitizer policy so every developer and CI
runner configures identically. Schema v6 (CMake ≥ 3.28).

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 28, "patch": 0 },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
        "EMC_BUILD_TESTS": "ON",
        "EMC_BUILD_EXAMPLES": "ON",
        "EMC_WARNINGS_AS_ERRORS": "ON"
      }
    },
    {
      "name": "debug",
      "displayName": "Debug (static, warnings-as-errors)",
      "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "BUILD_SHARED_LIBS": "OFF"
      }
    },
    {
      "name": "release",
      "displayName": "Release (shared, optimized)",
      "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "BUILD_SHARED_LIBS": "ON"
      }
    },
    {
      "name": "asan",
      "displayName": "Debug + AddressSanitizer/UBSanitizer",
      "inherits": "debug",
      "cacheVariables": {
        "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all",
        "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined",
        "CMAKE_SHARED_LINKER_FLAGS": "-fsanitize=address,undefined"
      }
    }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" },
    { "name": "asan",    "configurePreset": "asan" }
  ],
  "testPresets": [
    {
      "name": "debug",
      "configurePreset": "debug",
      "output": { "outputOnFailure": true },
      "execution": { "noTestsAction": "error", "stopOnFailure": false }
    },
    {
      "name": "asan",
      "configurePreset": "asan",
      "output": { "outputOnFailure": true },
      "environment": { "UBSAN_OPTIONS": "print_stacktrace=1", "ASAN_OPTIONS": "detect_leaks=1" }
    }
  ]
}
```

### Example usage

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset asan && cmake --build --preset asan && ctest --preset asan
```

> [!NOTE]
> ASan/UBSan is high-value here because the formulas use `std::pow`, `std::log`, `std::exp`, divisions,
> and `std::mdspan` indexing (e.g. the microstrip bidirectional solve in doc 06, the 12-mode enclosure
> loop in the rectangular-enclosure calculator). UBSan catches divide-by-zero and domain errors at their
> origin rather than letting them propagate as `inf`/`nan`.

---

## 6. Quality tooling

### 6.1 Warnings interface library (`cmake/CompilerWarnings.cmake`)

A dedicated `INTERFACE` target carries the warning policy so it applies to the lib *and* tests *and*
examples uniformly, and is exported (consumers are never forced into `-Werror`, but in-tree builds are
strict):

```cmake
# cmake/CompilerWarnings.cmake
add_library(emc_project_warnings INTERFACE)

set(_emc_gcc_clang
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    -Wunused -Woverloaded-virtual -Wnull-dereference
    -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)

set(_emc_msvc /W4 /permissive- /w14640 /w14242 /w14254 /w14263)

target_compile_options(emc_project_warnings INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:${_emc_gcc_clang}>
    $<$<CXX_COMPILER_ID:MSVC>:${_emc_msvc}>)

if(EMC_WARNINGS_AS_ERRORS)
    target_compile_options(emc_project_warnings INTERFACE
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()
```

`-Wconversion`/`-Wsign-conversion` are explicitly on: EMC inputs mix lengths, frequencies, and unit
factors, and a narrowing or a bare-`double` escape now warns, actively policing that the unit-safety from
doc 03 is not bypassed.

`emc_project_warnings` is exported in §4's `install(TARGETS ...)` only so the export set is
self-consistent; it is an `INTERFACE` lib whose `-Werror` is gated behind `EMC_WARNINGS_AS_ERRORS`
(default OFF for non-top-level), so it never raises consumers' own warning levels.

### 6.2 `.clang-format` and `.clang-tidy` (repo root)

Pointers, not full configs (style is owned by the repo, not this doc):

```text
# .clang-format  (top of file)
BasedOnStyle: LLVM
Standard: c++23
ColumnLimit: 100
PointerAlignment: Left
```

```text
# .clang-tidy  (top of file)
Checks: >
  -*,
  bugprone-*,
  cppcoreguidelines-*,
  modernize-*,
  performance-*,
  readability-*,
  misc-include-cleaner,
  -modernize-use-trailing-return-type
WarningsAsErrors: 'bugprone-*'
HeaderFilterRegex: 'include/emc/.*'
```

`misc-include-cleaner` doubles as the layering enforcer from doc 01: a `src/component/*.cpp` that includes
`emc/basic/...` shows up as an unexpected include and fails review.

### 6.3 `compile_commands.json`

`CMAKE_EXPORT_COMPILE_COMMANDS ON` (set in §1 and the preset) emits
`build/<preset>/compile_commands.json`. Point clangd and clang-tidy at it:

```bash
ln -sf build/debug/compile_commands.json compile_commands.json   # one-time, repo root
clang-tidy -p build/debug src/component/microstrip_trace.cpp
```

---

## 7. Folder layout recap and source→target mapping

Consistent with `01-architecture-and-layout.md`. Public headers and compiled sources mirror the same
Foundation / Domain / Facade structure:

```text
emcpp/
├── CMakeLists.txt                 # §1
├── CMakePresets.json              # §5
├── vcpkg.json                     # §11 (optional)
├── conanfile.py                   # §11 (optional)
├── .clang-format  .clang-tidy     # §6.2
├── cmake/
│   ├── Dependencies.cmake         # §2 (mp-units find-or-fetch)
│   ├── CompilerWarnings.cmake     # §6.1
│   ├── Install.cmake              # §4
│   └── emcConfig.cmake.in         # §4
├── include/emc/                   # PUBLIC API (installed)
│   ├── export.hpp                 # GENERATED into build tree, installed from there (§3)
│   ├── error.hpp                  # Foundation
│   ├── units/...   constants/...   materials/...
│   ├── basic/skin_depth.hpp  decibel.hpp  antenna.hpp
│   ├── converter/...  component/...  prediction/...
│   ├── shielding/...  filtering/...  cabling/...  grounding/...  testing/...
│   └── emc.hpp                    # Facade umbrella (include-only)
├── src/                           # COMPILED translation units (target_sources, §1)
│   ├── error.cpp
│   ├── units/parse.cpp  constants/constants.cpp  materials/material_db.cpp
│   ├── basic/skin_depth.cpp ...
│   └── component/microstrip_trace.cpp ...
├── tests/                         # doc 09 — separate target, links emc::emc + Catch2
│   ├── CMakeLists.txt
│   └── data/                      # hand-computed / textbook reference vectors
└── examples/
    └── CMakeLists.txt
```

Mapping rules:

- One header `include/emc/<category>/<name>.hpp` ↔ one source `src/<category>/<name>.cpp` ↔ one entry in
  `target_sources(emc PRIVATE ...)`. Adding a doc-07 calculator is exactly: add the pair, add the one
  `target_sources` line, add a test (doc 09). No glob means the new file is built deterministically.
- Header-only calculators (pure `constexpr calculate()`) still get a `.cpp` if they need a compiled
  validate/parse boundary or an explicit instantiation anchor; otherwise the `.cpp` can be empty but is
  kept for symmetry and to give the linker a TU per calculator.
- Foundation `.cpp` files are few: `error.cpp` (message formatting), `parse.cpp` (string→quantity),
  `material_db.cpp` (the lookup over the constexpr table). Constants are header `constexpr`; the
  `constants.cpp` exists only to anchor any ODR-used definition.

---

## 8. Examples target

The `examples/` subdirectory links `emc::emc` and demonstrates the public API through a generic front end
that prints results with `std::print` — no GUI toolkit involved. Because examples link the same
`emc::emc` alias as a `find_package(emc)` consumer, building them in-tree is a standing smoke test of the
public surface.

### Example usage

```c++
// examples/skin_depth.cpp
#include <emc/basic/skin_depth.hpp>
#include <emc/materials/material_db.hpp>
#include <mp-units/systems/si.hpp>
#include <print>

int main() {
    using namespace mp_units;
    using namespace mp_units::si::unit_symbols;

    const emc::basic::SkinDepthInput in{
        .frequency   = 1.0 * MHz,
        .conductor   = emc::materials::copper(),
    };

    if (const auto r = emc::basic::calculate(in)) {
        std::println("skin depth = {}", r->skin_depth);
    } else {
        std::println("input error: {}", r.error().message());
    }
}
```

```cmake
# examples/CMakeLists.txt
add_executable(emc_example_skin_depth skin_depth.cpp)
target_link_libraries(emc_example_skin_depth PRIVATE emc::emc emc_project_warnings)
```

> [!NOTE]
> The front end is intentionally framework-agnostic. A calculator returns a `Result` of mp-units
> quantities; rendering it (console, a desktop UI, a web service) is a presentation concern that lives
> entirely outside `emc` and links against `emc::emc` like any other consumer.

---

## 9. Keeping the library GUI-free (CI gate)

### 9.1 The CI grep gate (build-side enforcement)

To keep the library free of any GUI dependency permanently (doc 01's one invariant), CI greps for any GUI
symbols over the *library* tree before configuring:

```bash
# fails the build if any emc source/header references a GUI toolkit symbol
if grep -rEni '#include[[:space:]]*<[^>]*(gtk|gui|window|x11|cocoa|wx)' include/ src/; then
  echo "ERROR: GUI-toolkit symbols found in the GUI-free emc library" >&2
  exit 1
fi
```

The library exposes only `emc::*` calculators returning `std::expected<Result, Error>` over mp-units
quantities; any front end links `emc::emc` and renders results itself (§8), so no GUI symbol ever belongs
in `include/` or `src/`.

---

## 10. ABI & versioning

- **SemVer on the package**: `project(emc VERSION MAJOR.MINOR.PATCH)`. The version file uses
  `COMPATIBILITY SameMajorVersion`, so a consumer asking `find_package(emc 0.1)` accepts `0.x` but rejects
  `1.0` (during 0.x, ABI may break freely — appropriate for a pre-1.0 library being shaped by the doc-07
  work-list).
- **SOVERSION = MAJOR** on the shared lib (`set_target_properties(... SOVERSION ${PROJECT_VERSION_MAJOR})`)
  so the runtime linker tracks ABI by major version (`libemc.so.0`).
- **ABI surface is intentionally tiny** thanks to default-hidden visibility (§3): only `EMC_API`
  functions are exported, so most refactors of the header-only `constexpr` calculators are *not* ABI
  breaks at all — they recompile into the consumer. The risk surface is the handful of compiled boundary
  functions (`parse_*`, `material_*`, error formatting).
- **mp-units is a public dependency**, so an mp-units major bump is an `emc` ABI concern; the pinned
  `GIT_TAG` (§2) and `find_dependency(mp-units)` (§4) make the coupling explicit and reproducible.

---

## 11. C++23 now, modules later (forward-looking callout)

> **Headers + `.cpp` today; modules are a deliberate non-goal for v1.** The plan targets a traditional
> compiled library because mp-units, Catch2, and tooling (clang-tidy, clangd) are most robust in
> header+`.cpp` mode across GCC 14 / Clang 18 / MSVC today. When the ecosystem settles, the path to
> modules is mechanical and *additive*:
>
> - bump nothing — `cmake_minimum_required(3.28)` already supports `import std` and `CXX_MODULES`;
> - add a `FILE_SET CXX_MODULES` to the `emc` target listing `*.cppm` interface units;
> - mp-units already ships a modules build (we keep `MP_UNITS_BUILD_CXX_MODULES OFF` today, flip it on);
> - keep the `include/emc/*.hpp` headers as a *parallel* facade for consumers who can't use modules yet.
>
> No public API or namespace changes are implied. This callout exists so the directory layout (§7) and the
> `.cpp`-per-calculator rule (one TU per module interface later) do not have to change when modules
> arrive. Other C++26 forward-looking items (reflection-driven enum↔string, contracts replacing some
> `validate()` bodies) are discussed in docs 02 and 05.

---

## 12. Reproducible dependencies (optional sketches)

For consumers/CI that want lockfile-style reproducibility instead of `FetchContent`, the project can ship
either manifest; both resolve the *same* mp-units the `find_package` branch in §2 picks up.

**vcpkg manifest (`vcpkg.json`):**

```json
{
  "name": "emc",
  "version-semver": "0.1.0",
  "description": "Modern-C++ EMC engineering calculation library",
  "dependencies": [
    "mp-units"
  ],
  "features": {
    "tests":   { "description": "Build tests",    "dependencies": ["catch2"] },
    "enum":    { "description": "magic_enum boundary helpers", "dependencies": ["magic-enum"] }
  },
  "builtin-baseline": "<pin-a-vcpkg-commit-sha-here>"
}
```

Configure with the vcpkg toolchain:

```bash
cmake --preset debug -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

**Conan recipe sketch (`conanfile.py`):**

```python
from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout

class EmcConan(ConanFile):
    name = "emc"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False]}
    default_options = {"shared": False}
    exports_sources = "CMakeLists.txt", "cmake/*", "include/*", "src/*"

    def requirements(self):
        self.requires("mp-units/2.5.0")        # PUBLIC -> propagates to consumers

    def build_requirements(self):
        self.test_requires("catch2/3.7.1")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeToolchain(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()

    def package_info(self):
        self.cpp_info.libs = ["emc"]
        self.cpp_info.requires = ["mp-units::mp-units"]   # transitive public dep
```

Both manifests pin exact versions (`builtin-baseline` / `mp-units/2.5.0`) so a checkout from any machine
builds the *same* `emc` — the package-manager analogue of the `GIT_TAG v2.5.0` pin in §2, mattering
because mp-units is a `PUBLIC` dependency that participates in `emc`'s ABI (§10).

---

## Cross-references

- `01-architecture-and-layout.md` — the layer/namespace/directory structure these targets implement and
  the GUI-free invariant the §9.1 CI gate enforces.
- `03-quantities-and-units-mp-units.md` — why mp-units links `PUBLIC` and must be `find_dependency`'d in
  the package config (§2, §4).
- `04-constants-and-material-database.md` — the `constants.cpp`/`material_db.cpp` compiled units.
- `05-error-handling-and-validation.md` — the `error.cpp` boundary unit and the `std::expected` model.
- `06-calculator-design-pattern.md` — the per-calculator header/`.cpp` pair each `target_sources` line
  corresponds to.
- `07-calculator-inventory.md` — the work-list whose ~52 items each add one `target_sources` entry.
- `09-testing-and-golden-vectors.md` — the `tests/` subdirectory target, Catch2 acquisition (§2),
  hand-computed/textbook reference vectors under `tests/data/`, and the ASan/UBSan `ctest` preset (§5).
- `10-roadmap.md` — sequencing of the calculator work-list.
