#include <VOG/Application/Application.hpp>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <stdexcept>

namespace VOG::Application::Test
{
namespace
{
bool
countEvent(void* count, SDL_Event* /*event*/)
{
    ++*static_cast<int*>(count);

    return true;
}
} // namespace

/**
 * A bad config throws before anything touches SDL. The test holds the events subsystem itself.
 * The constructor's own SDL handle would shut SDL down when it unwinds, so SDL staying initialized
 * shows that no handle was ever made, with or without a video driver. Creating a window posts
 * window events, which the event watch counts; only that count needs a video driver to see the
 * difference.
 */
TEST(Application, invalidConfigThrowsBeforeSdlInit)
{
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_EVENTS));

    int events = 0;
    SDL_AddEventWatch(countEvent, &events);

    EXPECT_THROW(Application({.shaderStoragePath = ""}), std::runtime_error);
    EXPECT_THROW(Application({.shaderStoragePath = "/does/not/exist"}), std::runtime_error);

    // No window was created (no events), and the handle that would call SDL_Quit never existed.
    EXPECT_EQ(events, 0);
    EXPECT_NE(SDL_WasInit(SDL_INIT_EVENTS), 0u);

    SDL_RemoveEventWatch(countEvent, &events);
    SDL_Quit();
}
} // namespace VOG::Application::Test
