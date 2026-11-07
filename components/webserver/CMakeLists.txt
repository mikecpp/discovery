find_package(Threads REQUIRED)
find_program(NPM_EXECUTABLE npm)
if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "npm is required to build the web server frontend; install Node.js or disable COMPONENT_WEBSERVER")
endif()

set(frontend_dir "${CMAKE_CURRENT_SOURCE_DIR}/frontend")
set(frontend_dist "${CMAKE_CURRENT_BINARY_DIR}/frontend-dist")
set(frontend_packages "${frontend_dir}/node_modules/.package-lock.json")
set(embedded_assets "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedAssets.cpp")
file(GLOB_RECURSE frontend_sources CONFIGURE_DEPENDS "${frontend_dir}/src/*")

add_custom_command(
    OUTPUT "${frontend_packages}"
    COMMAND "${NPM_EXECUTABLE}" ci --no-audit --no-fund
    DEPENDS "${frontend_dir}/package.json" "${frontend_dir}/package-lock.json"
    WORKING_DIRECTORY "${frontend_dir}"
    COMMENT "Installing web frontend packages"
)
add_custom_command(
    OUTPUT "${frontend_dist}/index.html"
    COMMAND "${NPM_EXECUTABLE}" run build -- --outDir "${frontend_dist}" --emptyOutDir
    DEPENDS ${frontend_sources} "${frontend_dir}/index.html" "${frontend_dir}/vite.config.js" "${frontend_packages}"
    WORKING_DIRECTORY "${frontend_dir}"
    COMMENT "Building web frontend"
)
add_custom_command(
    OUTPUT "${embedded_assets}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT_DIR=${frontend_dist}" "-DOUTPUT=${embedded_assets}"
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    DEPENDS "${frontend_dist}/index.html" "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    COMMENT "Embedding web frontend"
)

add_library(webserver
    src/WebServer.cpp
    src/HttpMessage.cpp
    src/HttpUtil.cpp
    "${embedded_assets}"
)
add_library(components::webserver ALIAS webserver)
target_include_directories(webserver PUBLIC inc PRIVATE src)
target_compile_options(webserver PRIVATE ${COMPONENT_WARNINGS})
target_link_libraries(webserver PUBLIC Threads::Threads)
