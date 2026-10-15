add_subdirectory(Unit)

# One translation unit that includes every public header, generated from the header set so that a
# header added later is covered. Compiling it is the test: it is linked to Engine alone, whose
# Graphics link is private, so a public header that includes a VOG.Graphics header cannot find it.
# The macros catch a Vulkan header that a system install makes findable anyway.
get_target_property(publicHeaders Engine HEADER_SET_public_headers)
set(publicHeaderIncludes "")
foreach(header IN LISTS publicHeaders)
    string(APPEND publicHeaderIncludes "#include \"${header}\"\n")
endforeach()
file(GENERATE
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/EnginePublicHeadersTest.cpp
    CONTENT "// Generated from HEADER_SET_public_headers; see CMakeLists.txt.
${publicHeaderIncludes}
#if defined(VULKAN_CORE_H_) || defined(VULKAN_HPP) || defined(VOG_DECLARE_PTR)
    #error \"A public Engine header includes a Vulkan or VOG.Graphics header\"
#endif
")

add_library(VOG.Engine.PublicHeadersTest OBJECT ${CMAKE_CURRENT_BINARY_DIR}/EnginePublicHeadersTest.cpp)
set_target_properties(VOG.Engine.PublicHeadersTest PROPERTIES FOLDER "VOG/Tests")
target_link_libraries(VOG.Engine.PublicHeadersTest PRIVATE ${PROJECT_NAME}::Engine)
