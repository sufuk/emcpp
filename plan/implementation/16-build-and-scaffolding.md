# Build & Project Scaffolding — from zero to a building, testing `emc::emc` 🏗️

> Purpose: the **implementation-grade**, copy-paste-quality guide to scaffold the `emc` library from an
> empty directory — a traditional **compiled** C++23 library (`emc::emc`) built on **mp-units** — and get
> it configuring, building, and testing with one command.

`emc` is a from-scratch, GUI-free EMC engineering calculation library. It ships as a normal compiled
library (static **or** shared, your pick at configure time) exposing the imported target `emc::emc`. Public
headers live under `include/emc/`, compiled translation units mirror them under `src/`, and the whole thing
is driven by modern CMake (≥ 3.28) with `CMakePresets.json` so every developer and CI runner configures
identically.

This guide is self-contained: drop the files below into the tree exactly as shown, run the bootstrap
commands in §13, and you have a green build. Each later calculator (see
[`00-foundation-code.md`](00-foundation-code.md) and the per-category guides) is then *additive* — one
header, one `.cpp`, one `target_sources` line, one test.

> [!IMPORTANT]
> Two non-obvious, load-bearing choices run through this whole guide: sources are listed **explicitly**
> (never globbed), and the C++23 requirement is a **`PUBLIC`** property of the target (not a global
> `CMAKE_CXX_STANDARD`). Rationale in §3.1.

---

## 1. Project directory tree

The complete scaffold. `include/` is the public, installed API surface; `src/` mirrors it 1:1 with the
compiled bodies; `tests/` and `examples/` link `emc::emc` exactly as a downstream consumer would.

```text
emcpp/
├── CMakeLists.txt                      # top-level project() + the emc target (§3)
├── CMakePresets.json                   # configure/build/test presets (§10)
├── README.md
├── .gitignore                          # build dirs, caches, IDE noise (§11)
├── .clang-format                       # style (§11)
├── .clang-tidy                         # static analysis + layering lint (§11)
│
├── cmake/
│   ├── Dependencies.cmake              # mp-units + Catch2 find-or-fetch (§4)
│   ├── CompilerWarnings.cmake          # warnings INTERFACE target (§6)
│   ├── Install.cmake                   # install/export + package config (§9)
│   └── emcConfig.cmake.in              # find_package(emc) entry template (§9)
│
├── include/emc/                        # PUBLIC headers — installed, the API contract
│   ├── emc.hpp                         #   FACADE umbrella: re-includes everything
│   ├── export.hpp                      #   GENERATED into build tree, installed from there (§5)
│   ├── core/                           #   FOUNDATION (see 00-foundation-code.md)
│   │   ├── error.hpp                   #     emc::Error, emc::ErrorCode, emc::Result<T>, validators
│   │   ├── constants.hpp               #     emc::constants:: c, mu0, eps0, h, z0, pi
│   │   ├── units.hpp                   #     emc::units:: Frequency, Length, ... + Decibel/Dbm
│   │   ├── materials.hpp               #     emc::materials:: Material, properties(), named consts
│   │   └── calculator.hpp              #     emc::Calculator / ValidatedCalculator concepts
│   ├── basic/                          #   DOMAIN — category 1
│   │   ├── basic.hpp                   #     per-category facade
│   │   └── skin_depth.hpp              #     example calculator header
│   ├── converter/
│   │   ├── converter.hpp
│   │   └── vswr.hpp
│   ├── component/
│   │   ├── component.hpp
│   │   └── microstrip_trace.hpp
│   ├── prediction/
│   │   ├── prediction.hpp
│   │   └── rf_field.hpp
│   ├── shielding/
│   │   ├── shielding.hpp
│   │   └── shielding_effectiveness.hpp
│   ├── filtering/
│   │   ├── filtering.hpp
│   │   └── ferrite.hpp
│   ├── cabling/
│   │   ├── cabling.hpp
│   │   └── crosstalk.hpp
│   ├── grounding/
│   │   ├── grounding.hpp
│   │   └── microstrip_current.hpp
│   └── testing/
│       ├── testing.hpp
│       └── noise_figure.hpp
│
├── src/                                # COMPILED TUs (target_sources, §3) — mirrors include/
│   ├── core/
│   │   ├── error.cpp
│   │   └── materials.cpp
│   ├── basic/skin_depth.cpp
│   ├── converter/vswr.cpp
│   ├── component/microstrip_trace.cpp
│   ├── prediction/rf_field.cpp
│   ├── shielding/shielding_effectiveness.cpp
│   ├── filtering/ferrite.cpp
│   ├── cabling/crosstalk.cpp
│   ├── grounding/microstrip_current.cpp
│   └── testing/noise_figure.cpp
│
├── tests/                              # Catch2 v3 — links emc::emc (§7)
│   ├── CMakeLists.txt
│   ├── support/                        #   shared test helpers (see 00-foundation-code.md §6)
│   │   ├── csv.hpp                     #     emc::test::load_csv
│   │   └── approx.hpp                  #     emc::test::approx / WithinUnits
│   ├── reference/                      #   reference (golden) CSV vectors, one per calculator
│   │   ├── SkinDepth.csv
│   │   ├── MicrostripTrace.csv
│   │   └── ...
│   ├── core/
│   │   ├── constants_test.cpp
│   │   └── materials_test.cpp
│   ├── basic/skin_depth_test.cpp
│   ├── converter/vswr_test.cpp
│   ├── component/microstrip_trace_test.cpp
│   ├── prediction/rf_field_test.cpp
│   ├── shielding/shielding_effectiveness_test.cpp
│   ├── filtering/ferrite_test.cpp
│   ├── cabling/crosstalk_test.cpp
│   ├── grounding/microstrip_current_test.cpp
│   └── testing/noise_figure_test.cpp
│
├── examples/                           # usage demos — link emc::emc, no GUI (§8)
│   ├── CMakeLists.txt
│   └── skin_depth.cpp
│
└── .github/workflows/
    └── ci.yml                          # configure / build / ctest / sanitizers / GUI-free gate (§12)
```

