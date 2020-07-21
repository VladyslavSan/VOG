cmake_minimum_required(VERSION 3.10.0)

file(GLOB_RECURSE APPLICATION_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE APPLICATION_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")

include_directories(include/)

add_library(VOG.Application STATIC ${APPLICATION_SOURCES} ${APPLICATION_HEADERS})
set_target_properties(VOG.Application PROPERTIES FOLDER "VOG")

target_link_libraries(VOG.Application PUBLIC VOG.Common)
target_link_libraries(VOG.Application PUBLIC VOG.Engine)
target_link_libraries(VOG.Application PRIVATE sfml-window)
target_include_directories(VOG.Application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/)
