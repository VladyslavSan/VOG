find_package(GTest REQUIRED)

add_executable(VOG.CVarSystem.Unit)
set_target_properties(VOG.CVarSystem.Unit PROPERTIES FOLDER "VOG/Tests")

target_sources(VOG.CVarSystem.Unit
    PRIVATE
        src/SystemTest.cpp
)

target_link_libraries(VOG.CVarSystem.Unit
    PUBLIC
        ${PROJECT_NAME}::CVarSystem
        GTest::gtest
        GTest::gtest_main
)

# Test discovery executes the test binary, which needs VOG.CVarSystem.dll next to it.
if (WIN32)
    add_custom_command(TARGET VOG.CVarSystem.Unit POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:VOG.CVarSystem.Unit>
            $<TARGET_FILE_DIR:VOG.CVarSystem.Unit>
        COMMAND_EXPAND_LISTS
    )
endif ()

include(GoogleTest)
gtest_discover_tests(VOG.CVarSystem.Unit)
