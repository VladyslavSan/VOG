cmake_minimum_required(VERSION 3.10.0)

set(VMA_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/vma/vma_configure.h)

add_library(vma INTERFACE ${VMA_HEADERS})

target_include_directories(vma INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)