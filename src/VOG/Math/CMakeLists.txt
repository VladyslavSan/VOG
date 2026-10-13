find_package(glm REQUIRED)

add_library(VOG.Math)
add_library(${PROJECT_NAME}::Math ALIAS VOG.Math)

set_target_properties(VOG.Math PROPERTIES
    FOLDER VOG
)

target_include_directories(VOG.Math
    PUBLIC
        include/
    PRIVATE
        src/
)

target_sources(VOG.Math
    PRIVATE
        include/VOG/Math/GLMConfig.hpp
        include/VOG/Math/Math.hpp
        include/VOG/Math/Projections.hpp
    PRIVATE
        src/VOG/Math/Projections.cpp
)

target_link_libraries(VOG.Math
    PUBLIC
        glm::glm
        ${PROJECT_NAME}::Common
)