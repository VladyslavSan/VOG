# Resource Ownership Model

VOG's core resource-management design decision: when a resource is recorded
into another entity (a command buffer, an encoder-like recorder), that entity
takes shared ownership of the resource and keeps it alive until the GPU has
provably finished using it — the same ownership model the Metal API provides.
The point is to ease resource management: user code never manages GPU resource
lifetimes manually — letting a `shared_ptr` go out of scope is always safe,
even for resources referenced by in-flight frames.

This costs a layer of `std::shared_ptr` traffic on recording paths, and buys
safety: there is no "destroyed while the GPU still reads it" class of bugs for
anything that flows through the tracked APIs.

## How the chain works

1. **Recording retains.** Recording APIs accept `shared_ptr`s and store them on
   the command buffer:
   - `CommandBufferRecorder::RenderingPass::bindVertexBuffers` inserts the
     buffer into `CommandBuffer::mBoundVertexBuffers`
     (`Graphics/Vulkan/CommandBuffer.hpp`).
   - `CommandBufferRecorder::beginRendering` registers every color and depth
     attachment, `RenderingPass::bindPipeline` registers the pipeline — all via
     `CommandBuffer::addBoundResource` (`Graphics/Vulkan/CommandBufferRecorder.cpp`).
   - `setImageBarrier` / `setBufferBarrier` retain the passed resource; prefer
     these over `unsafeSetBarriers` (raw Vulkan barriers with no retention).

2. **Submission retains the command buffer.** `Queue::submit` calls
   `vkQueueSubmit` first, then parks each `CommandBuffer` with a `FenceHandle`
   in the pool's submitted list (`Graphics/Vulkan/Queue.hpp`,
   `Graphics/Vulkan/CommandBufferPool.cpp`). If submit throws, handles are not
   parked against an unsignaled fence. The command buffer — and therefore
   everything it retains — stays alive while the GPU works. After a successful
   submit each submitted command buffer also retains the wait and signal
   `Semaphore`s, so the caller can drop them.

3. **Fence wait releases.** When a frame slot is re-acquired
   (`FrameObjectManager::acquireNextFrame` → `FrameObjects::onFrameStart` →
   `CommandBufferPool::reset`), the pool **waits on the fences first**, then
   calls `CommandBuffer::clearBoundResources`, which clears the bound-resource sets. Only at
   that point do retained resources drop their reference and (possibly) die.

The release point is therefore exactly the earliest provably-safe moment:
after the fence of the frame that referenced the resource has signaled.

4. **Presenting retains.** A command buffer fence says nothing about the
   presentation engine. `Swapchain::present` chains a present fence
   (`VK_EXT_swapchain_maintenance1`, required) and returns a `Presentation`:
   the swapchain generation, the semaphore the present waits, and the fence.
   The frame slot keeps it and drops it in `FrameObjects::onFrameStart`, which
   waits the fence — before the slot signals that semaphore again. A
   `FenceHandle` waits for its fence in its destructor if the GPU may still
   signal it, so any owner (a pool holding an unreset submission, a
   `Presentation`, a `FrameObjects`) drains on drop. Swapchain recreation needs
   no `waitIdle`: the old generation is retired by reference count.
   Details: `SwapchainSync.md`.

## Self-sufficient handles

The model, generalised from Metal: **any object you hold keeps alive everything
it depends on** — the object that created it, the object it wraps, and the
objects it references. Holding one handle is always enough; user code never
keeps a parent around "for" a child and never sequences teardown.
Examples, not the whole list: a `Buffer` keeps its `Device` alive; a
`CommandBufferHandle` keeps its pool (and through it the device) alive; a
`Device` keeps its `Instance` alive. Scope guards (`Buffer::MemoryMapping`,
`CommandBufferRecorder` and its `RenderingPass`) are the one deliberate
exception — see requirement 2.

*Recording retains* (above) is the same rule applied to the GPU: a command
buffer depends on what was recorded into it, so it retains it until its fence
has signalled.

Requirements — every VOG type that owns, wraps or references a Vulkan object:

1. **Retain every dependency.** Creator, wrapped object and referenced objects
   are held by strong reference (`shared_ptr`), transitively up to the
   `Instance`. Dropping every user reference to a dependency never invalidates
   the dependent; it stays fully usable.
2. **No borrowed references stored.** `T&`, `T*`, raw `vk::` handles and
   pointers into another object's storage may be used inside a call, never kept
   in a member — the member is the owning `shared_ptr` instead.
   *Exception — lifetime strictly nested in the referent's:* borrowing is
   allowed when the borrower provably cannot outlive what it borrows, and the
   type is non-copyable and non-movable so that stays true:
   - a scope guard, like `std::lock_guard` (`Buffer::MemoryMapping`,
     `CommandBufferRecorder`, `CommandBufferRecorder::RenderingPass`);
   - a sub-object exclusively owned by its referent and never handed out
     (`MemoryAllocator` inside `Device` holding `Device&`).

   A sub-object that is handed out is shared through an aliasing `shared_ptr`
   that owns the referent: `Device::getGraphicsQueue()` returns
   `shared_ptr<const Queue>` pointing into the `Device`, and `Queue` is
   non-copyable and stores no back-reference.
