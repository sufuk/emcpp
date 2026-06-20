# Build System (Modern CMake, Compiled Library) ⚙️

> The CMake build for the `emc` library as a **traditional compiled library** (static and/or shared)
> exporting `emc::emc`: dependency acquisition (mp-units), shared-library visibility, install/export +
> package config for `find_package(emc)`, and presets.

## Locked decisions

| Decision | Form |
| --- | --- |
| **C++23 baseline** | `target_compile_features(emc PUBLIC cxx_std_23)` — a PUBLIC usage requirement, not global `CMAKE_CXX_STANDARD` |
| **Compiled library** | `add_library(emc)` with `install()`/`export()` + package config; headers in `include/emc/`, TUs in `src/`. Modules are a future option (§9). |
| **mp-units = units layer** | `find_package(mp-units CONFIG)` + `FetchContent` fallback, linked `PUBLIC` (it appears in every public signature) |
| **GUI-free** | no UI toolkit anywhere; a CI grep gate (§8) enforces it |

> [!IMPORTANT]
> Two non-obvious choices: sources are listed **explicitly** (never globbed), and C++23 is a **`PUBLIC`**
> requirement on the target. Rationale in §1.1.

---

## 1. Top-level `CMakeLists.txt`

`cmake_minimum_required(VERSION 3.28)` is the floor: solid `cxx_std_23` across GCC 14 / Clang 18 / MSVC
19.4x, a supported `import std` path (for §9 later, not used now), and CMakePresets schema v6 (§5).

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

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)          # for clang-tidy / clangd (§6.3)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)        # only EMC_API symbols exported (§3)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BUILD_SHARED_LIBS      "Build emc as a shared library"       OFF)
option(EMC_BUILD_TESTS        "Build the emc test suite"            ${PROJECT_IS_TOP_LEVEL})
option(EMC_BUILD_EXAMPLES     "Build the emc usage examples"        ${PROJECT_IS_TOP_LEVEL})
option(EMC_WARNINGS_AS_ERRORS "Treat warnings as errors"           ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)
include(cmake/Dependencies.cmake)              # mp-units (+ test deps) — §2

add_library(emc)                               # STATIC or SHARED via BUILD_SHARED_LIBS
add_library(emc::emc ALIAS emc)                # consumers always say emc::emc

target_compile_features(emc PUBLIC cxx_std_23) # PUBLIC: propagates to consumers (§1.1)

target_sources(emc PRIVATE                     # explicit list, NOT glob (§1.1)
    src/error.cpp
    src/units/parse.cpp
    src/constants/constants.cpp
    src/materials/material_db.cpp
    src/basic/skin_depth.cpp
    src/converter/wavelength_frequency.cpp
    src/component/microstrip_trace.cpp
    # ... one .cpp per calculator, added as doc 07 items land
)

