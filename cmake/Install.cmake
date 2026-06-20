# cmake/Install.cmake
include(CMakePackageConfigHelpers)

install(TARGETS emc emc_project_warnings
    EXPORT emcTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/emc
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.hpp")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/emc/export.hpp   # generated header
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/emc)

install(EXPORT emcTargets
    FILE emcTargets.cmake NAMESPACE emc::         # imported target -> emc::emc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/emc)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/emcConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/emc)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)   # 0.x and 1.x not compatible

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/emcConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/emc)

export(EXPORT emcTargets NAMESPACE emc::          # find_package against build dir, no install
    FILE ${CMAKE_CURRENT_BINARY_DIR}/emcTargets.cmake)