3. **Leaves are `unique_ptr`, parents are `shared_ptr`.** Factories return
   `std::unique_ptr` by default (`Shader`, `ShaderProgram`, `GraphicsPipeline`,
   `Swapchain`, `RenderBuffer`, `Buffer`) — callers convert to `shared_ptr`
   when they need to share or record the object. A type that hands out
   dependents (calls `shared_from_this()`: `Instance`, `Device`,
   `CommandBufferPool`, `FencePool`) must be created as `shared_ptr`, since
   `shared_from_this()` on a `unique_ptr`-owned object throws
   `std::bad_weak_ptr` at runtime. Enforced, not documented: those types have
   private constructors, deleted copy and move constructors, and are created only
   through a factory returning `shared_ptr` (`Instance::create`, `Instance::makeDevice`,
   `Device::createCommandBufferPool`, `FencePool::create`). `Semaphore` is a
   leaf that Device::createSemaphore returns as `shared_ptr`, because every
   submit shares it with the command buffers.
4. **No cycles.** A dependency never strongly references its dependents, so
   dropping the last reference destroys the object.
5. **Own handle first, dependencies last — by declaration order.** C++ destroys
   members in reverse declaration order, then bases in reverse order, so the
   dependency reference must be declared *before* the handle within the same
   kind: an earlier member, or an earlier base (`Buffer : Allocation,
   vk::raii::Buffer` with `Allocation` holding the `DevicePtr`). Never a
   dependency *member* next to a handle *base* — bases outlive members, which
   is what forced the hand-written `~Buffer` at 16af4c1. `DeviceChild` is that
   earlier base for types that derive from a `vk::raii` handle and need the
   device (`Fence`, `GraphicsPipeline`, `Semaphore`, `TimelineSemaphore`).
   Creation-only inputs, like the pipeline layout a `GraphicsPipeline` was built with, rely on
   `maintenance4` (enabled in `Device.cpp`), not on declaration order.
6. **RAII wrappers bracket state.** A wrapper that puts its object into a state
   (recording, mapped, rendering) enters it in the constructor and leaves it in
   the destructor: `CommandBufferRecorder` begins the command buffer in its
   constructor and ends it in `finish()` (its destructor asserts that
   `finish()` ran); `beginRendering()` returns a `RenderingPass` whose
   constructor records `vkCmdBeginRendering` and whose destructor records
   `vkCmdEndRendering`. Draw and bind commands exist only on the pass.
7. **Destruction respects external synchronization.** The last reference can
   drop where the owner drops it, so destructors must respect Vulkan's
   external-synchronization rules (e.g. command buffers vs. their pool). These
   are **render-thread only**, because they take no lock and the last
   reference returns the object to a pool or touches an externally synchronized
   queue or swapchain: `FencePool` and `FenceHandle` (its destructor waits for
   the fence, then returns it to the pool), `CommandBufferHandle` (its
   destructor returns the command buffer to its pool), `Queue::submit`,
   `Swapchain::present`, dropping a `Swapchain::Presentation` (it holds a
   `FenceHandle`), `AcquiredSwapchainImage` (its destructor releases an
   unpresented image on the swapchain) and `FrameObjects::retainUntilPresented`.
8. **Every type is tested.** Drop the dependency, make a real Vulkan call
   through the dependent, drop it, assert the dependency expired and that the
   validation layer raised no error while the dependent was destroyed — a child
   object that outlives its device is reported at `vkDestroyDevice`
   (`DeviceLifetimeTest.cpp`, `VulkanFixture::expectDependentHoldsDevice`).
   Covered: `Buffer`, `Fence`, `FenceHandle`, `FencePool`, `CommandBufferPool`,
   `CommandBufferHandle`, `Semaphore`, `TimelineSemaphore`, `Queue`,
   `RenderBuffer`, `Shader`, `ShaderProgram`, `GraphicsPipeline`,
   `FrameObjects`, `FrameObjectManager`, `ShaderProgramCache`, `Swapchain`,
   `SwapchainGeneration` and `AcquiredSwapchainImage` (`SwapchainTest.cpp`, on a
   headless surface via `VK_EXT_headless_surface`; the generation is covered by
   `acquiredImageKeepsSwapchainAlive`, which holds nothing else). `Shader`, `ShaderProgram`, `RenderBuffer` and
   `Semaphore` expose no Vulkan call, so their destruction is the call under
   test; for `ShaderProgramCache` it is the lookup.

## Rules when extending the API

- Any new recording call that references a GPU resource **must** accept a
  `shared_ptr` and register it via `CommandBuffer::addBoundResource` (or a
  dedicated tracked container, like vertex buffers use). Raw `vk::` handles may
  only be used transiently inside the call.
- Do not add manual `vkDeviceWaitIdle`-style synchronization to "fix" lifetime
  issues — if a resource can die too early, it means an API failed to retain it.
- Do not bypass the tracked wrappers by recording into the raw
  `vk::raii::CommandBuffer` base when a resource reference is involved.
  `CommandBuffer` inherits that type privately; use `CommandBufferRecorder`.

## Known gaps (intentional, to be addressed where they arise)

- **Unpresented acquired image.** Dropping an `AcquiredSwapchainImage` without
  presenting it releases the image but leaves its acquire semaphore signaled or
  pending. The next acquire's swap trick can then get a signaled spare, and the
  generation destroys a semaphore with a signal pending. Only an exception
  between acquire and present reaches it; see `SwapchainSync.md`.

- **Descriptor-set-referenced resources** (future textures/UBOs): tracking must
  be extended to descriptor set binding when those appear.

## Performance note

Per-bind retention costs a `shared_ptr` copy plus a container insert on hot
recording paths. This is the accepted trade-off at current scale. If profiling
ever shows it matters (thousands of draws per frame), the escape hatch is
frame-level deletion queues paid per destruction event instead of per bind —
do not switch preemptively.
