add_library(icmp src/Icmp.cpp)
add_library(components::icmp ALIAS icmp)
target_include_directories(icmp PUBLIC inc)
target_compile_options(icmp PRIVATE ${COMPONENT_WARNINGS})
