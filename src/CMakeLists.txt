add_executable(flex main.c bus.c cpu8086.c)
target_link_libraries(flex PUBLIC flex_interface git_hash_interface)
target_include_directories(flex PRIVATE ${PROJECT_BINARY_DIR})

configure_file(flex_version.h.in "${PROJECT_BINARY_DIR}/flex_version.h")

install(TARGETS flex DESTINATION bin)