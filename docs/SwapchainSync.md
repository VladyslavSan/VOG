# Swapchain Synchronization Design

This document captures the synchronization model used by `Vulkan::Swapchain` — the
rationale for semaphore ownership, why the acquire semaphores belong to the frame slots, and
how `VK_EXT_swapchain_maintenance1` (required, no fallback) closes the gap between the
presentation engine and the CPU.

## The three independent indices

A Vulkan frame involves three independent indices that are easy to conflate:

1. **Frame slot** (`FrameObjectManager::mRenderFrame % framesInFlight`) — CPU-side ring
   for recycling command pools and the slot's semaphores. The renderer prepares the slot
   (`FrameObjectManager::prepareNextFrame`) before it acquires an image, and advances it
   (`advance`) only when an image was acquired and a submit will follow (skip / out-of-date
   paths prepare but do not advance). Gated by the per-slot submit fence (waited in
   `CommandBufferPool::reset`, and in `~CommandBufferPool`) and by the slot's present fence
   (waited by `~Presentation`, when `FrameObjects::onFrameStart` drops the slot's
   `Presentation`). These waits have no timeout.

2. **Swapchain image index** — returned by `vkAcquireNextImageKHR`. The driver picks
   from images the presentation engine has released. Uncorrelated with the frame slot:
   with 2 frames in flight and 3 images, different slots can render to the same image on
   consecutive cycles. A headless lavapipe surface even hands out image 0 twice in a row
   (0, 1, 0, 2, 0, 1, 0, 3, ...). This is why the acquire semaphore is keyed by slot, not by image.

3. **Present order** — `vkQueuePresentKHR` calls may complete out of order under mailbox
   or immediate modes.

The frame fence (slot gate) proves only that the **GPU** finished work for that slot.
It says nothing about the **presentation engine's** own activity.

## Semaphore ownership: per frame slot

Both semaphores of a frame live in its `FrameObjects`, keyed by frame-in-flight slot:
`imageAvailableSemaphore` (`FrameObjects::getAcquireSemaphore()`) and `renderFinishedSemaphore`
(`FrameObjects::getRenderFinishedSemaphore()`). The swapchain owns neither.
`AcquiredSwapchainImage::imageAvailableSemaphore()` returns the semaphore that was passed to
`Swapchain::acquireNextImage`.

### `imageAvailableSemaphore`

Passed to `vkAcquireNextImageKHR` and signaled when the image is free to be written to again:
the presentation engine may not have finished reading the image when the acquire returns its
index, so the semaphore is the signal (Vulkan spec, `vkAcquireNextImageKHR`).

The submit waits it on `eColorAttachmentOutput` stage. `Queue::submit` makes the submitted
command buffers retain it until their fence signals.

**Reuse.** The semaphore may go to `vkAcquireNextImageKHR` again only when it is unsignaled
(`VUID-vkAcquireNextImageKHR-semaphore-01286`) and has no operation pending
(`VUID-vkAcquireNextImageKHR-semaphore-01779`). The wait of the previous submit is a pending
operation until the host knows that the submit completed. The slot's submit fence is that
knowledge: `FrameObjectManager::prepareNextFrame` returns the slot only after
`CommandBufferPool::reset` waited the fences of the slot's submits, and the renderer calls it
before the acquire. So the slot's previous submit, the only one that waited the semaphore, is
complete when the semaphore is signaled again, whatever image the acquire returns and however many
frame slots and images there are.

**Why not one per image.** The index comes back from the acquire, after the semaphore went in.
An earlier design kept one semaphore per image plus a spare, swapped the spare with the one at
the returned index, and assumed that getting image K back showed that the submit which waited K's
last semaphore had completed. It does not show that to the host: the acquire may return the index
before the presentation engine is done, so the old submit may still wait the semaphore, which the
validation layer reports as 01779. It happened whenever an image came back within one cycle of the
frame slots, which lavapipe does (26 reports in 400 frames at 2 slots and 4 images).

