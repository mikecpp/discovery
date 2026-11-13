# Loaded by CPack at package time (CPACK_PROJECT_CONFIG_FILE).
# Selects which install components go into each generator's package.
if(CPACK_GENERATOR STREQUAL "DEB")
    set(CPACK_COMPONENTS_ALL runtime docs)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_PACKAGE_FILE_NAME
        "${CPACK_PACKAGE_NAME}_${CPACK_PACKAGE_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}")
else()
    set(CPACK_COMPONENTS_ALL runtime docs tarball)
    set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY ON)
endif()
