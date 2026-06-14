# Build System (Modern CMake, Compiled Library)

> Purpose: specify the complete CMake build for the Qt-free `emc` library as a **traditional compiled
> library** (static and/or shared) named `emc` exporting the alias `emc::emc`, including dependency
> acquisition (mp-units), shared-library symbol visibility, install/export and package config so
> downstream code can `find_package(emc)`, presets, quality tooling, and how the existing Qt app rewires
> onto it.

---

## 0. Reading this document

This is the *mechanical* document. It tells you exactly what CMake to write so that the structure
defined in `01-architecture-and-layout.md` (Foundation / Domain / Facade layers, the `emc::*`
namespaces) and the dependencies defined in `03-quantities-and-units-mp-units.md` (mp-units) and
`05-error-handling-and-validation.md` (`std::expected`, C++23) actually compile, install, and are
consumable.

Locked decisions this document obeys:

- **C++23 baseline** — `target_compile_features(emc PUBLIC cxx_std_23)`; no `CMAKE_CXX_STANDARD`
  global, no compiler-specific `-std=` strings.
- **Compiled library, not header-only, not modules-first** — public headers in `include/emc/`,
  compiled translation units in `src/`, built into a real `add_library(emc ...)` target with
  `install()`/`export()` and a generated package config. A short callout (§11) notes C++20 modules as a
  *future* option only.
- **mp-units is the units layer** — pulled with `find_package(mp-units CONFIG)` and a `FetchContent`
  fallback, propagated `PUBLIC` because it appears in `emc`'s public headers (every quantity type).
- **No Qt anywhere in the library build** — there is deliberately no `find_package(Qt6)` in this
  project. A CI grep gate (§9.4) enforces it.

---

## 1. What changes versus the old build (contrast)

The source app's `CMakeLists.txt` (`/Users/sufuk/CLionProjects/emc-prediction/CMakeLists.txt`) is a
single 216-line file whose every pathology is a thing this plan removes. A side-by-side:

| Old `NinjaEMC` `CMakeLists.txt`                                                         | New `emc` build                                                                  |
|----------------------------------------------------------------------------------------|----------------------------------------------------------------------------------|
| `cmake_minimum_required(VERSION 3.16)`                                                  | `cmake_minimum_required(VERSION 3.28)` (good C++23 + clean `import std` story)    |
| `set(CMAKE_CXX_STANDARD 20)` global mutable var                                         | `target_compile_features(emc PUBLIC cxx_std_23)` (requirement *propagates*)       |
| `CMAKE_AUTOUIC/AUTOMOC/AUTORCC ON`                                                      | gone — no Qt, no moc, no `.ui`, no `.qrc`                                          |
| Hard-coded per-OS `find_path(Qt5_DIR PATHS E:/Qt/...)` (lines 28–134)                   | gone — no absolute machine paths; deps via `find_package`/`FetchContent`          |
| `find_package(Qt6 COMPONENTS Widgets Gui Core Svg ...)`                                 | `find_package(mp-units CONFIG)` only                                              |
| `file(GLOB_RECURSE SOURCE_FILES src/*.cpp)`                                             | explicit `target_sources(emc PRIVATE src/...)` (GLOB does not re-trigger CMake)   |
| `add_executable(NinjaEMC ...)`                                                          | `add_library(emc ...)` + `add_library(emc::emc ALIAS emc)`                        |
| `target_include_directories(... PUBLIC ${header_dir_list})` (every header dir exposed)  | one public root via `$<BUILD_INTERFACE>`/`$<INSTALL_INTERFACE>`                    |
| `add_compile_definitions(TEST_MODE)` toggling in-widget CSV harness                     | tests are a separate target; goldens consumed by tests, not the lib (doc 09)      |
| no `install()`, no `export()`, no package config                                        | full `install(TARGETS ... EXPORT)` + `emcConfig.cmake` so `find_package(emc)`     |
| no warnings flags, no sanitizers, no presets                                            | `-Wall -Wextra -Wpedantic -Werror`, ASan/UBSan preset, `CMakePresets.json`        |

Two of these matter enough to call out explicitly:

**`GLOB_RECURSE` is removed on purpose.** The old build globs `src/*.cpp`. CMake evaluates the glob
*once* at configure time; adding a new calculator `.cpp` does **not** re-run CMake, so the new file is
silently not built until someone manually reconfigures. For a library where the work-list (doc 07) adds
~52 calculators incrementally, that is a footgun. We list sources explicitly via `target_sources`.
(`CONFIGURE_DEPENDS` exists, but it is best-effort, slows every build, and is discouraged for installed
libraries — explicit lists are the documented recommendation.)

