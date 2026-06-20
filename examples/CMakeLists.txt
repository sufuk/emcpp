# examples/CMakeLists.txt
add_executable(emc_example_skin_depth skin_depth.cpp)
target_link_libraries(emc_example_skin_depth PRIVATE emc::emc emc_project_warnings)
target_compile_features(emc_example_skin_depth PRIVATE cxx_std_23)

# Add one add_executable + target_link_libraries pair per example as the inventory grows.
