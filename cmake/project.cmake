# Generic project utilities
include(cmake/clang-tools.cmake)

macro(utility_declare_project)
    # declare project
    project(${ARGN})

    # declare project variables
    set(INTERFACE_DIR ${PROJECT_SOURCE_DIR}/include/)
    set(IMPLEMENTATION_DIR ${PROJECT_SOURCE_DIR}/src/)
    set(RESOURCES_DIR ${PROJECT_SOURCE_DIR}/resources/)
endmacro(utility_declare_project)

# Add new target
function(utility_add_target)
    cmake_parse_arguments(ARG "TEST;LIBRARY;EXECUTABLE" "TARGET_NAME;FOLDER" "HEADERS;SOURCES;RESOURCES;LINK_PUBLIC;LINK_PRIVATE;PUBLIC_FRAMEWORKS;PRIVATE_FRAMEWORKS" ${ARGN})
    if((NOT ARG_TARGET_NAME) AND (${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME}))
        message(FATAL_ERROR "target name is not set")
    endif()

    if(NOT ARG_TARGET_NAME)
        set(ARG_TARGET_NAME ${PROJECT_NAME})
    endif()

    if((NOT ARG_HEADERS) AND INTERFACE_DIR)
        file(GLOB_RECURSE ARG_HEADERS "${INTERFACE_DIR}/*.hpp")
    endif()

    if((NOT ARG_SOURCES) AND IMPLEMENTATION_DIR)
        file(GLOB_RECURSE ARG_SOURCES "${IMPLEMENTATION_DIR}/*.cpp" "${IMPLEMENTATION_DIR}/*.hpp")
    endif()

    if(ARG_LIBRARY)
        add_library(${ARG_TARGET_NAME} STATIC ${ARG_HEADERS} ${ARG_SOURCES})
    elseif(ARG_EXECUTABLE)
        add_executable(${ARG_TARGET_NAME} ${ARG_HEADERS} ${ARG_SOURCES})
    elseif(ARG_TEST)
        message(FATAL_ERROR "Not implemented")
    endif()

    if(ARG_RESOURCES)
        set_target_properties(${ARG_TARGET_NAME} PROPERTIES
            RESOURCE "${ARG_RESOURCES}"
        )
    endif()

    # Check that target was created
    if(NOT TARGET ${ARG_TARGET_NAME})
        message(FATAL_ERROR "Target creation failed ${ARG_TARGET_NAME}")
    endif()

    if(ARG_LINK_PUBLIC)
        target_link_libraries(${ARG_TARGET_NAME} PUBLIC ${ARG_LINK_PUBLIC})
    endif()
    if(ARG_LINK_PRIVATE)
        target_link_libraries(${ARG_TARGET_NAME} PRIVATE ${ARG_LINK_PRIVATE})
    endif()

    if(ARG_FOLDER)
        set_target_properties(${ARG_TARGET_NAME} PROPERTIES FOLDER ${ARG_FOLDER})
    endif()

    if(EXISTS ${INTERFACE_DIR})
        target_include_directories(${ARG_TARGET_NAME} PUBLIC ${INTERFACE_DIR})
    endif()

    if(EXISTS ${IMPLEMENTATION_DIR})
        target_include_directories(${ARG_TARGET_NAME} PRIVATE ${IMPLEMENTATION_DIR})
    endif()

    set_property(TARGET ${ARG_TARGET_NAME} PROPERTY INTERFACE_DIR ${INTERFACE_DIR})
    set_property(TARGET ${ARG_TARGET_NAME} PROPERTY IMPLEMENTATION_DIR ${IMPLEMENTATION_DIR})


    add_clang_tidy(TARGET ${ARG_TARGET_NAME} FOLDER "clang-tidy-checks")

endfunction(utility_add_target)