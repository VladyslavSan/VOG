set(ApplicationExample_source_files
    src/main.cpp
    src/SpinningQuadsRenderable.hpp
    src/SpinningQuadsRenderable.cpp
)

add_executable(ApplicationExample ${ApplicationExample_source_files})

set_target_properties(ApplicationExample PROPERTIES FOLDER "VOG/EXAMPLES")
target_link_libraries(ApplicationExample
    PRIVATE
        ${PROJECT_NAME}::Application
        ${PROJECT_NAME}::Math
)
