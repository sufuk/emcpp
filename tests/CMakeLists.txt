# tests/CMakeLists.txt
# Catch2 (target Catch2::Catch2WithMain) is provided by cmake/Dependencies.cmake — via a
# system find_package OR FetchContent. include(Catch) gives catch_discover_tests(); its
# module is on CMAKE_MODULE_PATH (set by Dependencies.cmake for the FetchContent case).
include(Catch)

add_executable(emc_tests
    # core
    core/constants_test.cpp
    core/materials_test.cpp
    # basic
    basic/skin_depth_test.cpp
    basic/decibel_test.cpp
    basic/dipole_antenna_test.cpp
    basic/loop_antenna_test.cpp
    basic/far_field_criteria_test.cpp
    # converter
    converter/antenna_factor_test.cpp
    converter/efield_power_density_test.cpp
    converter/energy_frequency_test.cpp
    converter/wavelength_frequency_test.cpp
    converter/vswr_test.cpp
    # component
    component/capacitance_test.cpp
    component/inductance_test.cpp
    component/resistance_test.cpp
    component/transmission_line_test.cpp
    component/microstrip_trace_test.cpp
    component/stripline_trace_test.cpp
    component/dual_stripline_trace_test.cpp
    component/embedded_microstrip_trace_test.cpp
    component/harmonic_trap_test.cpp
    # prediction
    prediction/esd_coupling_test.cpp
    prediction/lightning_coupling_test.cpp
    prediction/rf_field_test.cpp
    prediction/friis_test.cpp
    # shielding
    shielding/cavity_resonance_test.cpp
    shielding/shielding_effectiveness_test.cpp
    shielding/aperture_test.cpp
    shielding/slot_test.cpp
    # cabling
    cabling/braid_coverage_test.cpp
    cabling/crosstalk_test.cpp
    # grounding
    grounding/microstrip_current_test.cpp
    # testing
    testing/noise_figure_test.cpp
    # filtering
    filtering/ferrite_toroid_test.cpp
    # validation against the Excel reference vectors (tests/reference/*.csv)
    validation/reference_test.cpp
)

target_link_libraries(emc_tests PRIVATE
    emc::emc                            # the library under test (public surface)
    emc_validation                      # shared runners over tests/reference/*.csv
    Catch2::Catch2WithMain)             # Catch2 v3 provides main()

target_compile_features(emc_tests PRIVATE cxx_std_23)
target_include_directories(emc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})  # for "support/*.hpp"

# Register every TEST_CASE with CTest so `ctest` runs them all.
catch_discover_tests(emc_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
