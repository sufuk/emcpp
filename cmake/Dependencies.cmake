# cmake/Dependencies.cmake
include(FetchContent)

find_package(mp-units CONFIG QUIET)
if(NOT mp-units_FOUND)
    # These options are read by mp-units' own CMakeLists, so set them BEFORE MakeAvailable:
    set(MP_UNITS_API_CONTRACTS NONE CACHE STRING "" FORCE)   # no gsl-lite / ms-gsl transitive dep
    set(MP_UNITS_BUILD_CXX_MODULES OFF CACHE BOOL "" FORCE)  # headers, not C++20 modules
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
    endif()
endif()