**`CMAKE_CXX_STANDARD 20` global is removed on purpose.** A global variable does not travel with the
target; a consumer linking `emc::emc` would not automatically get C++23 turned on. Because `emc`'s
public headers *require* C++23 (they use `std::expected`, `std::format`, mp-units' deducing-this), the
requirement must be a `PUBLIC` *usage requirement* on the target (`target_compile_features(... PUBLIC
cxx_std_23)`), which propagates to every consumer transitively.

---

## 2. Top-level `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.28)
# 3.28 chosen deliberately:
#  - solid C++23 feature-level handling (cxx_std_23) across GCC 14 / Clang 18 / MSVC 19.4x,
#  - first CMake with a supported `import std` path (we do NOT use it yet — see §11 — but we
#    want the floor high enough that turning modules on later is not a version bump),
#  - CMakePresets schema v6 (used in §6).

project(emc
    VERSION 0.1.0
    DESCRIPTION "Qt-free modern-C++ EMC engineering calculation library"
    HOMEPAGE_URL "https://github.com/sufuk/emcpp"
    LANGUAGES CXX)

# ---- Project-level guard rails -------------------------------------------------------------
# Refuse in-source builds (the old app built straight into the source tree).
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a build/ directory or a preset.")
endif()

# Generate compile_commands.json for clang-tidy / clangd / IDEs (see §9.3).
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

# ---- Dependencies (see §2 detail in §2.1 below) --------------------------------------------
include(cmake/Dependencies.cmake)   # brings in mp-units (+ test deps when EMC_BUILD_TESTS)

# ---- The library target --------------------------------------------------------------------
add_library(emc)            # STATIC or SHARED chosen by BUILD_SHARED_LIBS
add_library(emc::emc ALIAS emc)   # consumers always say emc::emc, never bare emc

# C++23 as a PUBLIC usage requirement (propagates to consumers — see §1).
target_compile_features(emc PUBLIC cxx_std_23)

# Explicit source list (NOT glob — see §1). Grouped to mirror doc 01's layer/category layout.
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

# mp-units is in emc's PUBLIC headers (every quantity type) -> PUBLIC link (see §2.1).
target_link_libraries(emc PUBLIC mp-units::mp-units)

# Warnings & version/soname metadata.
include(cmake/CompilerWarnings.cmake)   # sets up the -Wall -Wextra ... interface lib (see §9)
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

### 2.1 Notes on the snippet

- **`add_library(emc)` with no `STATIC`/`SHARED`** lets `BUILD_SHARED_LIBS` decide. The plan supports
  both (the user asked for "static and/or shared"); a consumer or packager flips one cache var. Because
  visibility is default-hidden + `EMC_API` (see §3), the *static* build behaves identically and the
  *shared* build exports a clean, minimal symbol set.
- **`emc::emc` alias** is created at configure time so in-tree examples/tests link the same name a
  downstream `find_package(emc)` consumer uses. This means `examples/` code is byte-for-byte the same
  whether built in-tree or against the installed package — a cheap correctness check.
- **`mp-units::mp-units` is `PUBLIC`**, not `PRIVATE`. Every public `emc` header includes mp-units
  types in function signatures (e.g. `quantity<isq::frequency[si::hertz]>`), so consumers need
  mp-units' include dirs and compile flags transitively. `PUBLIC` propagates them; `PRIVATE` would
  compile `emc` but break every consumer with "mp-units/... not found". This is also why the package
  config must `find_dependency(mp-units)` (§4).
- **`PROJECT_IS_TOP_LEVEL`** (CMake ≥ 3.21) drives test/example defaults: ON when `emc` is the root
  project, OFF when it is pulled in via `add_subdirectory`/`FetchContent` by the Qt app — so consuming
  the library does not drag in its tests.

---

## 3. Dependencies (`cmake/Dependencies.cmake`)

Pattern: prefer a system/`find_package` copy; fall back to `FetchContent` so a clean checkout builds
with zero manual setup. This is the modern "find-or-fetch" idiom and replaces the old build's hard-coded
`find_path(Qt5_DIR PATHS E:/Qt/5.15.2/...)` machine-specific paths entirely.

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

> **PUBLIC vs PRIVATE rule of thumb for this project:** a dependency goes `PUBLIC` iff it appears in a
> public header under `include/emc/`. mp-units → `PUBLIC` (quantity types in every signature). Catch2 →
> test target only. magic_enum → `PRIVATE` (used only inside `.cpp` parse helpers). This rule is what
> keeps the package config's `find_dependency` list minimal and correct.

---

## 4. Shared-library symbol visibility (`include/emc/export.hpp`)

A shared `emc` defaults to **hidden** visibility (set in §2: `CMAKE_CXX_VISIBILITY_PRESET hidden` +
`VISIBILITY_INLINES_HIDDEN`). Only symbols explicitly marked `EMC_API` are exported. This is the modern
default-hidden discipline: smaller export tables, faster load, no accidental ABI surface, and it makes
Windows (`__declspec(dllexport/dllimport)`) and ELF/Mach-O (`__attribute__((visibility("default")))`)
behave identically from one macro.

We generate the macro with `generate_export_header` (called in §2), which writes
`include/emc/export.hpp` in the build tree. Generated content is equivalent to:

```cpp
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

(For a static build CMake compiles with `-DEMC_STATIC_DEFINE`, so `EMC_API` expands to nothing — the
exact same source works in both modes. We add `EMC_STATIC_DEFINE` to the static target's
`COMPILE_DEFINITIONS` in `Install.cmake` / the static branch.)

### 4.1 Where `EMC_API` goes (and where it must NOT)

Most of `emc` is templates and `constexpr`, which are **header-only and must NOT carry `EMC_API`** —
exporting a template instantiation is meaningless and on MSVC actively wrong. Mark only the
**non-template, non-inline functions that are compiled in `src/*.cpp`** — chiefly the few runtime
boundary helpers (string→quantity parsing, the material lookup, error-message formatting):

```cpp
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

```cpp
// include/emc/basic/skin_depth.hpp  -- a pure calculator: NO EMC_API.
// calculate() is constexpr/inline and header-resolved; nothing to export.
namespace emc::basic {

struct SkinDepthInput  { /* mp-units quantities, designated-init */ };
struct SkinDepthResult { /* ... */ };

