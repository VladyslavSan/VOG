cmake_minimum_required(VERSION 3.10.0)

set(THIRD_PARTY_NUM_THREADS_FOR_BUILD 8)

set(LOCK_FILE ${CMAKE_BINARY_DIR}/third_party/shaderc/lock.txt)
if(NOT EXISTS ${LOCK_FILE})
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/third_party/shaderc)
    execute_process(
        COMMAND ${CMAKE_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR}/shaderc
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/third_party/shaderc
        COMMAND_ERROR_IS_FATAL ANY
    )

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build .
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/third_party/shaderc
    )

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/third_party/shaderc/build --target shaderc_combined_genfile --parallel ${THIRD_PARTY_NUM_THREADS_FOR_BUILD}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/third_party/shaderc
        COMMAND_ERROR_IS_FATAL ANY
    )

    file(TOUCH ${LOCK_FILE})
endif()

add_library(shaderc STATIC IMPORTED GLOBAL)

find_library(SHADERC_LIBRARY libshaderc_combined.a libshaderc_combined.lib
    PATHS ${CMAKE_BINARY_DIR}/third_party/shaderc/build/libshaderc)

if(SHADERC_LIBRARY-NOTFOUND)
    message(FATAL_ERROR "Could not find the libshaderc_combined")
else()
    SET_PROPERTY(TARGET shaderc PROPERTY IMPORTED_LOCATION ${SHADERC_LIBRARY})
endif()

target_include_directories(shaderc INTERFACE ${CMAKE_BINARY_DIR}/third_party/shaderc/src/shaderc/libshaderc/include)