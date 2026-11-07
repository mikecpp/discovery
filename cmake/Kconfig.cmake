function(kconfig_load config_file header_file)
    file(STRINGS "${config_file}" lines)
    set(content "#pragma once\n\n")
    foreach(line IN LISTS lines)
        if(line MATCHES "^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
            set(name "${CMAKE_MATCH_1}")
            set(value "${CMAKE_MATCH_2}")
            if(value STREQUAL "y")
                set(${name} ON PARENT_SCOPE)
                string(APPEND content "#define ${name} 1\n")
            elseif(value MATCHES "^\"(.*)\"$")
                set(${name} "${CMAKE_MATCH_1}" PARENT_SCOPE)
                string(APPEND content "#define ${name} ${value}\n")
            elseif(NOT value STREQUAL "n")
                set(${name} "${value}" PARENT_SCOPE)
                string(APPEND content "#define ${name} ${value}\n")
            endif()
        endif()
    endforeach()
    file(WRITE "${header_file}.tmp" "${content}")
    configure_file("${header_file}.tmp" "${header_file}" COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${config_file}")
endfunction()

function(kconfig_add_targets kconfig_file config_file defconfig_file)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_FOUND)
        return()
    endif()
    get_filename_component(source_tree "${kconfig_file}" DIRECTORY)
    set(KCONFIG_MENUCONFIG_STYLE
        "aquatic"
        CACHE STRING "menuconfig color style (see MENUCONFIG_STYLE in kconfiglib)")
    set(environment ${CMAKE_COMMAND} -E env "KCONFIG_CONFIG=${config_file}" "srctree=${source_tree}"
        "MENUCONFIG_STYLE=${KCONFIG_MENUCONFIG_STYLE}")
    foreach(tool menuconfig guiconfig olddefconfig)
        add_custom_target(${tool}
            COMMAND ${environment} ${Python3_EXECUTABLE} -m ${tool} "${kconfig_file}"
            WORKING_DIRECTORY "${source_tree}"
            USES_TERMINAL
        )
    endforeach()
    add_custom_target(savedefconfig
        COMMAND ${environment} ${Python3_EXECUTABLE} -m savedefconfig --kconfig "${kconfig_file}" --out "${defconfig_file}"
        WORKING_DIRECTORY "${source_tree}"
        USES_TERMINAL
    )
    add_custom_target(defconfig
        COMMAND ${CMAKE_COMMAND} -E copy "${defconfig_file}" "${config_file}"
        WORKING_DIRECTORY "${source_tree}"
    )
endfunction()
