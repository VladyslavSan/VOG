cmake_minimum_required(VERSION 3.10.0)

file(GLOB_RECURSE GRAPHICS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE GRAPHICS_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")

find_package(Vulkan REQUIRED)

add_library(VOG.Graphics STATIC ${GRAPHICS_SOURCES} ${GRAPHICS_HEADERS})
set_target_properties(VOG.Graphics PROPERTIES FOLDER "VOG")

target_link_libraries(VOG.Graphics PRIVATE shaderc)
target_link_libraries(VOG.Graphics PUBLIC VOG.Common)

target_include_directories(VOG.Graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/)
target_include_directories(VOG.Graphics PUBLIC ${Vulkan_INCLUDE_DIR})
target_include_directories(VOG.Graphics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)
target_include_directories(VOG.Graphics PRIVATE ${PROJECT_DEPENDENCIES_DIR}/vma/)

add_subdirectory(Test)