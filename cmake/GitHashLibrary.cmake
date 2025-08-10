# Create an interface library which includes a Git hash header file. 
# The macro that contains the Git hash is GIT_HASH.

find_package(Git QUIET)

set(CURRENT_LIST_DIR ${CMAKE_CURRENT_LIST_DIR})
if(NOT DEFINED HEADER_DIR)
    set(HEADER_DIR ${CURRENT_LIST_DIR})
endif()
if(NOT DEFINED TRANSFORMED_DIR)
    set(TRANSFORMED_DIR "${CMAKE_BINARY_DIR}/git_hash")
endif()
set(HEADER_FILE "${HEADER_DIR}/git_hash.h.in")
set(TRANSFORMED_FILE "${TRANSFORMED_DIR}/git_hash.h")

function(get_git_hash)
    if(NOT Git_FOUND)
        set(GIT_HASH "NO_GIT_REPO" PARENT_SCOPE)
        configure_file(${HEADER_FILE} ${TRANSFORMED_FILE} @ONLY)
        return()
    endif()
    
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    configure_file(${HEADER_FILE} ${TRANSFORMED_FILE} @ONLY)
endfunction()

function(create_git_hash_library)
    add_custom_target(target_get_git_hash COMMAND ${CMAKE_COMMAND}
        -DRUN_GET_GIT_HASH=1
        -DHEADER_DIR=${HEADER_DIR}
        -DTRANSFORMED_DIR=${TRANSFORMED_DIR}
        -P "${CURRENT_LIST_DIR}/GitHashLibrary.cmake"
        BYPRODUCTS ${TRANSFORMED_FILE}
    )

    add_library(git_hash_interface INTERFACE)
    target_include_directories(git_hash_interface INTERFACE ${TRANSFORMED_DIR})
    add_dependencies(git_hash_interface target_get_git_hash)

    # Prevent pre-build errors by copying the header file over initially.
    configure_file(${HEADER_FILE} ${TRANSFORMED_FILE} COPYONLY)
endfunction()

if(RUN_GET_GIT_HASH)
    get_git_hash()
endif()
