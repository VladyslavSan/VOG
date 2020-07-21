cmake_minimum_required(VERSION 3.10.0)

add_library(shaderc STATIC IMPORTED GLOBAL)
if(WIN32)
    SET_PROPERTY(TARGET shaderc PROPERTY 
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/win64/release/shaderc_combined.lib
        )
    SET_PROPERTY(TARGET shaderc PROPERTY 
    IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/lib/win64/debug/shaderc_combined.lib
        )
elseif(UNIX)
    find_library(SHADERC_LIBRARY libshaderc_combined.a)
    if(SHADERC_LIBRARY-NOTFOUND)
        message(FATAL_ERROR "Could not find the libshaderc_combined")
    else()
        SET_PROPERTY(TARGET shaderc PROPERTY IMPORTED_LOCATION ${SHADERC_LIBRARY})
    endif()
endif()

target_include_directories(shaderc INTERFACE include)