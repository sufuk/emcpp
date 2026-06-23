# Development & CI

## Project layout

```
include/emc/<domain>/   public headers (one per calculator family)
src/<domain>/           out-of-line implementations
examples/               runnable usage examples
tests/<domain>/         Catch2 test suites + tests/support/ helpers
tests/reference/        Excel-derived reference vectors (see Validation)
validation/             shared validation core + the HTML report tool
cmake/                  dependencies, warnings, install rules, package config
.github/workflows/      ci.yml (build/test/sanitizers/install) + release.yml + docs.yml
```

## Continuous integration

[`ci.yml`](https://github.com/sufuk/emcpp/blob/main/.github/workflows/ci.yml) runs on every push/PR:

| Job | What it does |
|---|---|
| **build-test** | Matrix of `{gcc-14} × {Debug, Release}` on ubuntu-24.04 — configure, build (`-Werror`), and `ctest` (incl. the reference validation gate). |
| **install-smoke** | Installs the package, then builds a tiny out-of-tree `find_package(emc)` consumer to prove it is self-contained. |
| **validation-report** | Builds the report tool and uploads `validation.html` as an artifact. |

## Toolchain status

emcpp is **gcc-first** (GCC 14 is the supported, CI-blocking compiler — it builds and passes all tests).

!!! warning "clang support is being ported"
    The clang-18 CI checks are currently commented out. clang needs further work:

    - **libc++** (not the default libstdc++) for `std::expected`;
    - a few `-Wshadow`/`-Werror` fixes (e.g. an `in` parameter vs the `in`=inch unit symbol);

    Configure-stage fixes already landed (`MP_UNITS_API_STD_FORMAT=ON`, a global `CMAKE_CXX_STANDARD=23`, and a constexpr-portable EM-identity check). The remaining work is tracked separately.

## Releases

Releases are **tag-driven** ([`release.yml`](https://github.com/sufuk/emcpp/blob/main/.github/workflows/release.yml)). Pushing a SemVer tag (`vX.Y.Z`) runs a gated pipeline: full build + `ctest` → version/package check → deterministic archives + checksums → a GitHub Release with auto-generated notes.

## Documentation (this site)

This site is built by [`docs.yml`](https://github.com/sufuk/emcpp/blob/main/.github/workflows/docs.yml): **MkDocs Material** for the guides + **Doxygen** for the [API reference](api/index.html), deployed to GitHub Pages on every push to `main`.
