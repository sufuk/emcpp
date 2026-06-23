# Getting started

## Requirements

| Requirement | Version |
|---|---|
| C++ standard | **C++23** |
| CMake | **≥ 3.28** |
| Compiler | GCC 14+ (the supported/CI compiler) — see [Development & CI](development.md) |
| [mp-units](https://github.com/mpusz/mp-units) | **v2.5.0** — fetched automatically if not found |
| [Catch2](https://github.com/catchorg/Catch2) | **v3.7.1** — tests only, fetched automatically |

Dependencies are resolved with `find_package(... CONFIG QUIET)` first and fall back to `FetchContent` (shallow clone, pinned tags), so a clean checkout builds with **no manual dependency setup**.

## Build & test

```sh
# Configure (Release is the default build type if none is set)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run the test suite (built by default at top level)
ctest --test-dir build --output-on-failure
```

!!! tip
    In-source builds are blocked — use a separate `build/` directory. The **Ninja** generator works too: `cmake -S . -B build -G Ninja`.

### CMake options

| Option | Default | Purpose |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Build `emc` as a shared library instead of static. |
| `EMC_BUILD_TESTS` | `ON` top-level | Build the test suite and pull in Catch2. |
| `EMC_BUILD_EXAMPLES` | `ON` top-level | Build the usage examples. |
| `EMC_WARNINGS_AS_ERRORS` | `ON` top-level | Treat warnings as errors. |

## Use emcpp in your project

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /your/install/prefix
```

```cmake
# In your CMakeLists.txt
find_package(emc CONFIG REQUIRED)          # version compatibility: SameMajorVersion
target_link_libraries(your_target PRIVATE emc::emc)
```

The installed package config calls `find_dependency(mp-units CONFIG)`, so **mp-units must be findable by the consumer**. The package can also be consumed directly from the build tree.

## A first program

```cpp
#include <print>
#include <mp-units/systems/si.h>

#include <emc/basic/skin_depth.hpp>
#include <emc/core/materials.hpp>

int main() {
    using namespace mp_units;
    using namespace mp_units::si::unit_symbols;

    const emc::basic::SkinDepthInput cu{
        .frequency = 1.0 * MHz,
        .material  = emc::materials::Material::Copper,
    };
    if (const auto r = emc::basic::calculate(cu))
        std::println("Cu @ 1 MHz: skin depth = {}", r->skin_depth.in(um));   // ~65.21 um
    else
        std::println(stderr, "error: {}", r.error().what());
    return 0;
}
```
