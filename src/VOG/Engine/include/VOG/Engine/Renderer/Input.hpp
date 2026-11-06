#pragma once

#include <VOG/Math/Math.hpp>

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace VOG::Engine
{
/** The keys a game reads: letters, digits, space, escape, return, the arrows and left shift. A
 *  physical key, as the scancode of the keyboard names it, not the character it types. */
enum class Key : std::uint8_t
{
    eA,
    eB,
    eC,
    eD,
    eE,
    eF,
    eG,
    eH,
    eI,
    eJ,
    eK,
    eL,
    eM,
    eN,
    eO,
    eP,
    eQ,
    eR,
    eS,
    eT,
    eU,
    eV,
    eW,
    eX,
    eY,
    eZ,
    e0,
    e1,
    e2,
    e3,
    e4,
    e5,
    e6,
    e7,
    e8,
    e9,
    eSpace,
    eEscape,
    eReturn,
    eLeft,
    eRight,
    eUp,
    eDown,
    eLeftShift
};

inline constexpr std::size_t gKeyCount = std::to_underlying(Key::eLeftShift) + 1u;

enum class MouseButton : std::uint8_t
{
    eLeft,
    eRight,
    eMiddle
};

inline constexpr std::size_t gMouseButtonCount = std::to_underlying(MouseButton::eMiddle) + 1u;

/**
 * One frame of input, as a value: what is down now, and what changed since the frame before. A
 * game reads it from FrameContext. Application fills it from the events of the window, and a test
 * builds it with a Writer, without a window.
 */
class Input
{
public:
    /** The only way to change an Input. It is a class of its own, so that a game cannot press a
     *  key it reads, and so that Engine names nothing of the code that fills the input. */
    class Writer
    {
    public:
        explicit Writer(Input& input) noexcept
            : mInput{input}
        {
        }

        /** Starts a frame: what was pressed or released in the frame before is forgotten, and
         *  what is down stays down. */
        void beginFrame() noexcept;

        /** A key that is down already is not pressed again; a key that is up is not released. */
        void press(Key key) noexcept;
        void release(Key key) noexcept;
        void press(MouseButton button) noexcept;
        void release(MouseButton button) noexcept;

        /** In pixels of the window, from its top left corner. */
        void moveMouse(Math::Vector2f position) noexcept;

    private:
        Input& mInput;
    };

    Input() noexcept = default;

    /** Whether the key is down in this frame. */
    [[nodiscard]] bool isDown(Key key) const noexcept;
    /** Whether the key went down since the frame before: true in one frame per press. */
    [[nodiscard]] bool wasPressed(Key key) const noexcept;
    /** Whether the key went up since the frame before: true in one frame per release. */
    [[nodiscard]] bool wasReleased(Key key) const noexcept;

    [[nodiscard]] bool isDown(MouseButton button) const noexcept;
    [[nodiscard]] bool wasPressed(MouseButton button) const noexcept;
    [[nodiscard]] bool wasReleased(MouseButton button) const noexcept;

    /** In pixels of the window, from its top left corner. */
    [[nodiscard]] Math::Vector2f
    mousePosition() const noexcept
    {
        return mMouse;
    }

private:
    std::bitset<gKeyCount>         mDown;
    std::bitset<gKeyCount>         mPressed;
    std::bitset<gKeyCount>         mReleased;
    std::bitset<gMouseButtonCount> mMouseDown;
    std::bitset<gMouseButtonCount> mMousePressed;
    std::bitset<gMouseButtonCount> mMouseReleased;
    Math::Vector2f                 mMouse{0.0f};
};
} // namespace VOG::Engine
