add_executable(flex main.c)
target_link_libraries(flex PUBLIC flex_interface)

install(TARGETS flex DESTINATION bin)