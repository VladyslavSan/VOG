cmake_minimum_required(VERSION 3.10.0)

file(GLOB_RECURSE COMMON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE COMMON_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")

add_library(VOG.Common STATIC ${COMMON_SOURCES} ${COMMON_HEADERS})
set_target_properties(VOG.Common PROPERTIES FOLDER "VOG")

target_link_libraries(VOG.Common PUBLIC glm)

target_include_directories(VOG.Common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/)
target_include_directories(VOG.Common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)