[[nodiscard]] constexpr std::expected<SkinDepthResult, Error>
calculate(const SkinDepthInput& in) noexcept;   // no EMC_API: header-only

}  // namespace emc::basic
```

> **Why default-hidden + a generated macro instead of the old "export everything" default:** the source
> app never built a library, so it never faced this — but its `target_include_directories(... PUBLIC
> ${header_dir_list})` exposed *every* header directory as public API. Default-hidden visibility is the
> library-shaped equivalent of "expose only what you mean to": the public surface is exactly the set of
> `EMC_API`-marked functions plus the header-only calculators, nothing else.

---

## 5. Install & package config (`cmake/Install.cmake` + `cmake/emcConfig.cmake.in`)

The goal: after `cmake --install`, a completely separate project does

```cmake
find_package(emc 0.1 REQUIRED)
target_link_libraries(myapp PRIVATE emc::emc)
```

and gets the headers, the compiled lib, the C++23 requirement, *and* mp-units transitively — with zero
manual include/link flags.

```cmake
# cmake/Install.cmake
include(CMakePackageConfigHelpers)

# 1. Install the library + record it in an export set, attaching include dirs from §2.
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

The package config template — **must re-find mp-units**, because `emc::emc` carries a `PUBLIC` link to
`mp-units::mp-units`; without re-finding it, the consumer's `find_package(emc)` succeeds but the link
step explodes on the missing imported target:

```cmake
# cmake/emcConfig.cmake.in
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# emc::emc links mp-units::mp-units PUBLIC -> the consumer must be able to resolve it too.
find_dependency(mp-units CONFIG)

include("${CMAKE_CURRENT_LIST_DIR}/emcTargets.cmake")

check_required_components(emc)
```

> **Why `find_dependency` and not `find_package` in the config:** `find_dependency` forwards `REQUIRED`
> / `QUIET` / version from the outer `find_package(emc ...)` call, and fails the whole lookup cleanly if
> mp-units is missing — the right propagation semantics for a transitive public dependency. This is the
> mechanical realization of doc 03's note that "mp-units propagates `PUBLIC`."

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

## 6. `CMakePresets.json`

