## Plan: Modern C++ EMC Calculation Library Project Structure & Setup

Design a modular, extensible C++ EMC calculation library using CMake, modern C++20/23 features, and best practices for code organization, testing, documentation, and CI. The structure will support clear separation of modules (Antenna, Decibel, etc.), maintainability, and future growth.

### Steps
1. Define top-level folder structure: `/src`, `/include`, `/tests`, `/examples`, `/docs`, `/cmake`, `/ci`.
2. Under `/src` and `/include`, create subfolders for each module: `antenna`, `decibel`, `skindepth`, `converter`, `component`, `emc`, `shielding`, `filtering`, `cabling`, `grounding`, `testing`.
3. Set up CMake with a root `CMakeLists.txt` and per-module CMake files for modular builds and dependencies.
4. Establish a top-level namespace `emcpp` and sub-namespaces for each module (e.g., `emcpp::antenna`).
5. Define code organization guidelines: header/source separation, use of `#pragma once`, clear API boundaries, and extensibility via interfaces/concepts.
6. Specify use of modern C++ features: C++20/23 standard, concepts, modules (if supported), smart pointers, `constexpr`, ranges, and guidelines for their use.
7. Create `/tests` for unit tests (suggest Catch2, GoogleTest, or doctest) and `/examples` for usage demos.
8. Add `/docs` for documentation (recommend Doxygen + Markdown), and `/ci` for CI scripts (suggest GitHub Actions or GitLab CI).
9. Document contribution, code style, and extensibility guidelines in `CONTRIBUTING.md` and `README.md`.

### Further Considerations
