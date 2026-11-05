#pragma once

#include <VOG/Graphics/Typedefs.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace VOG::Graphics::Vulkan
{
class TimelineSemaphore;
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/**
 * Whether the copy of a mesh or of a texture to its GPU memory has finished. The handle of the
 * resource holds it, and so does the entry in the upload queue, which is how the render thread
 * tells the one from the other.
 *
 * The ticket holds the timeline semaphore that the copy signals, so a handle that outlives the
 * Renderer still answers from the semaphore, which the device keeps.
 */
struct UploadTicket
{
    /** 0 until the transfer submit of the copy returned; then the value that it signals. Written
     *  by the render thread, read by any thread. */
    std::atomic<std::uint64_t> value = 0u;

    /** Render thread only: a draw of the frame in hand uses the resource. Set in the iteration
     *  that records the copy. */
    bool referenced = false;

    /** The semaphore of every copy of the renderer; the copy of this ticket signals `value`. */
    std::shared_ptr<const Graphics::Vulkan::TimelineSemaphore> timeline;

    /**
     * Any thread. True once the semaphore reached `value`: one query to the device, with no help
     * of the render thread.
     * @throws vk::SystemError if the device is lost.
     */
    [[nodiscard]] bool isResident() const;
};
} // namespace VOG::Engine