target_include_directories(emc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>   # generated export.hpp
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(emc PUBLIC mp-units::mp-units)        # in public headers -> PUBLIC (§2)

include(cmake/CompilerWarnings.cmake)
target_link_libraries(emc PRIVATE emc_project_warnings)

set_target_properties(emc PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}         # ABI tracked by major (§9)
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

### 1.1 Four load-bearing choices

- **`add_library(emc)` with no kind** — `BUILD_SHARED_LIBS` selects static vs shared. Default-hidden
  visibility + `EMC_API` means both modes expose an identical, minimal public surface.
- **`emc::emc` alias** — in-tree examples/tests link the same name a downstream `find_package(emc)`
  consumer uses, so `examples/` is a standing smoke test of the public surface.
- **`mp-units::mp-units` is `PUBLIC`** — every public header puts mp-units types in signatures
  (`quantity<isq::frequency[si::hertz]>`), so consumers need its includes/flags transitively. This is also
  why the package config must `find_dependency(mp-units)` (§4).
- **No `GLOB_RECURSE`** — a glob is evaluated once at configure time, so a newly added calculator `.cpp`
  would silently not build until someone reconfigures. With ~52 calculators landing incrementally, sources
  are listed explicitly.
- **C++23 is `PUBLIC`, not global** — `CMAKE_CXX_STANDARD` does not travel with the target; a `PUBLIC`
  `target_compile_features` does, so every consumer compiles in the mode the headers require.
- **`PROJECT_IS_TOP_LEVEL`** drives test/example defaults — ON at the root, OFF when consumed via
  `add_subdirectory`/`FetchContent`, so the library never drags its tests into a consumer.

---

## 2. Dependencies (`cmake/Dependencies.cmake`)

Find-or-fetch: prefer a system `find_package` copy; fall back to `FetchContent` so a clean checkout builds
with zero setup.

```cmake
include(FetchContent)

find_package(mp-units CONFIG QUIET)
if(NOT mp-units_FOUND)
    FetchContent_Declare(mp-units
        GIT_REPOSITORY https://github.com/mpusz/mp-units.git
        GIT_TAG        v2.5.0          # pinned for reproducibility (§9)
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
            GIT_TAG v3.7.1 GIT_SHALLOW TRUE SYSTEM)
        FetchContent_MakeAvailable(Catch2)
    endif()
endif()
```

> [!NOTE]
> **PUBLIC vs PRIVATE rule:** a dependency goes `PUBLIC` iff it appears in a public header under
> `include/emc/`. mp-units → `PUBLIC` (quantity types in every signature); Catch2 → test target only. This
> keeps the package config's `find_dependency` list minimal and correct.

---

## 3. Shared-library visibility (`include/emc/export.hpp`)

A shared `emc` defaults to **hidden** visibility (set in §1); only `EMC_API`-marked symbols are exported.
One macro makes Windows (`__declspec`) and ELF/Mach-O (`visibility("default")`) behave identically:
smaller export tables, faster load, no accidental ABI surface.

`generate_export_header` (called in §1) writes `include/emc/export.hpp` into the build tree, defining
`EMC_API` / `EMC_LOCAL`. For a static build CMake compiles with `-DEMC_STATIC_DEFINE`, so `EMC_API`
expands to nothing — the same source works in both modes.

### 3.1 Where `EMC_API` goes

Most of `emc` is templates and `constexpr` — **header-only, must NOT carry `EMC_API`** (exporting a
template instantiation is meaningless, and wrong on MSVC). Mark only the **non-template functions compiled
in `src/*.cpp`**: the runtime boundary helpers (string→quantity parsing, material lookup, error
formatting).

```cpp
// include/emc/units/parse.hpp — compiled in src/units/parse.cpp -> needs EMC_API
[[nodiscard]] EMC_API std::expected<Length, Error> parse_length(std::string_view text);

// include/emc/basic/skin_depth.hpp — pure calculator: NO EMC_API (constexpr, header-resolved)
[[nodiscard]] constexpr std::expected<SkinDepthResult, Error>
calculate(const SkinDepthInput& in) noexcept;
```

> [!TIP]
> The public surface is exactly the `EMC_API`-marked functions plus the header-only calculators, nothing
> else — the library-shaped equivalent of "expose only what you mean to".

---

## 4. Install & package config

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
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)   # 0.x and 1.x not compatible (§9)

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

## 5. `CMakePresets.json`

Presets pin generator, build dir, flags, and warning/sanitizer policy so devs and CI configure
identically. Schema v6 (CMake ≥ 3.28).

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
      "name": "asan", "configurePreset": "asan",
      "output": { "outputOnFailure": true },
      "environment": { "UBSAN_OPTIONS": "print_stacktrace=1", "ASAN_OPTIONS": "detect_leaks=1" }
    }
  ]
}
```

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan
```

> [!NOTE]
> ASan/UBSan is high-value here: the formulas use `std::pow`/`std::log`/`std::exp`, divisions, and
> `std::mdspan` indexing. UBSan catches divide-by-zero and domain errors at their origin instead of letting
> them propagate as `inf`/`nan`.

---

## 6. Quality tooling

### 6.1 Warnings interface library (`cmake/CompilerWarnings.cmake`)

A dedicated `INTERFACE` target carries the warning policy so it applies uniformly to lib + tests +
examples. `-Werror` is gated behind `EMC_WARNINGS_AS_ERRORS` (default OFF off-tree), so consumers are
never forced into it.

```cmake
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
actively policing that the unit-safety from doc 03 is not bypassed.

### 6.2 `.clang-format` / `.clang-tidy`

Pointers, not full configs (style is owned by the repo):

```text
# .clang-format
BasedOnStyle: LLVM
Standard: c++23
ColumnLimit: 100
PointerAlignment: Left
```

```text
# .clang-tidy
Checks: >
  -*, bugprone-*, cppcoreguidelines-*, modernize-*, performance-*,
  readability-*, misc-include-cleaner, -modernize-use-trailing-return-type
