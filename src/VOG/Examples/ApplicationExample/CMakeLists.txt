set(ApplicationExample_source_files src/main.cpp)

add_executable(ApplicationExample ${ApplicationExample_source_files})

set_target_properties(ApplicationExample PROPERTIES FOLDER "VOG/EXAMPLES")
target_link_libraries(ApplicationExample PRIVATE ${PROJECT_NAME}::Application)