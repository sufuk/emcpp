# validation/CMakeLists.txt
#
# Shared validation core: per-calculator runners that feed the Excel-derived
# reference vectors (tests/reference/*.csv) through emc and record the result
# next to the spreadsheet ground truth. Consumed by BOTH the reference test
# (tests/validation/reference_test.cpp, a CI gate) and the HTML report tool, so
# the two can never drift apart.

add_library(emc_validation STATIC validators.cpp)

target_link_libraries(emc_validation PUBLIC emc::emc)

# validation/  -> validation.hpp / validators.hpp ; tests/ -> support/csv.hpp
target_include_directories(emc_validation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests)

target_compile_features(emc_validation PUBLIC cxx_std_23)

# Absolute path to the reference CSVs, baked in so the binary finds them no
# matter what working directory CTest / the report run from.
target_compile_definitions(emc_validation PRIVATE
    EMC_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/tests/reference")

# The human-facing HTML dashboard generator (built with the examples).
if (EMC_BUILD_EXAMPLES)
    add_executable(emc_validation_report report.cpp)
    target_link_libraries(emc_validation_report PRIVATE emc_validation)
    target_compile_features(emc_validation_report PRIVATE cxx_std_23)
endif ()