Presets pin generator, build dir, flags and the warnings/sanitizer policy so every developer and CI
runner configures identically — replacing the old habit of remembering ad-hoc `-D` flags. Schema v6
(CMake ≥ 3.28).

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

Usage:

```text
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset asan && cmake --build --preset asan && ctest --preset asan
```

> ASan/UBSan is high-value here precisely because the formulas use `std::pow`, `std::log`, `std::exp`,
> divisions, and `std::mdspan` indexing (e.g. the microstrip bidirectional solve in doc 06, the
> 12-mode enclosure loop in `RectangularEnclosureWidget.cpp`). UBSan catches divide-by-zero / domain
> errors that the old code would silently turn into `inf`/`nan` and push into a Qt spinbox.

---

## 7. Quality tooling

### 7.1 Warnings interface library (`cmake/CompilerWarnings.cmake`)

A dedicated `INTERFACE` target carries the warning policy so it can be applied to the lib *and* tests
*and* examples uniformly, and exported (so consumers are never forced into `-Werror`, but in-tree builds
are strict):

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

`-Wconversion`/`-Wsign-conversion` are explicitly on: the old code mixed `qreal`, `int` spinbox values,
and unit factors freely (the `*39.37` m→inch literals, the `if/else` gauge tables in
`StandardGaugeWireWidget.cpp`). With mp-units a narrowing or a bare-double escape now warns, so these
flags actively police that the unit-safety from doc 03 is not bypassed.

> Note: `emc_project_warnings` is exported in §5's `install(TARGETS ...)` only so the export set is
> self-consistent; it is an `INTERFACE` lib with no usage requirements that affect consumers' own
> warning levels (the `-Werror` is gated behind `EMC_WARNINGS_AS_ERRORS`, default OFF for non-top-level).

### 7.2 `.clang-format` and `.clang-tidy` (repo root)

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

`misc-include-cleaner` doubles as the layering enforcer from doc 01: a `src/component/*.cpp` that
includes `emc/basic/...` shows up as an unexpected include and fails review.

### 7.3 `compile_commands.json`

`CMAKE_EXPORT_COMPILE_COMMANDS ON` (set in §2 and the preset) emits `build/<preset>/compile_commands.json`.
Symlink/point clangd and clang-tidy at it:

```text
ln -sf build/debug/compile_commands.json compile_commands.json   # one-time, repo root
clang-tidy -p build/debug src/component/microstrip_trace.cpp
```

---

## 8. Folder layout recap and source→target mapping

Consistent with `01-architecture-and-layout.md`. Public headers and compiled sources mirror the same
Foundation / Domain / Facade structure:

```text
emcpp/
├── CMakeLists.txt                 # §2
├── CMakePresets.json              # §6
├── vcpkg.json                     # §12 (optional)
├── conanfile.py                   # §12 (optional)
├── .clang-format  .clang-tidy     # §7.2
├── cmake/
│   ├── Dependencies.cmake         # §3 (mp-units find-or-fetch)
│   ├── CompilerWarnings.cmake     # §7.1
│   ├── Install.cmake              # §5
│   └── emcConfig.cmake.in         # §5
├── include/emc/                   # PUBLIC API (installed)
│   ├── export.hpp                 # GENERATED into build tree, installed from there (§4)
│   ├── error.hpp                  # Foundation
│   ├── units/...   constants/...   materials/...
│   ├── basic/skin_depth.hpp  decibel.hpp  antenna.hpp
│   ├── converter/...  component/...  prediction/...
│   ├── shielding/...  filtering/...  cabling/...  grounding/...  testing/...
│   └── emc.hpp                    # Facade umbrella (include-only)
├── src/                           # COMPILED translation units (target_sources, §2)
│   ├── error.cpp
│   ├── units/parse.cpp  constants/constants.cpp  materials/material_db.cpp
│   ├── basic/skin_depth.cpp ...
│   └── component/microstrip_trace.cpp ...
├── tests/                         # doc 09 — separate target, links emc::emc + Catch2
│   ├── CMakeLists.txt
│   └── data/*.csv                 # the 52 golden fixtures, moved out of resources/
└── examples/
    └── CMakeLists.txt
```

Mapping rules:

- One header `include/emc/<category>/<name>.hpp` ↔ one source `src/<category>/<name>.cpp` ↔ one entry
  in `target_sources(emc PRIVATE ...)`. Adding a doc-07 calculator is exactly: add the pair, add the
  one `target_sources` line, add a test (doc 09). No glob means the new file is built deterministically.
