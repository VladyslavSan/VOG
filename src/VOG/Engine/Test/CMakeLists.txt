add_subdirectory(Unit)

# One translation unit for each public header, generated from the header set so that a header added
# later is covered. Each includes its header and nothing else, so a header that needs another one
# and does not include it fails here, and a unit that included all of them would hide it. Compiling
# them is the test: they link to Engine alone, whose Graphics link is private, so a public header
# that includes a VOG.Graphics header cannot find it. The macros catch a Vulkan header that a
# system install makes findable anyway.
get_target_property(publicHeaders Engine HEADER_SET_public_headers)
set(publicHeaderUnits "")
foreach(header IN LISTS publicHeaders)
    cmake_path(RELATIVE_PATH header BASE_DIRECTORY ${PROJECT_SOURCE_DIR}/src/VOG/Engine/include
               OUTPUT_VARIABLE includePath)
    string(MAKE_C_IDENTIFIER "${includePath}" unitName)
    set(unit ${CMAKE_CURRENT_BINARY_DIR}/PublicHeaders/${unitName}.cpp)
    file(GENERATE
        OUTPUT ${unit}
        CONTENT "// Generated from HEADER_SET_public_headers; see CMakeLists.txt.
#include \"${includePath}\"

#if defined(VULKAN_CORE_H_) || defined(VULKAN_HPP) || defined(VOG_DECLARE_PTR)
    #error \"The public Engine header ${includePath} includes a Vulkan or VOG.Graphics header\"
#endif
")
    list(APPEND publicHeaderUnits ${unit})
endforeach()

add_library(VOG.Engine.PublicHeadersTest OBJECT ${publicHeaderUnits})
set_target_properties(VOG.Engine.PublicHeadersTest PROPERTIES FOLDER "VOG/Tests")
target_link_libraries(VOG.Engine.PublicHeadersTest PRIVATE ${PROJECT_NAME}::Engine)

# A forward declaration of a Graphics type needs no include, so it passes the units above. This
# test finds it: no public header names `Graphics::`, `vk::`, a `Vk` type or a Graphics include.
add_test(NAME VOG.Engine.PublicHeadersNameNoGraphics
    COMMAND ${CMAKE_COMMAND}
        -DHEADER_DIRECTORY=${PROJECT_SOURCE_DIR}/src/VOG/Engine/include
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckPublicHeaders.cmake)