> [!NOTE]
> Every public `include/emc/<cat>/<name>.hpp` has a matching `src/<cat>/<name>.cpp` at the **same relative
> path**, and a matching `tests/<cat>/<name>_test.cpp`. Navigation is trivial and CMake lists files
> predictably. The category folders shown above (`basic` … `testing`) carry one example header each; you
> add the rest of the inventory (see [`../07-calculator-inventory.md`](../07-calculator-inventory.md)) the
> same way.

---

## 2. The category set

The nine domain categories and the Foundation `core/` are the entire library. Each domain calculator is a
self-contained `(Input, Result, calculate())` triple in its own namespace; none depends on another's.

| #  | Category   | Folder / namespace              | Example leaf shown above        |
|----|------------|---------------------------------|---------------------------------|
| 1  | Basic      | `basic/` `emc::basic`           | `skin_depth.hpp`                |
| 2  | Converter  | `converter/` `emc::converter`   | `vswr.hpp`                      |
| 3  | Component  | `component/` `emc::component`   | `microstrip_trace.hpp`         |
| 4  | Prediction | `prediction/` `emc::prediction` | `rf_field.hpp`                  |
| 5  | Shielding  | `shielding/` `emc::shielding`   | `shielding_effectiveness.hpp`  |
| 6  | Filtering  | `filtering/` `emc::filtering`   | `ferrite.hpp`                   |
| 7  | Cabling    | `cabling/` `emc::cabling`       | `crosstalk.hpp`                 |
| 8  | Grounding  | `grounding/` `emc::grounding`   | `microstrip_current.hpp`       |
| 9  | Testing    | `testing/` `emc::testing`       | `noise_figure.hpp`             |

See [`../01-architecture-and-layout.md`](../01-architecture-and-layout.md) for the layering rule the
folders encode (Foundation → Domain → Facade, dependencies point down only).

---

## 3. Top-level `CMakeLists.txt`

`cmake_minimum_required(VERSION 3.28)` is the floor: solid `cxx_std_23` across GCC 14 / Clang 18 /
MSVC 19.4x, a supported `import std` path (for §15 later, not used now), and CMakePresets schema v6 (§10).

