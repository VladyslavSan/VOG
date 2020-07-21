cmake_minimum_required(VERSION 3.10.0)

set(ApplicationExample_source_files src/main.cpp)

add_executable(ApplicationExample ${ApplicationExample_source_files})

set_target_properties(ApplicationExample PROPERTIES FOLDER "VOG/EXAMPLES")
target_link_libraries(ApplicationExample PRIVATE VOG.Application)