- Header-only calculators (pure `constexpr calculate()`) still get a `.cpp` if they need a compiled
  validate/parse boundary or just an explicit instantiation anchor; otherwise the `.cpp` can be empty
  but is kept for symmetry and to give the linker a TU per calculator.
- Foundation `.cpp` files are few: `error.cpp` (message formatting), `parse.cpp` (string→quantity),
  `material_db.cpp` (the lookup over the constexpr table). Constants are header `constexpr`; the
  `constants.cpp` exists only to anchor any ODR-used definition.

---

## 9. Consuming from the existing Qt app

The app keeps its own Qt build (it stays a Qt executable) and simply *adds* `emc` as a dependency.
Two acquisition modes:

**A. Installed package (preferred for releases):**

```cmake
# emc-prediction/CMakeLists.txt  (the app)
find_package(emc 0.1 REQUIRED)              # pulls emc::emc + mp-units transitively
# ... existing Qt setup unchanged ...
target_link_libraries(NinjaEMC PRIVATE
    Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Svg Qt6::SvgWidgets
    emc::emc)                               # <-- the only new line
```

**B. In-tree (preferred during migration):**

```cmake
# Pull the sibling library directly while iterating on both repos.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../emcpp emcpp-build EXCLUDE_FROM_ALL)
target_link_libraries(NinjaEMC PRIVATE emc::emc)
```

(`PROJECT_IS_TOP_LEVEL` is false in mode B, so `emc`'s tests/examples stay off by default — the app
build does not also build the library's test suite.)

What the app *deletes* once linked (this is the payoff — the pain points from the shared context vanish):

```diff
  // src/Utilites/HelperTypes.h  (app)
- #define PI 3.14                                 // precision BUG
- #define SPEEDOFLIGHT 300000000.0                // imprecise c
- #define PLANCK_CONSTANT 6.62606957e-34
+ #include <emc/constants.hpp>                    // emc::constants::pi, ::c, ::h (exact)

  // src/BasicCalculations/SkinDepth/SkinDepthWidget.h  (and 7+ other files)
- qreal mu0 = 4 * M_PI * 1e-7;                    // re-defined in 8+ files
+ // gone — use emc::constants::mu_0

  // 6 Inductance headers
- #define PermofFreeSpace ((4 * M_PI) / 10000000.0)   // copy-pasted x6
+ // gone — use emc::constants::mu_0
```

```diff
  // src/.../StandardGaugeWireWidget.cpp  (app)  -- the if/else unit chains + EXIT_FAILURE sentinel
- if (unit == "mm") factor = 1.0; else if (unit == "mils") factor = 0.0254; else ...   // ~35 lines
- qreal GetResistivity(Material m) { ...; return EXIT_FAILURE; }                        // sentinel
+ // gone — the widget now reads spinboxes into an emc::component::StandardGaugeWireInput
+ //        (mp-units quantities), calls emc::component::calculate(in), and on the
+ //        std::expected error branch shows the QMessageBox (UI stays in the UI).
```

The widget's button-clicked lambda shrinks to: read spinboxes → build the aggregate `Input` (designated
initializers, mp-units quantities) → call `emc::<cat>::calculate(in)` → on `std::expected` error show the
`QMessageBox` (now the *only* place Qt validation lives), on success write `Result` fields to the
spinboxes. All math, constants, unit conversion, material data, and validation move into `emc`. The full
before/after of a widget body is owned by doc 06; the build-side fact is simply: **+1 link line, and
hundreds of duplicated lines deleted.** Sequencing is in doc 10.

### 9.1 The anti-Qt CI gate (build-side enforcement)

