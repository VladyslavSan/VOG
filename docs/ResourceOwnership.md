# Resource Ownership Model

VOG deliberately mimics the **Metal API ownership semantics**: when a resource is
recorded into another entity (a command buffer, an encoder-like recorder), that
entity takes shared ownership of the resource and keeps it alive until the GPU
has provably finished using it. User code never manages GPU resource lifetimes
manually — letting a `shared_ptr` go out of scope is always safe, even for
resources referenced by in-flight frames.

This costs a layer of `std::shared_ptr` traffic on recording paths, and buys
safety: there is no "destroyed while the GPU still reads it" class of bugs for
anything that flows through the tracked APIs.

## How the chain works

1. **Recording retains.** Recording APIs accept `shared_ptr`s and store them on
   the command buffer:
   - `CommandBuffer::bindVertexBuffers` inserts the buffer into
     `mBoundVertexBuffers` (`Graphics/Vulkan/CommandBuffer.hpp`).
   - `CommandBufferRecorder::beginRenderPass` registers the render pass and
     framebuffer, `bindPipeline` registers the pipeline — all via
     `CommandBuffer::addBoundResource` (`Graphics/Vulkan/CommandBufferRecorder.cpp`).

2. **Submission retains the command buffer.** `Queue::submit` pairs the
   submitted `CommandBuffer` with a `FenceHandle` and parks it in the command
   buffer pool's submitted list (`Graphics/Vulkan/Queue.hpp`,
   `Graphics/Vulkan/CommandBufferPool.cpp`). The command buffer — and therefore
   everything it retains — stays alive while the GPU works.

3. **Fence wait releases.** When a frame slot is re-acquired
   (`FrameObjectManager::acquireNextFrame` → `FrameObjects::onFrameStart` →
   `CommandBufferPool::reset`), the pool **waits on the fences first**, then
   calls `CommandBuffer::reset`, which clears the bound-resource sets. Only at
   that point do retained resources drop their reference and (possibly) die.

The release point is therefore exactly the earliest provably-safe moment:
after the fence of the frame that referenced the resource has signaled.

## Rules when extending the API

- Any new recording call that references a GPU resource **must** accept a
  `shared_ptr` and register it via `CommandBuffer::addBoundResource` (or a
  dedicated tracked container, like vertex buffers use). Raw `vk::` handles may
  only be used transiently inside the call.
- Do not add manual `vkDeviceWaitIdle`-style synchronization to "fix" lifetime
  issues — if a resource can die too early, it means an API failed to retain it.
- Do not bypass the tracked wrappers by recording into the raw
  `vk::raii::CommandBuffer` base when a resource reference is involved.

## Known gaps (intentional, to be addressed where they arise)

- **Objects never recorded into a command buffer.** Example: on swapchain
  recreation (window resize), the old `vk::SwapchainKHR` and its image views are
  referenced by the presentation engine, not by any command buffer. These need
  explicit frame-keyed retirement at the recreation site.
- **Descriptor-set-referenced resources** (future textures/UBOs): tracking must
  be extended to descriptor set binding when those appear.

## Performance note

Per-bind retention costs a `shared_ptr` copy plus a container insert on hot
recording paths. This is the accepted trade-off at current scale. If profiling
ever shows it matters (thousands of draws per frame), the escape hatch is
frame-level deletion queues paid per destruction event instead of per bind —
do not switch preemptively.