### `renderFinishedSemaphore`

Signaled by the graphics submit when the frame is fully rendered. Waited by
`vkQueuePresentKHR`. Owned by `FrameObjects`, one per frame-in-flight slot.

Reuse is gated by **two** fences, because the submit fence alone proves nothing about
the present. The submit fence fires after the GPU finished the submit, so the signal op
has completed — but `vkQueuePresentKHR` then *waits* the semaphore, and Vulkan does not
guarantee that waiting on a submit fence synchronizes with the presentation engine's
consumption of that wait. Signalling the semaphore again while the previous present may
still wait it is invalid (`VUID-vkQueueSubmit2-semaphore-03868`), however many
images the swapchain has. So `Swapchain::present()` chains a
`VkSwapchainPresentFenceInfoKHR`: its fence does not signal until the presentation engine
reset the payload of every wait semaphore. The slot keeps the `Presentation` (fence,
semaphore, swapchain generation) and drops it in `onFrameStart`, before the next submit
signals the semaphore again; `~Presentation` waits for the fence. Conceptually the semaphore
belongs with the frame because it tracks a submission's render-completion state, not the
image's identity. The same holds for the acquire semaphore, whose reuse is gated by the submit
fence alone.

## The PE→CPU gap, present fences and `recreate()`

`imageAvailableSemaphore` is a **GPU-timeline** signal; it tells the CPU nothing about the
presentation engine. `VK_EXT_swapchain_maintenance1` adds the missing CPU-visible signal:
the **present fence**. Per the spec, the application can destroy the wait semaphores of a
presentation when its fence signalled, and can destroy a swapchain when the fences of all
past presentations that refer to it signalled.

Everything one `vkCreateSwapchainKHR` produces lives in a `SwapchainGeneration`: surface
reference, swapchain and image views. It is private to the Graphics library and constant after
construction. Two kinds of holder keep a generation alive:

- a `Presentation` (returned by `Swapchain::present()`, kept by the frame slot until the
  slot restarts): holds the generation, the semaphore the present waits, and the present
  fence, all private. `~Presentation` waits for the fence explicitly (a moved-from
  `Presentation` has none), and only then are the generation and the semaphore released.
  It is move-only: a copy would be a second owner that waits again;
- an `AcquiredSwapchainImage`, which command buffers retain like any recorded resource, so
  the generation outlives the last command buffer that recorded one of its images.

`recreate()` builds the new generation (passing the old swapchain as `oldSwapchain`) and
lets go of the old one. It does not wait for the device: the old generation dies by
reference count, when the last of its presentations and command buffers is done. Shutdown
is the same path — dropping the frame slots waits each slot's submit fence and present
fence, and the swapchain follows.

An acquired image that is dropped without being presented is returned with
`vkReleaseSwapchainImagesEXT` in the `AcquiredSwapchainImage` destructor. Presenting
releases the acquisition itself, also when the present reports `eErrorOutOfDateKHR` or
throws `SurfaceLostKHRError` (the queue operations still count as enqueued, the semaphore
wait still executes and the fence still signals; `present()` settles the fence and the
acquisition before it rethrows). Any other present error enqueues nothing, so the image
reaches the destructor, as does an exception between acquire and present. The release is
valid once the image's uses completed, which the command-buffer retention guarantees. The
out-of-date, suboptimal and surface-lost paths have no automated test (see the known gaps in
`ResourceOwnership.md`).

Known gap: an image released unpresented leaves its slot's acquire semaphore signaled or
pending. The next acquire on that slot then hands a signaled semaphore to
`vkAcquireNextImageKHR` (`VUID-vkAcquireNextImageKHR-semaphore-01286`), and `FrameObjects`
destroys a semaphore with a signal pending (MoltenVK can crash there). Only an exception between
acquire and submit reaches it (a Vulkan error, or out of memory in a retain or a fence take).
An exception between submit and present (out of memory in a fence take, or a present error that
enqueues nothing) is a separate case: the submit consumed the acquire semaphore, but the slot's
`renderFinishedSemaphore` stays signaled with no present waiting it, so the slot's next submit
signals it again. `Renderer::submitFrame` runs the renderables on the main thread, and the render
thread looks up the pipelines of a frame before it acquires, so a renderable that throws and a
vertex layout the shader cannot read both fail while no image is acquired.