```cmake
cmake_minimum_required(VERSION 3.28)
project(emc
    VERSION 0.1.0
    DESCRIPTION "Modern-C++ EMC engineering calculation library"
    HOMEPAGE_URL "https://github.com/sufuk/emcpp"
    LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a build/ directory or a preset.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)          # for clang-tidy / clangd (§11)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)        # only EMC_API symbols exported (§5)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BUILD_SHARED_LIBS      "Build emc as a shared library" OFF)
option(EMC_BUILD_TESTS        "Build the emc test suite"      ${PROJECT_IS_TOP_LEVEL})
option(EMC_BUILD_EXAMPLES     "Build the emc usage examples"  ${PROJECT_IS_TOP_LEVEL})
option(EMC_WARNINGS_AS_ERRORS "Treat warnings as errors"      ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)
include(cmake/Dependencies.cmake)              # mp-units (+ test deps) — §4

add_library(emc)                               # STATIC or SHARED via BUILD_SHARED_LIBS
add_library(emc::emc ALIAS emc)                # consumers always say emc::emc

target_compile_features(emc PUBLIC cxx_std_23) # PUBLIC: propagates to consumers (§3.1)

target_sources(emc PRIVATE                     # explicit list, NOT glob (§3.1)
    src/core/error.cpp
    src/core/materials.cpp
    src/basic/skin_depth.cpp
    src/converter/vswr.cpp
    src/component/microstrip_trace.cpp
    src/prediction/rf_field.cpp
    src/shielding/shielding_effectiveness.cpp
    src/filtering/ferrite.cpp
    src/cabling/crosstalk.cpp
    src/grounding/microstrip_current.cpp
    src/testing/noise_figure.cpp
    # ... one .cpp per calculator, added as inventory items land (doc 07)
)

target_include_directories(emc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>   # generated export.hpp
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(emc PUBLIC mp-units::mp-units)        # in public headers -> PUBLIC (§4)

include(cmake/CompilerWarnings.cmake)
target_link_libraries(emc PRIVATE emc_project_warnings)

set_target_properties(emc PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}         # ABI tracked by major (§15)
    EXPORT_NAME emc)

include(GenerateExportHeader)
generate_export_header(emc
    BASE_NAME EMC
    EXPORT_MACRO_NAME EMC_API
    NO_EXPORT_MACRO_NAME EMC_LOCAL
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/emc/export.hpp)

if(EMC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(EMC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

include(cmake/Install.cmake)
```

### 3.1 Load-bearing choices

- **`add_library(emc)` with no kind** — `BUILD_SHARED_LIBS` selects static vs shared. Default-hidden
  visibility + `EMC_API` means both modes expose an identical, minimal public surface.
- **`emc::emc` alias** — in-tree examples/tests link the exact name a downstream `find_package(emc)`
  consumer uses, so `examples/` becomes a standing smoke test of the public surface.
- **`mp-units::mp-units` is `PUBLIC`** — every public header puts mp-units types in signatures
  (`quantity<isq::frequency[si::hertz]>`), so consumers need its includes/flags transitively. This is also
  why the package config must `find_dependency(mp-units)` (§9).
- **No `GLOB_RECURSE`** — a glob is evaluated once at configure time, so a newly added calculator `.cpp`
  would silently not build until someone reconfigures. With the inventory landing incrementally, sources
  are listed explicitly.
- **C++23 is `PUBLIC`, not global** — `CMAKE_CXX_STANDARD` does not travel with the target; a `PUBLIC`
  `target_compile_features` does, so every consumer compiles in the mode the headers require.
- **`PROJECT_IS_TOP_LEVEL`** drives test/example defaults — ON at the root, OFF when consumed via
  `add_subdirectory`/`FetchContent`, so the library never drags its tests into a consumer.

---

## 4. Dependencies (`cmake/Dependencies.cmake`)

Find-or-fetch: prefer a system `find_package` copy; fall back to `FetchContent` so a clean checkout builds
with zero setup. mp-units links `PUBLIC` (it appears in every public signature); Catch2 is test-only.

```cmake
# cmake/Dependencies.cmake
include(FetchContent)

find_package(mp-units CONFIG QUIET)
if(NOT mp-units_FOUND)
    FetchContent_Declare(mp-units
        GIT_REPOSITORY https://github.com/mpusz/mp-units.git
        GIT_TAG        v2.5.0          # pinned for reproducibility (§15)
        GIT_SHALLOW    TRUE
        SYSTEM)                        # treat headers as -isystem
    set(MP_UNITS_BUILD_CXX_MODULES OFF CACHE BOOL "" FORCE)
    set(MP_UNITS_BUILD_AS_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mp-units)
endif()

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
```