To keep the library Qt-free permanently (doc 01's one invariant), CI runs a grep gate over the *library*
tree before configuring:

```bash
# fails the build if any emc source/header references Qt
if grep -rEn 'Q[A-Z][A-Za-z]*|qreal|QtWidgets|#include +<Q' include/ src/; then
  echo "ERROR: Qt symbols found in the Qt-free emc library" >&2
  exit 1
fi
```

---

## 10. ABI & versioning

- **SemVer on the package**: `project(emc VERSION MAJOR.MINOR.PATCH)`. The version file uses
  `COMPATIBILITY SameMajorVersion`, so a consumer asking `find_package(emc 0.1)` accepts `0.x` but
  rejects `1.0` (during 0.x, ABI may break freely — appropriate for a pre-1.0 library being shaped by
  the doc-07 work-list).
- **SOVERSION = MAJOR** on the shared lib (`set_target_properties(... SOVERSION ${PROJECT_VERSION_MAJOR})`)
  so the runtime linker tracks ABI by major version (`libemc.so.0`).
- **ABI surface is intentionally tiny** thanks to default-hidden visibility (§4): only `EMC_API`
  functions are exported, so most refactors of the header-only `constexpr` calculators are *not* ABI
  breaks at all — they recompile into the consumer. The risk surface is the handful of compiled boundary
  functions (`parse_*`, `material_*`, error formatting).
- **mp-units is a public dependency**, so an mp-units major bump is an `emc` ABI concern; the pinned
  `GIT_TAG` (§3) and `find_dependency(mp-units)` (§5) make the coupling explicit and reproducible.

---

## 11. C++23 now, modules later (forward-looking callout)

> **Headers + `.cpp` today; modules are a deliberate non-goal for v1.** The plan targets a traditional
> compiled library because mp-units, Catch2, and tooling (clang-tidy, clangd) are most robust in
> header+`.cpp` mode across GCC 14 / Clang 18 / MSVC today, and because the Qt app consuming us is also
> header-based. When the ecosystem settles, the migration is mechanical and *additive*:
>
> - bump nothing — `cmake_minimum_required(3.28)` already supports `import std` and `CXX_MODULES`;
> - add a `FILE_SET CXX_MODULES` to the `emc` target listing `*.cppm` interface units;
> - mp-units already ships a modules build (we keep `MP_UNITS_BUILD_CXX_MODULES OFF` today, flip it on);
> - keep the `include/emc/*.hpp` headers as a *parallel* facade for consumers who can't use modules yet.
>
> No public API or namespace changes are implied. This callout exists so the directory layout (§8) and
> the `.cpp`-per-calculator rule (one TU per module interface later) do not have to change when modules
> arrive. Other C++26 forward-looking items (reflection-driven enum↔string replacing magic_enum,
> contracts replacing some `validate()` bodies) are discussed in docs 02 and 05, not here.

---

## 12. Reproducible dependencies (optional sketches) and ABI note

For consumers/CI that want lockfile-style reproducibility instead of `FetchContent`, the project can
ship either manifest; both resolve the *same* mp-units the `find_package` branch in §3 picks up.

**vcpkg manifest (`vcpkg.json`):**

```json
{
  "name": "emc",
  "version-semver": "0.1.0",
  "description": "Qt-free modern-C++ EMC engineering calculation library",
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

Configure with vcpkg toolchain:

```text
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

Both manifests must pin exact versions (`builtin-baseline` / `mp-units/2.5.0`) so a checkout from any
machine builds the *same* `emc`. This is the package-manager analogue of the `GIT_TAG v2.5.0` pin in §3,
and it matters because mp-units is a `PUBLIC` dependency that participates in `emc`'s ABI (§10).

---

## Cross-references

- `01-architecture-and-layout.md` — the layer/namespace/directory structure these targets implement; the
  one Qt-free invariant the §9.1 CI gate enforces; the ABI/versioning stance expanded in §10.
- `03-quantities-and-units-mp-units.md` — why mp-units links `PUBLIC` and must be `find_dependency`'d in
  the package config (§3, §5).
- `04-constants-and-material-database.md` — the `constants.cpp`/`material_db.cpp` compiled units and the
  duplicated `#define PI`/`mu0`/material tables the app deletes in §9.
- `05-error-handling-and-validation.md` — the `error.cpp` boundary unit; `EXIT_FAILURE`/`QMessageBox`
  removal shown in the §9 diff.
- `06-calculator-design-pattern.md` — the per-calculator header/`.cpp` pair each `target_sources` line
  corresponds to; full widget before/after.
- `07-calculator-inventory.md` — the work-list whose ~52 items each add one `target_sources` entry.
- `09-testing-and-golden-vectors.md` — the `tests/` subdirectory target, Catch2 acquisition (§3), the 52
  CSV fixtures moved under `tests/data/`, and the ASan/UBSan `ctest` preset (§6).
- `10-migration-roadmap.md` — sequencing of the app rewire (in-tree `add_subdirectory` first, installed
  `find_package` at release) described in §9.