## Resizing

A surface that reports `currentExtent` (macOS, Windows, X11) goes out of date or suboptimal when
its window changes size. Wayland and headless surfaces do not report a window resize, and leave the size to the
application. Both are covered by one path on the render thread. `Application` turns
`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` into `Renderer::resize(extent)`, which stores the size in an
atomic. For each packet it takes, the render thread reads it; if it differs from the last size applied, or an
acquire or a present reports out of date, `Renderer::applyExtent` calls
`Swapchain::recreate(extent)`, the only place that does, and remembers the size. Surfaces that
report their own size ignore the requested one. A zero size, from either source, makes `recreate`
return false: the renderer drops the packet, backs off for 16 ms and tries again with the next one.
A minimized window therefore still drains the packets, and the main thread keeps running its
simulation.

## Threads

The main thread runs `Renderable::onDraw` and queues one packet per frame; the render thread takes
the packets, acquires, records and presents. The queue has one slot, and `Renderer::submitFrame`
waits while it is full, so the render thread paces the main thread. If an occluded window reports
no free image, the acquire waits up to its timeout (100 ms) per frame and the simulation runs at
that rate; the main thread still pumps events between frames, so it can quit. On macOS with
MoltenVK, a fully covered window kept presenting at about 60 frames per second, after one stall of
about a second when the window became covered. That observation was made before the acquire
semaphores moved to the frame slots and the descriptor-heap requirement (MoltenVK has no
descriptor heaps), so it cannot be re-checked on this stack.

**The slot waits have no timeout.** `FrameObjectManager::prepareNextFrame` waits the slot's submit
fence and its present fence before every acquire, also for a frame whose acquire then skips. A
presentation engine that holds a present fence (an occluded or hidden window, for instance) blocks
the render thread there, and through the one-packet queue the main thread in `submitFrame`. The
design assumes that the GPU and the presentation engine make progress; the 100 ms acquire timeout
does not bound these waits.

**The render thread never needs the main thread.** The main thread can be parked in `submitFrame`
while the render thread acquires and presents, so a driver call that waits for the main thread
would deadlock. In MoltenVK's `libMoltenVK.dylib`, `mvkDispatchToMainAndWait` has two callers: the
`MVKSurface` constructor (main thread) and `screenMVK`. `initSurfaceImages` guards `screenMVK` with
an `isMainThread` check; `getRefreshCycleDuration` does not. Do not use `VK_GOOGLE_display_timing`
on the render thread, and check this again when MoltenVK is upgraded. KosmicKrisp imports no
`dispatch_sync`.

### Live resize

macOS (while a window edge is dragged) and Windows (during the size and move loop) keep the main
thread in the OS loop and do not return to `Application::run`. SDL calls its event watches from
that loop, synchronously, for every event pushed there: the 60 Hz tick as
`SDL_EVENT_WINDOW_EXPOSED` with `data1 == 1`, and `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`. `Application`
installs one watch for the duration of `run()`. It passes the size to `Renderer::resize` and, on
the tick, calls `Renderer::trySubmitFrame`, which skips the frame (and its `onDraw` calls) if the
render thread has not taken the previous one. A tick never waits. A watch frame runs
`Renderable::onDraw` under SDL's watch-list lock, so `onDraw` must not wait for a thread that
pushes SDL events. An exception from a watch frame is kept and rethrown by `run()`, because SDL
cannot unwind. Wayland and X11 have no such loop and need no watch for the tick.

The extension is required: device selection rejects devices without
`VK_EXT_swapchain_maintenance1` (the EXT name, because the KHR promotion is not exposed
everywhere yet; the types and `sType` values are identical).
