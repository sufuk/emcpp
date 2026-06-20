# tests/CMakeLists.txt
find_package(Catch2 3 REQUIRED)        # provided by cmake/Dependencies.cmake (system or FetchContent)
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
    emc_project_warnings)               # same warning policy as the lib

target_compile_features(emc_tests PRIVATE cxx_std_23)
target_include_directories(emc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})  # for support/*.hpp

# Copy reference (golden) vectors next to the test binary so
# load_csv("reference/<Name>.csv") resolves at runtime.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/reference
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Register every TEST_CASE with CTest (so `ctest --preset debug` runs them all).
catch_discover_tests(emc_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
