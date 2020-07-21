cmake_minimum_required(VERSION 3.10.0)

file(GLOB_RECURSE ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE ENGINE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")

add_library(VOG.Engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})
set_target_properties(VOG.Engine PROPERTIES FOLDER "VOG")

target_link_libraries(VOG.Engine PUBLIC VOG.Common)
target_link_libraries(VOG.Engine PRIVATE VOG.Graphics)

target_include_directories(VOG.Engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/)
target_include_directories(VOG.Engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)