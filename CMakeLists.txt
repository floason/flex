cmake_minimum_required(VERSION 3.12)
project(flex LANGUAGES C VERSION 0.0.0)

add_library(flex_interface INTERFACE)
target_compile_features(flex_interface INTERFACE c_std_11)
target_compile_definitions(flex_interface INTERFACE
   $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>	# Remove pesky warnings about insecure MSVSC functions.
)
target_include_directories(flex_interface INTERFACE src)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")

add_subdirectory(src)