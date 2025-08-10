add_executable(flex main.c)
target_link_libraries(flex PUBLIC flex_interface git_hash_interface)

install(TARGETS flex DESTINATION bin)