cmake_minimum_required(VERSION 3.16)
project(discovery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Kconfig)

set(KCONFIG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig")
set(KCONFIG_DEFCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/defconfig")
set(KCONFIG_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/.config" CACHE FILEPATH "Kconfig configuration file")

if(NOT EXISTS "${KCONFIG_CONFIG}")
    configure_file("${KCONFIG_DEFCONFIG}" "${KCONFIG_CONFIG}" COPYONLY)
endif()

kconfig_load("${KCONFIG_CONFIG}" "${CMAKE_CURRENT_BINARY_DIR}/generated/kconfig.h")
kconfig_add_targets("${KCONFIG_ROOT}" "${KCONFIG_CONFIG}" "${KCONFIG_DEFCONFIG}")

add_library(kconfig INTERFACE)
target_include_directories(kconfig INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")

set(COMPONENT_WARNINGS -Wall -Wextra -Wpedantic)

set(COMPONENT_TABLE
    icmp:COMPONENT_ICMP
    webserver:COMPONENT_WEBSERVER
)

set(COMPONENT_REQUIRES
    DISCOVERY:COMPONENT_ICMP
)

foreach(rule IN LISTS COMPONENT_REQUIRES)
    string(REPLACE ":" ";" pair "${rule}")
    list(GET pair 0 user)
    list(GET pair 1 required)
    if(CONFIG_${user} AND NOT CONFIG_${required})
        message(FATAL_ERROR "CONFIG_${user} requires CONFIG_${required}")
    endif()
endforeach()

set(ENABLED_COMPONENTS)
foreach(entry IN LISTS COMPONENT_TABLE)
    string(REPLACE ":" ";" pair "${entry}")
    list(GET pair 0 component)
    list(GET pair 1 symbol)
    if(CONFIG_${symbol})
        add_subdirectory(components/${component})
        list(APPEND ENABLED_COMPONENTS ${component})
    endif()
endforeach()
message(STATUS "Enabled components: ${ENABLED_COMPONENTS}")

find_package(Threads REQUIRED)

add_executable(discovery
    main.cpp
    src/Api.cpp
    src/Discovery.cpp
    src/Identifier.cpp
    src/Json.cpp
    src/NetworkInfo.cpp
    src/Oui.cpp
    src/RangeScan.cpp
    src/Subnet.cpp
    src/SystemMonitor.cpp
)
target_include_directories(discovery PRIVATE src)
target_compile_options(discovery PRIVATE ${COMPONENT_WARNINGS})
target_link_libraries(discovery PRIVATE kconfig ${ENABLED_COMPONENTS} Threads::Threads)

add_custom_target(setcap
    COMMAND sudo setcap cap_net_raw+ep "$<TARGET_FILE:discovery>"
    DEPENDS discovery
    USES_TERMINAL
)

find_package(Python3 COMPONENTS Interpreter QUIET)
set(INSTALL_GUIDE_MD "${CMAKE_CURRENT_SOURCE_DIR}/docs/INSTALL.md")
set(INSTALL_GUIDE_PDF "${CMAKE_CURRENT_BINARY_DIR}/INSTALL.pdf")
set(HAVE_REPORTLAB FALSE)
if(Python3_Interpreter_FOUND)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import reportlab"
        RESULT_VARIABLE reportlab_missing
        OUTPUT_QUIET ERROR_QUIET
    )
    if(reportlab_missing EQUAL 0)
        set(HAVE_REPORTLAB TRUE)
    endif()
endif()
if(HAVE_REPORTLAB)
    add_custom_command(
        OUTPUT "${INSTALL_GUIDE_PDF}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/packaging/md2pdf.py"
                "${INSTALL_GUIDE_MD}" "${INSTALL_GUIDE_PDF}"
        DEPENDS "${INSTALL_GUIDE_MD}" "${CMAKE_CURRENT_SOURCE_DIR}/packaging/md2pdf.py"
        COMMENT "Rendering installation guide PDF"
    )
    add_custom_target(guide ALL DEPENDS "${INSTALL_GUIDE_PDF}")
    install(FILES "${INSTALL_GUIDE_PDF}" DESTINATION share/doc/discovery COMPONENT docs)
else()
    message(STATUS "Python 3 with reportlab not found; INSTALL.pdf will not be generated")
endif()

# Install components:
#   runtime  - the binary (every package)
#   docs     - README / installation guide (every package)
#   tarball  - install.sh / uninstall.sh and the unit template (TGZ only)
# The DEB package is runtime only (binary + docs, no service).
# packaging/debian/cpack-config.cmake picks the components per generator.
install(TARGETS discovery RUNTIME DESTINATION bin COMPONENT runtime)
install(FILES README.md "${INSTALL_GUIDE_MD}" DESTINATION share/doc/discovery COMPONENT docs)
install(FILES packaging/discovery.service DESTINATION share/discovery COMPONENT tarball)
install(PROGRAMS packaging/install.sh packaging/uninstall.sh DESTINATION . COMPONENT tarball)

set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_NAME discovery)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Device Discovery")
set(CPACK_PACKAGE_CONTACT "Device Discovery <discovery@example.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Subnet device discovery with a web UI")
set(CPACK_PACKAGE_DESCRIPTION "Scans the local IPv4 subnet with multi-threaded ICMP pings, guesses each\ndevice's type and operating system, keeps watching every device and shows\nthe result in a web page served on port 8080.")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGING_INSTALL_PREFIX "")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_STRIP_FILES ON)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/cpack-config.cmake")
# Component-aware install so each generator can pick its components, but
# still emit a single package per generator.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)

# Debian package (cpack -G DEB, or the `deb` target below)
execute_process(
    COMMAND dpkg --print-architecture
    OUTPUT_VARIABLE DEB_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
)
if(NOT DEB_ARCH)
    set(DEB_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${DEB_ARCH}")
set(CPACK_DEBIAN_PACKAGE_SECTION net)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/mikecpp/discovery")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcap2-bin")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "iputils-ping")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)

# `deb` target: build the .deb and copy it to the source tree root.
set(DEB_FILE "${CPACK_PACKAGE_NAME}_${PROJECT_VERSION}_${DEB_ARCH}.deb")
add_custom_target(deb
    COMMAND "${CMAKE_CPACK_COMMAND}" -G DEB --config "${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_BINARY_DIR}/${DEB_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/${DEB_FILE}"
    COMMAND "${CMAKE_COMMAND}" -E echo "Wrote ${CMAKE_CURRENT_SOURCE_DIR}/${DEB_FILE}"
    DEPENDS discovery
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Building Debian package"
    VERBATIM
)