WarningsAsErrors: 'bugprone-*'
HeaderFilterRegex: 'include/emc/.*'
```

`misc-include-cleaner` doubles as the layering enforcer from doc 01: a `src/component/*.cpp` that includes
`emc/basic/...` shows up as an unexpected include and fails review.

### 6.3 `compile_commands.json`

`CMAKE_EXPORT_COMPILE_COMMANDS ON` emits `build/<preset>/compile_commands.json`; point clangd/clang-tidy
at it:

```bash
ln -sf build/debug/compile_commands.json compile_commands.json
clang-tidy -p build/debug src/component/microstrip_trace.cpp
```

---

## 7. Folder layout and source→target mapping

Mirrors `01-architecture-and-layout.md`: public headers and compiled sources share the same Foundation /
Domain / Facade structure.

```text
emcpp/
├── CMakeLists.txt                 # §1
├── CMakePresets.json              # §5
├── .clang-format  .clang-tidy     # §6.2
├── cmake/
│   ├── Dependencies.cmake         # §2 (mp-units find-or-fetch)
│   ├── CompilerWarnings.cmake     # §6.1
│   ├── Install.cmake              # §4
│   └── emcConfig.cmake.in         # §4
├── include/emc/                   # PUBLIC API (installed)
│   ├── export.hpp                 # GENERATED into build tree, installed from there (§3)
│   ├── error.hpp  units/...  constants/...  materials/...
│   ├── basic/...  converter/...  component/...  prediction/...
│   ├── shielding/...  filtering/...  cabling/...  grounding/...  testing/...
│   └── emc.hpp                    # Facade umbrella
├── src/                           # COMPILED TUs (target_sources, §1)
│   ├── error.cpp  units/parse.cpp  constants/constants.cpp  materials/material_db.cpp
│   └── basic/...  component/...
├── tests/                         # doc 09 — links emc::emc + Catch2
└── examples/
```

Mapping rules:

- One `include/emc/<cat>/<name>.hpp` ↔ one `src/<cat>/<name>.cpp` ↔ one `target_sources(emc PRIVATE ...)`
  line. Adding a doc-07 calculator = add the pair, add the one line, add a test. No glob means the new file
  builds deterministically.
- Header-only calculators (pure `constexpr calculate()`) still get a `.cpp` if they need a compiled
  validate/parse boundary; otherwise it can be empty but is kept for one-TU-per-calculator symmetry.
- Foundation `.cpp`s are few: `error.cpp` (formatting), `parse.cpp` (string→quantity), `material_db.cpp`
  (table lookup). Constants are header `constexpr`; `constants.cpp` only anchors ODR-used definitions.

### 7.1 Examples target

`examples/` links `emc::emc` and prints results with `std::print` — no GUI toolkit. Because they link the
same alias as a `find_package(emc)` consumer, building them in-tree is a standing smoke test of the public
surface.

```cmake
add_executable(emc_example_skin_depth skin_depth.cpp)
target_link_libraries(emc_example_skin_depth PRIVATE emc::emc emc_project_warnings)
```

A calculator returns a `Result` of mp-units quantities; rendering it (console, service, anything) is a
presentation concern that lives outside `emc` and links `emc::emc` like any other consumer.

---

## 8. Keeping the library GUI-free (CI gate)

Doc 01's one invariant: CI greps for GUI symbols over the *library* tree before configuring.

```bash
if grep -rEni '#include[[:space:]]*<[^>]*(gtk|gui|window|x11|cocoa|wx)' include/ src/; then
  echo "ERROR: GUI-toolkit symbols found in the GUI-free emc library" >&2
  exit 1
fi
```

The library exposes only `emc::*` calculators returning `std::expected<Result, Error>` over mp-units
quantities; any front end links `emc::emc` and renders results itself (§7.1), so no GUI symbol belongs in
`include/` or `src/`.

---

## 9. ABI, versioning, and modules-later

- **SemVer + `SameMajorVersion`** — `find_package(emc 0.1)` accepts `0.x`, rejects `1.0`. During 0.x the
  ABI may break freely (appropriate for a pre-1.0 library still being shaped by the doc-07 work-list).
- **SOVERSION = MAJOR** on the shared lib (`libemc.so.0`) so the runtime linker tracks ABI by major.
- **Tiny ABI surface** thanks to default-hidden visibility (§3): only `EMC_API` functions are exported, so
  most refactors of the header-only `constexpr` calculators are *not* ABI breaks — they recompile into the
  consumer. The risk surface is the handful of compiled boundary functions (`parse_*`, `material_*`,
  formatting).
- **mp-units is a public dependency**, so its major bump is an `emc` ABI concern; the pinned `GIT_TAG` (§2)
  and `find_dependency(mp-units)` (§4) make the coupling explicit and reproducible.

**Modules are a deliberate non-goal for v1.** Headers + `.cpp` are the most robust mode for mp-units,
Catch2, and tooling across current toolchains. The path to modules later is additive: `3.28` already
supports `CXX_MODULES`/`import std`; add a `FILE_SET CXX_MODULES` listing `*.cppm`, flip mp-units'
`MP_UNITS_BUILD_CXX_MODULES` on, and keep the `*.hpp` headers as a parallel facade. No public API or
namespace changes implied — so the §7 layout and the one-TU-per-calculator rule do not have to change when
modules arrive.

---

## Cross-references

See `01` (layers/GUI-free invariant), `03` (why mp-units links `PUBLIC` + `find_dependency`), `06`/`07`
(the per-calculator header/`.cpp` pairs each `target_sources` line maps to), and `09` (the `tests/` target
and ASan/UBSan ctest preset).
