find_package(Boost COMPONENTS program_options REQUIRED)

set(ApplicationExample_source_files
    src/main.cpp
    src/SwingBehaviour.hpp
    src/SwingBehaviour.cpp
)

add_executable(ApplicationExample ${ApplicationExample_source_files})

set_target_properties(ApplicationExample PROPERTIES FOLDER "VOG/EXAMPLES")
target_link_libraries(ApplicationExample
    PRIVATE
        ${PROJECT_NAME}::Application
        ${PROJECT_NAME}::Math
        Boost::program_options
)
