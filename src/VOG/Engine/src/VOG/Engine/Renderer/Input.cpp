#include <VOG/Engine/Renderer/Input.hpp>

#include <utility>

namespace VOG::Engine
{
namespace
{
/** Sets @p bit in @p down, and in @p changed if it was not set. */
void
setBit(std::size_t bit, auto& down, auto& changed) noexcept
{
    if (!down[bit])
    {
        down[bit]    = true;
        changed[bit] = true;
    }
}

/** Clears @p bit in @p down, and sets it in @p changed if it was set. */
void
clearBit(std::size_t bit, auto& down, auto& changed) noexcept
{
    if (down[bit])
    {
        down[bit]    = false;
        changed[bit] = true;
    }
}
} // namespace

void
Input::Writer::beginFrame() noexcept
{
    mInput.mPressed.reset();
    mInput.mReleased.reset();
    mInput.mMousePressed.reset();
    mInput.mMouseReleased.reset();
}

void
Input::Writer::press(Key key) noexcept
{
    setBit(std::to_underlying(key), mInput.mDown, mInput.mPressed);
}

void
Input::Writer::release(Key key) noexcept
{
    clearBit(std::to_underlying(key), mInput.mDown, mInput.mReleased);
}

void
Input::Writer::press(MouseButton button) noexcept
{
    setBit(std::to_underlying(button), mInput.mMouseDown, mInput.mMousePressed);
}

void
Input::Writer::release(MouseButton button) noexcept
{
    clearBit(std::to_underlying(button), mInput.mMouseDown, mInput.mMouseReleased);
}

void
Input::Writer::moveMouse(Math::Vector2f position) noexcept
{
    mInput.mMouse = position;
}

bool
Input::isDown(Key key) const noexcept
{
    return mDown[std::to_underlying(key)];
}

bool
Input::wasPressed(Key key) const noexcept
{
    return mPressed[std::to_underlying(key)];
}

bool
Input::wasReleased(Key key) const noexcept
{
    return mReleased[std::to_underlying(key)];
}

bool
Input::isDown(MouseButton button) const noexcept
{
    return mMouseDown[std::to_underlying(button)];
}

bool
Input::wasPressed(MouseButton button) const noexcept
{
    return mMousePressed[std::to_underlying(button)];
}

bool
Input::wasReleased(MouseButton button) const noexcept
{
    return mMouseReleased[std::to_underlying(button)];
}
} // namespace VOG::Engine
