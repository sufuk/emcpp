cmake_minimum_required(VERSION 3.28)
project(emc
        VERSION 0.1.0
        DESCRIPTION "Modern-C++ EMC engineering calculation library"
        HOMEPAGE_URL "https://github.com/sufuk/emcpp"
        LANGUAGES CXX
)

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a build/ directory or a preset.")
endif ()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)          # for clang-tidy / clangd
set(CMAKE_CXX_VISIBILITY_PRESET hidden)        # only EMC_API symbols exported
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()

option(BUILD_SHARED_LIBS "Build emc as a shared library" OFF)
option(EMC_BUILD_TESTS "Build the emc test suite" ${PROJECT_IS_TOP_LEVEL})
option(EMC_BUILD_EXAMPLES "Build the emc usage examples" ${PROJECT_IS_TOP_LEVEL})
option(EMC_WARNINGS_AS_ERRORS "Treat warnings as errors" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)
include(cmake/Dependencies.cmake)              # mp-units (+ test deps)

add_library(emc)                               # STATIC or SHARED via BUILD_SHARED_LIBS
add_library(emc::emc ALIAS emc)                # consumers always say emc::emc

target_compile_features(emc PUBLIC cxx_std_23) # PUBLIC: propagates to consumers

target_sources(emc PRIVATE                     # explicit list, NOT glob
        #    src/core/error.cpp
        include/emc/core/error.hpp
        src/core/materials.cpp
        #    src/basic/skin_depth.cpp
        #    src/converter/vswr.cpp
        #    src/component/microstrip_trace.cpp
        #    src/prediction/rf_field.cpp
        #    src/shielding/shielding_effectiveness.cpp
        #    src/filtering/ferrite.cpp
        #    src/cabling/crosstalk.cpp
        #    src/grounding/microstrip_current.cpp
        #    src/testing/noise_figure.cpp
        # ... one .cpp per calculator, added as inventory items land (07-calculator-inventory)
)

target_include_directories(emc PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>   # generated export.hpp
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(emc PUBLIC mp-units::mp-units)        # in public headers -> PUBLIC

include(cmake/CompilerWarnings.cmake)
target_link_libraries(emc PRIVATE emc_project_warnings)

set_target_properties(emc PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}         # ABI tracked by major
        EXPORT_NAME emc)

include(GenerateExportHeader)
generate_export_header(emc
        BASE_NAME EMC
        EXPORT_MACRO_NAME EMC_API
        NO_EXPORT_MACRO_NAME EMC_LOCAL
        EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/emc/export.hpp)

if (EMC_BUILD_TESTS)
    enable_testing()
    #    add_subdirectory(tests)
endif ()
if (EMC_BUILD_EXAMPLES)
    #    add_subdirectory(examples)
endif ()

include(cmake/Install.cmake)
