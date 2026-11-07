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
    src/Oui.cpp
    src/RangeScan.cpp
    src/Subnet.cpp
)
target_include_directories(discovery PRIVATE src)
target_compile_options(discovery PRIVATE ${COMPONENT_WARNINGS})
target_link_libraries(discovery PRIVATE kconfig ${ENABLED_COMPONENTS} Threads::Threads)

add_custom_target(setcap
    COMMAND sudo setcap cap_net_raw+ep "$<TARGET_FILE:discovery>"
    DEPENDS discovery
    USES_TERMINAL
)
