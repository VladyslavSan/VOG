# clang tools declarations

find_program(CLANG_TIDY_COMMAND NAMES clang-tidy REQUIRED)
add_custom_target(clang-tidy-all)

# generate clang tidy target for target
# add_clang_tidy(TARGET <target_name>)
function(add_clang_tidy)
    cmake_parse_arguments(ARG "" "TARGET;FOLDER" "" ${ARGN})

    if(NOT TARGET ${ARG_TARGET})
        message(FATAL_ERROR "Target is invalid")
    endif()

    get_target_property(SOURCES ${ARG_TARGET} SOURCES)

    list(APPEND TARGETS_TO_EXTRACT_INCLUDE_DIRS ${ARG_TARGET})
    set(INCLUDE_DIRECTORIES_FINAL)
    while(TARGETS_TO_EXTRACT_INCLUDE_DIRS)
        list(POP_BACK TARGETS_TO_EXTRACT_INCLUDE_DIRS CURRENT_TARGET)
        if(NOT TARGET ${CURRENT_TARGET})
            continue()
        endif()
        
        # Add current target include dirs to final list
        get_target_property(CURR_INCLUDE_DIRECTORIES ${CURRENT_TARGET} INCLUDE_DIRECTORIES)
        if(CURR_INCLUDE_DIRECTORIES)
            string(FIND "${CURR_INCLUDE_DIRECTORIES}" "_INTERFACE" ANY_INTERFACE_FOUND)
            if(NOT ANY_INTERFACE_FOUND EQUAL -1)
                # Do nothing, just ignore build interfaces
            else()
                list(TRANSFORM CURR_INCLUDE_DIRECTORIES PREPEND -I)
                list(APPEND INCLUDE_DIRECTORIES_FINAL ${CURR_INCLUDE_DIRECTORIES})
            endif()
        endif()

        # Push current target libraries to stack
        get_target_property(CURR_LINK_LIBRARIES ${CURRENT_TARGET} LINK_LIBRARIES)
        if(CURR_LINK_LIBRARIES)
            list(APPEND TARGETS_TO_EXTRACT_INCLUDE_DIRS ${CURR_LINK_LIBRARIES})
        endif()

        set(CURR_INCLUDE_DIRECTORIES)
        set(CURR_LINK_LIBRARIES)
    endwhile()

    if(CLANG_TIDY_APPLY_FIXES)
        set(CLANG_TIDY_FIX_FLAG "-fix-errors")
    else()
        set(CLANG_TIDY_FIX_FLAG "-fix")
    endif()

    add_custom_target("${ARG_TARGET}-clang-tidy"
        COMMAND ${CLANG_TIDY_COMMAND} ${CLANG_TIDY_FIX_FLAG} ${SOURCES} -- -std=c++20 ${INCLUDE_DIRECTORIES_FINAL})
    
    if(ARG_FOLDER)
        set_target_properties("${ARG_TARGET}-clang-tidy" PROPERTIES FOLDER ${ARG_FOLDER})
    endif()
    
    add_dependencies(clang-tidy-all "${ARG_TARGET}-clang-tidy")
endfunction()