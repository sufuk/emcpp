# cmake/Dependencies.cmake
include(FetchContent)

# mp-units probes std::format support at configure time with a try_compile that
# honors the *global* C++ standard (CMP0067). This project otherwise sets the
# standard only per target (cxx_std_23), so the probe would compile at the
# compiler default (C++17) where __cpp_lib_format is undefined and the check
# fails. Pin the standard globally so the probe — and every dependency check —
# sees C++23.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(mp-units CONFIG QUIET)
if(NOT mp-units_FOUND)
    # These options are read by mp-units' own CMakeLists, so set them BEFORE MakeAvailable:
    set(MP_UNITS_API_CONTRACTS NONE CACHE STRING "" FORCE)   # no gsl-lite / ms-gsl transitive dep
    set(MP_UNITS_BUILD_CXX_MODULES OFF CACHE BOOL "" FORCE)  # headers, not C++20 modules
    set(MP_UNITS_API_STD_FORMAT ON CACHE STRING "" FORCE)    # use std::format (C++23); avoids the fmt dependency
    FetchContent_Declare(mp-units
        GIT_REPOSITORY https://github.com/mpusz/mp-units.git
        GIT_TAG        v2.5.0          # pinned for reproducibility
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  src             # mp-units' real entry point is src/, not the repo root
        SYSTEM)                        # treat its headers as -isystem
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
        # FetchContent does not register a find_package config, so put Catch2's CMake
        # helpers (extras/Catch.cmake -> catch_discover_tests) on the module path here.
        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    endif()
endif()
