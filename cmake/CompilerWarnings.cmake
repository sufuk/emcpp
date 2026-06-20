# cmake/CompilerWarnings.cmake
add_library(emc_project_warnings INTERFACE)

set(_emc_gcc_clang
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
    -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion
    -Wformat=2 -Wimplicit-fallthrough)
set(_emc_msvc /W4 /permissive- /w14640 /w14242 /w14254 /w14263)

target_compile_options(emc_project_warnings INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:${_emc_gcc_clang}>
    $<$<CXX_COMPILER_ID:MSVC>:${_emc_msvc}>)

if(EMC_WARNINGS_AS_ERRORS)
    target_compile_options(emc_project_warnings INTERFACE
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()