> [!NOTE]
> **PUBLIC vs PRIVATE rule:** a dependency goes `PUBLIC` iff it appears in a public header under
> `include/emc/`. mp-units → `PUBLIC` (quantity types in every signature); Catch2 → test target only. This
> keeps the package config's `find_dependency` list minimal and correct (§9).

---

## 5. Shared-library visibility (`include/emc/export.hpp`)

A shared `emc` defaults to **hidden** visibility (set in §3); only `EMC_API`-marked symbols are exported.
One macro makes Windows (`__declspec`) and ELF/Mach-O (`visibility("default")`) behave identically: smaller
export tables, faster load, no accidental ABI surface.

`generate_export_header` (called in §3) writes `include/emc/export.hpp` into the **build tree**, defining
`EMC_API` / `EMC_LOCAL`. For a static build CMake compiles with `-DEMC_STATIC_DEFINE`, so `EMC_API`
expands to nothing — the same source works in both modes. The generated file looks like this (you never
hand-write it; reproduced here so you know what `#include <emc/export.hpp>` pulls in):

```cpp
// include/emc/export.hpp  — GENERATED by generate_export_header (do not edit)
#ifndef EMC_API_H
#define EMC_API_H

#ifdef EMC_STATIC_DEFINE
#  define EMC_API
#  define EMC_LOCAL
#else
#  ifndef EMC_API
#    ifdef emc_EXPORTS          /* building the emc shared lib */
#      ifdef _WIN32
#        define EMC_API __declspec(dllexport)
#      else
#        define EMC_API __attribute__((visibility("default")))
#      endif
#    else                        /* consuming the emc shared lib */
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

### 5.1 Where `EMC_API` goes

Most of `emc` is templates and `constexpr` — **header-only, must NOT carry `EMC_API`** (exporting a
template instantiation is meaningless, and wrong on MSVC). Mark only the **non-template functions compiled
in `src/*.cpp`**: the runtime boundary helpers (material lookup, error formatting, any string→quantity
parsing).

```cpp
// include/emc/core/materials.hpp — properties() is constexpr, header-resolved: NO EMC_API
[[nodiscard]] constexpr emc::Result<MaterialProperties> properties(Material m) noexcept;

// include/emc/basic/skin_depth.hpp — out-of-line calculate(), compiled in src/: needs EMC_API
[[nodiscard]] EMC_API emc::Result<SkinDepthResult> calculate(const SkinDepthInput& in);
```

> [!TIP]
> The public surface is exactly the `EMC_API`-marked functions plus the header-only `constexpr`
> calculators, nothing else — the library-shaped equivalent of "expose only what you mean to."

---

## 6. Warnings interface library (`cmake/CompilerWarnings.cmake`)

A dedicated `INTERFACE` target carries the warning policy so it applies uniformly to lib + tests +
examples. `-Werror` is gated behind `EMC_WARNINGS_AS_ERRORS` (default OFF off-tree), so consumers are
never forced into it.

```cmake
# cmake/CompilerWarnings.cmake
add_library(emc_project_warnings INTERFACE)

set(_emc_gcc_clang
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
    -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion
    -Wformat=2 -Wimplicit-fallthrough)
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

`-Wconversion`/`-Wsign-conversion` are deliberately on: a narrowing or bare-`double` escape now warns,
actively policing that the unit-safety from `core/units.hpp` is not bypassed.

> [!TIP]
> Link the same `emc_project_warnings` into tests and examples so the warning policy is uniform
> everywhere (`target_link_libraries(<target> PRIVATE emc_project_warnings)`).

---

## 7. Tests (`tests/CMakeLists.txt`)

A single Catch2 v3 executable links `emc::emc`, the shared test-support headers, and
`Catch2::Catch2WithMain`; `catch_discover_tests` registers each `TEST_CASE` with CTest. Reference CSVs are
copied next to the binary so `load_csv("reference/<Name>.csv")` resolves at runtime. (Helpers and the core
tests are defined in [`00-foundation-code.md`](00-foundation-code.md) §6.)

```cmake
# tests/CMakeLists.txt
find_package(Catch2 3 REQUIRED)        # provided by §4 (system or FetchContent)
include(Catch)                          # gives catch_discover_tests()

add_executable(emc_tests
    core/constants_test.cpp
    core/materials_test.cpp
    basic/skin_depth_test.cpp
    converter/vswr_test.cpp
    component/microstrip_trace_test.cpp
    prediction/rf_field_test.cpp
    shielding/shielding_effectiveness_test.cpp
    filtering/ferrite_test.cpp
    cabling/crosstalk_test.cpp
    grounding/microstrip_current_test.cpp
    testing/noise_figure_test.cpp
    # ... one *_test.cpp per calculator, added with its calculator
)

target_link_libraries(emc_tests PRIVATE
    emc::emc                            # the library under test (public surface)
    Catch2::Catch2WithMain              # Catch2 v3 main()
    emc_project_warnings)               # same warning policy as the lib (§6)

target_compile_features(emc_tests PRIVATE cxx_std_23)
target_include_directories(emc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})  # for support/*.hpp

# Copy reference (golden) vectors next to the test binary so
# load_csv("reference/<Name>.csv") resolves at runtime.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/reference
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Register every TEST_CASE with CTest (so `ctest --preset debug` runs them all).
catch_discover_tests(emc_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
```

> [!NOTE]
> `enable_testing()` is called in the top-level `CMakeLists.txt` (§3) before `add_subdirectory(tests)`, so
> CTest is active for this directory. `catch_discover_tests` runs the binary at build time to enumerate
> cases, giving one CTest entry per `TEST_CASE` — far better failure granularity than a single
> `add_test(emc_tests emc_tests)`. (If you ever need the coarse form, `add_test(NAME emc_tests COMMAND
> emc_tests)` is the fallback.)

---

## 8. Examples (`examples/CMakeLists.txt`)

Examples link `emc::emc` and print results with `std::print` — no GUI toolkit. Because they link the same
alias as a `find_package(emc)` consumer, building them in-tree is a standing smoke test of the public
surface.

```cmake
# examples/CMakeLists.txt
add_executable(emc_example_skin_depth skin_depth.cpp)
target_link_libraries(emc_example_skin_depth PRIVATE emc::emc emc_project_warnings)
target_compile_features(emc_example_skin_depth PRIVATE cxx_std_23)

# Add one add_executable + target_link_libraries pair per example as the inventory grows.
```

A calculator returns an `emc::Result<...>` of mp-units quantities; rendering it (console, service,
anything) is a presentation concern that lives outside `emc` and links `emc::emc` like any other consumer.
A minimal `examples/skin_depth.cpp`:

```cpp
// examples/skin_depth.cpp — a standing smoke test of the public surface
#include <print>

#include <mp-units/systems/si.h>

#include <emc/basic/skin_depth.hpp>
#include <emc/core/materials.hpp>

int main() {
    using namespace mp_units;
    using namespace mp_units::si::unit_symbols;

    const emc::basic::SkinDepthInput in{
        .frequency = 1.0 * MHz,
        .material  = emc::materials::Material::Copper,
    };

    if (const auto r = emc::basic::calculate(in)) {
        std::println("skin depth @ 1 MHz (Cu) = {}", r->skin_depth.in(um));
        return 0;
    } else {
        std::println(stderr, "error: {}", r.error().what());
        return 1;
    }
}
```

---

## 9. Install & package config

Goal: after `cmake --install`, a separate project does

```cmake
find_package(emc 0.1 REQUIRED)
target_link_libraries(myapp PRIVATE emc::emc)
```

and gets headers, the compiled lib, the C++23 requirement, and mp-units transitively — zero manual flags.

```cmake
# cmake/Install.cmake
include(CMakePackageConfigHelpers)

install(TARGETS emc emc_project_warnings
    EXPORT emcTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/emc
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.hpp")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/emc/export.hpp   # generated header
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/emc)

install(EXPORT emcTargets
    FILE emcTargets.cmake NAMESPACE emc::         # imported target -> emc::emc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/emc)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/emcConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/emc)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)   # 0.x and 1.x not compatible (§15)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/emc)

export(EXPORT emcTargets NAMESPACE emc::          # find_package against build dir, no install
    FILE ${CMAKE_CURRENT_BINARY_DIR}/emcTargets.cmake)
```

The config template **must re-find mp-units**, because `emc::emc` carries a `PUBLIC` link to it; otherwise
the consumer's `find_package(emc)` succeeds but the link step fails on the missing imported target:

```cmake
# cmake/emcConfig.cmake.in
@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
find_dependency(mp-units CONFIG)                  # emc::emc links mp-units PUBLIC
include("${CMAKE_CURRENT_LIST_DIR}/emcTargets.cmake")
check_required_components(emc)
```

> [!WARNING]
> Use `find_dependency`, not `find_package`, in the config — it forwards `REQUIRED`/`QUIET`/version from
> the outer `find_package(emc ...)` and fails the whole lookup cleanly if mp-units is missing.

Installed layout:

```text
<prefix>/
├── include/emc/...                  # public headers + generated export.hpp
└── lib/
    ├── libemc.a   (or libemc.so.0.1.0)
    └── cmake/emc/
        ├── emcConfig.cmake          # entry point for find_package(emc)
        ├── emcConfigVersion.cmake   # SameMajorVersion check
        └── emcTargets.cmake         # defines imported emc::emc
```

---

## 10. `CMakePresets.json`

Presets pin generator, build dir, flags, and warning/sanitizer policy so devs and CI configure
identically. Schema v6 (CMake ≥ 3.28). Three configure presets — `debug`, `release`, and an ASan/UBSan
`asan` preset — each with matching build and test presets.

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 28, "patch": 0 },
  "configurePresets": [
    {
      "name": "base", "hidden": true, "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
        "EMC_BUILD_TESTS": "ON", "EMC_BUILD_EXAMPLES": "ON",
        "EMC_WARNINGS_AS_ERRORS": "ON"
      }
    },
    {
      "name": "debug", "displayName": "Debug (static, warnings-as-errors)",
      "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "BUILD_SHARED_LIBS": "OFF" }
    },
    {
      "name": "release", "displayName": "Release (shared, optimized)",
      "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release", "BUILD_SHARED_LIBS": "ON" }
    },
    {
      "name": "asan", "displayName": "Debug + ASan/UBSan", "inherits": "debug",
      "cacheVariables": {
        "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all",
        "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined",
        "CMAKE_SHARED_LINKER_FLAGS": "-fsanitize=address,undefined"
      }
    }
  ],
  "buildPresets": [
    { "name": "debug", "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" },
    { "name": "asan", "configurePreset": "asan" }
  ],
  "testPresets": [
    {
      "name": "debug", "configurePreset": "debug",
      "output": { "outputOnFailure": true },
      "execution": { "noTestsAction": "error", "stopOnFailure": false }
    },
    {
      "name": "release", "configurePreset": "release",
      "output": { "outputOnFailure": true }
    },
    {
      "name": "asan", "configurePreset": "asan",
      "output": { "outputOnFailure": true },
      "environment": { "UBSAN_OPTIONS": "print_stacktrace=1", "ASAN_OPTIONS": "detect_leaks=1" }
    }
  ]
}
```

> [!NOTE]
> ASan/UBSan is high-value here: the formulas use `std::pow`/`std::log`/`std::exp`, divisions, and array
> indexing. UBSan catches divide-by-zero and math-domain errors at their origin instead of letting them
> propagate as `inf`/`nan`. The `asan` preset uses GCC/Clang flags; on MSVC use `/fsanitize=address`
> (UBSan is not available) in a separate preset if needed.

---

## 11. Dotfiles

### 11.1 `.gitignore`

Keep build outputs, caches, and IDE noise out of the repo. `CMakeUserPresets.json` is per-developer and is
**not** committed (it overrides the shared `CMakePresets.json`).

```ini
# Build & configure output
/build/
/out/
/install/
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
compile_commands.json

# Per-developer presets (never committed)
CMakeUserPresets.json

# Caches
.cache/
.ccache/

# IDE / editor
.idea/
.vscode/
.vs/
*.swp
*~
.DS_Store
```

### 11.2 `.clang-format`

```ini
# .clang-format
BasedOnStyle: LLVM
Standard: c++23
ColumnLimit: 100
PointerAlignment: Left
IndentWidth: 4
```

### 11.3 `.clang-tidy`

`misc-include-cleaner` doubles as the layering enforcer from
[`../01-architecture-and-layout.md`](../01-architecture-and-layout.md): a `src/component/*.cpp` that
includes a sibling category's `emc/basic/...` header shows up as an unexpected include and fails review.

```ini
# .clang-tidy
Checks: >
  -*, bugprone-*, cppcoreguidelines-*, modernize-*, performance-*,
  readability-*, misc-include-cleaner, -modernize-use-trailing-return-type
WarningsAsErrors: 'bugprone-*'
HeaderFilterRegex: 'include/emc/.*'
```

Point tooling at the compile database (`CMAKE_EXPORT_COMPILE_COMMANDS ON` writes it per preset):

```bash
ln -sf build/debug/compile_commands.json compile_commands.json
clang-tidy -p build/debug src/component/microstrip_trace.cpp
```

---

## 12. CI (`.github/workflows/ci.yml`)

A realistic GitHub Actions workflow: it runs the **GUI-free gate first** (cheap, fails fast), then
configures with the `debug` preset, builds, runs `ctest`, and finally re-runs everything under the
ASan/UBSan `asan` preset. Ninja + a recent compiler give clean C++23 support.

```yaml
# .github/workflows/ci.yml
name: ci

on:
  push:
    branches: [main]
  pull_request:

jobs:
  gui-free-gate:
    name: GUI-free gate
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Fail if any GUI-toolkit header leaks into the library
        run: |
          if grep -rEni '#include[[:space:]]*<[^>]*(gtk|gui|window|x11|cocoa|wx)' include/ src/; then
            echo "ERROR: GUI-toolkit symbols found in the GUI-free emc library" >&2
            exit 1
          fi
          echo "OK: no GUI-toolkit headers in include/ or src/"

  build-and-test:
    name: Build & test (debug)
    runs-on: ubuntu-latest
    needs: gui-free-gate
    steps:
      - uses: actions/checkout@v4
      - name: Install toolchain
        run: |
          sudo apt-get update
          sudo apt-get install -y ninja-build cmake g++-14
          echo "CXX=g++-14" >> "$GITHUB_ENV"
      - name: Configure
        run: cmake --preset debug
      - name: Build
        run: cmake --build --preset debug
      - name: Test
        run: ctest --preset debug

  sanitizers:
    name: Sanitizers (ASan/UBSan)
    runs-on: ubuntu-latest
    needs: gui-free-gate
    steps:
      - uses: actions/checkout@v4
      - name: Install toolchain
        run: |
          sudo apt-get update
          sudo apt-get install -y ninja-build cmake clang-18
          echo "CXX=clang++-18" >> "$GITHUB_ENV"
      - name: Configure
        run: cmake --preset asan
      - name: Build
        run: cmake --build --preset asan
      - name: Test (sanitized)
        run: ctest --preset asan
```

> [!TIP]
> The `gui-free-gate` job is a `needs:` dependency of both build jobs, so a GUI-header leak fails the whole
> pipeline before any expensive compilation runs.

---

## 13. Bootstrap from zero

From an empty directory to a green build and a first calculator. Run these in order.

```bash
# 1. Create the project skeleton.
mkdir -p emcpp && cd emcpp
git init
mkdir -p cmake \
         include/emc/core \
         include/emc/{basic,converter,component,prediction,shielding,filtering,cabling,grounding,testing} \
         src/core \
         src/{basic,converter,component,prediction,shielding,filtering,cabling,grounding,testing} \
         tests/{support,reference,core} \
         tests/{basic,converter,component,prediction,shielding,filtering,cabling,grounding,testing} \
         examples \
         .github/workflows

# 2. Drop in the build & dotfiles from this guide:
#    CMakeLists.txt (§3), CMakePresets.json (§10),
#    cmake/Dependencies.cmake (§4), cmake/CompilerWarnings.cmake (§6),
#    cmake/Install.cmake (§9), cmake/emcConfig.cmake.in (§9),
#    tests/CMakeLists.txt (§7), examples/CMakeLists.txt (§8),
#    .gitignore / .clang-format / .clang-tidy (§11), .github/workflows/ci.yml (§12)

# 3. Drop in the Foundation headers + test support from 00-foundation-code.md:
#    include/emc/core/{error,constants,units,materials,calculator}.hpp
#    src/core/{error,materials}.cpp
#    tests/support/{csv,approx}.hpp  +  tests/core/{constants,materials}_test.cpp

# 4. Configure, build, and test the Debug preset.
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# 5. Add your first calculator (e.g. emc::basic skin depth):
#    a. include/emc/basic/skin_depth.hpp   — Input/Result aggregates + calculate()/validate() + tag struct
#    b. src/basic/skin_depth.cpp           — the compiled body
#    c. add `src/basic/skin_depth.cpp` to target_sources(emc PRIVATE ...) in CMakeLists.txt (§3)
#    d. tests/basic/skin_depth_test.cpp    — add it to emc_tests in tests/CMakeLists.txt (§7)
#    e. tests/reference/SkinDepth.csv      — its reference (golden) vectors

# 6. Re-run — the new TU and test build deterministically (no glob to refresh).
cmake --build --preset debug
ctest --preset debug

# 7. (optional) Run the optimized + sanitized configurations too.
cmake --preset release && cmake --build --preset release && ctest --preset release
cmake --preset asan    && cmake --build --preset asan    && ctest --preset asan
```

> [!IMPORTANT]
> Adding a calculator is always the same four edits: **header + `.cpp` + one `target_sources` line + one
> test entry** (plus its reference CSV). Because sources are listed explicitly (§3.1), there is no
> "forgot to reconfigure" failure mode — a `.cpp` you didn't list simply isn't part of the library, which
> is the intended, predictable behavior.

---

## 14. GUI-free CI gate

The library's one hard environmental invariant: it depends only on the C++23 stdlib and mp-units, never on
any UI toolkit. CI greps the *library* tree (`include/` + `src/`) for GUI-toolkit include directives and
fails the build if any appear — generically, by common toolkit substrings, so no specific framework needs
naming.

```bash
if grep -rEni '#include[[:space:]]*<[^>]*(gtk|gui|window|x11|cocoa|wx)' include/ src/; then
  echo "ERROR: GUI-toolkit symbols found in the GUI-free emc library" >&2
  exit 1
fi
```

The library exposes only `emc::*` calculators returning `emc::Result<Result>` (i.e.
`std::expected<Result, emc::Error>`) over mp-units quantities; any front end links `emc::emc` and renders
results itself (§8), so no GUI symbol belongs in `include/` or `src/`. This is the same command the
`gui-free-gate` job runs in §12; run it locally before pushing.

---

## 15. ABI, versioning, and modules-later

- **SemVer + `SameMajorVersion`** — `find_package(emc 0.1)` accepts `0.x`, rejects `1.0`. During 0.x the
  ABI may break freely (appropriate for a pre-1.0 library still being shaped by the inventory work).
- **SOVERSION = MAJOR** on the shared lib (`libemc.so.0`) so the runtime linker tracks ABI by major.
- **Tiny ABI surface** thanks to default-hidden visibility (§5): only `EMC_API` functions are exported, so
  most refactors of the header-only `constexpr` calculators are *not* ABI breaks — they recompile into the
  consumer. The risk surface is the handful of compiled boundary functions (out-of-line `calculate()`,
  material lookup, error formatting).
- **mp-units is a public dependency**, so its major bump is an `emc` ABI concern; the pinned `GIT_TAG` (§4)
  and `find_dependency(mp-units)` (§9) make the coupling explicit and reproducible.

**Modules are a deliberate non-goal for v1.** Headers + `.cpp` are the most robust mode for mp-units,
Catch2, and tooling across current toolchains. The path to modules later is additive: 3.28 already supports
`CXX_MODULES`/`import std`; add a `FILE_SET CXX_MODULES` listing `*.cppm`, flip mp-units'
`MP_UNITS_BUILD_CXX_MODULES` on, and keep the `*.hpp` headers as a parallel facade. No public API or
namespace changes implied — so the §1 layout and the one-TU-per-calculator rule do not change when modules
arrive.

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — the Foundation headers (`core/*.hpp`), the `src/core`
  bodies, and the `tests/support/` helpers this build wires up.
- [`../01-architecture-and-layout.md`](../01-architecture-and-layout.md) — the layered namespace/directory
  model the `include/` + `src/` split encodes, and the GUI-free invariant.
- [`../07-calculator-inventory.md`](../07-calculator-inventory.md) — the full calculator list each
  `target_sources` line and `*_test.cpp` maps to.
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the reference-vector
  harness and the ASan/UBSan CTest preset.
- [`../README.md`](../README.md) — the full plan index.

