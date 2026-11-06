find_package(Boost COMPONENTS program_options REQUIRED)

# The game without its window and its resources: the tests run it with no renderer.
add_library(RallyGame STATIC
    src/Assets.cpp
    src/Assets.hpp
    src/Behaviours.cpp
    src/Behaviours.hpp
    src/Game.cpp
    src/Game.hpp
)
set_target_properties(RallyGame PROPERTIES FOLDER "VOG/EXAMPLES")
target_include_directories(RallyGame PUBLIC src)
target_link_libraries(RallyGame
    PUBLIC
        ${PROJECT_NAME}::Engine
        ${PROJECT_NAME}::Math
)

add_executable(RallyExample src/main.cpp)
set_target_properties(RallyExample PROPERTIES FOLDER "VOG/EXAMPLES")
target_link_libraries(RallyExample
    PRIVATE
        RallyGame
        ${PROJECT_NAME}::Application
        Boost::program_options
)

add_subdirectory(Test)
