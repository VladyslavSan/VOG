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
   the command buffer, through its one entry point,
   `CommandBuffer::retain` (`Graphics/Vulkan/CommandBuffer.hpp`), which is private:
   `CommandBufferRecorder` (recording) and `Queue::submit` (semaphores) call it.
   The command buffer keeps them
   in a set, so binding one pipeline 1000 times holds one reference:
   - `RenderingPass::bindVertexBuffers` retains each buffer, `bindIndexBuffer` its buffer, and
     `RenderingPass::bindPipeline` the pipeline
     (`Graphics/Vulkan/CommandBufferRecorder.cpp`).
   - `CommandBufferRecorder::beginRendering` retains every color and depth
     attachment.
   - `setImageBarrier` / `setBufferBarrier` retain the passed attachment or
     buffer. `setImageBarrier` fills `barrier.image` from the attachment.
   - `CommandBufferRecorder::bindDescriptorHeap` retains the `DescriptorHeap`.
     `RenderingPass::useResource` retains a `Texture` or a `UniformBlock` that a draw
     reads by heap index, and `copyBufferToTexture` retains the staging buffer and the
     texture.

2. **Submission retains the command buffer.** `Queue::submit` consumes
   `ExecutableCommandBuffer`s (what `CommandBufferRecorder::finish()` returns).
   It makes each retain the wait and signal `Semaphore`s, so the caller can
   drop them, then calls `vkQueueSubmit2` with a `FenceHandle` whose fence
   becomes pending only if the call returned, then parks each `CommandBuffer`
   with that handle in its pool's submitted list (`Graphics/Vulkan/Queue.cpp`,
   `Graphics/Vulkan/CommandBufferPool.cpp`). If the submit throws, nothing is
   parked and the caller still owns the executables; dropping one is a
   discard, which releases everything it retained at once. The command buffer
   — and therefore everything it retains — stays alive while the GPU works.

3. **Fence wait releases.** When a frame slot is re-acquired
   (`FrameObjectManager::acquireNextFrame` → `FrameObjects::onFrameStart` →
   `CommandBufferPool::reset`), the pool **waits on the fences first**, resets
   the Vulkan pool, then calls `CommandBuffer::releaseRetained`. Only at that
   point do retained resources drop their reference and (possibly) die.
   `~CommandBufferPool` waits for its submitted fences too, so a pool that is
   dropped with work in flight never frees a command buffer the GPU still runs.

The release point is therefore exactly the earliest provably-safe moment:
after the fence of the frame that referenced the resource has signaled.

4. **Presenting retains.** A command buffer fence says nothing about the
   presentation engine. `Swapchain::present` chains a present fence
   (`VK_EXT_swapchain_maintenance1`, required) and returns a `Presentation`:
   the swapchain generation, the semaphore the present waits, and the fence.
   The frame slot keeps it and drops it in `FrameObjects::onFrameStart`;
   `~Presentation` waits the fence — before the slot signals that semaphore
   again. A `FenceHandle` waits for its fence in its destructor if the GPU may
   still signal it, as a safety net; the owners that release guarded resources
   (`CommandBufferPool::reset` and `~CommandBufferPool`, `~Presentation`) wait
   for their fences explicitly. Swapchain recreation needs
   no `waitIdle`: the old generation is retired by reference count.
   Details: `SwapchainSync.md`.

## Meshes and materials

The Engine hands out `MeshPtr` and `MaterialPtr`; the chain behind them is the same one:

- A `Mesh` owns a `BufferPtr`. The packet that `Renderer::submitFrame` queues holds each draw, and
  so its mesh, from `submitFrame` until the render thread has recorded the frame; the queue holds
  at most one packet.
- `bindVertexBuffers` and `bindIndexBuffer` retain the buffer, not the `Mesh`; the vertices and
  the indices of a mesh are in one buffer. Dropping the last `MeshPtr` while a frame
  is in flight frees the CPU object; the buffer dies when that frame's slot is reused and its
  fence has signalled (`meshBufferOutlivesMeshWhileFrameInFlight`).
- A `Material` owns a `ShaderProgramPtr`. Its parameters are copied into push constants while
  recording, so no GPU-side object is retained for them.
- The `PipelineCache` holds its pipelines until `~Renderer`; `bindPipeline` retains them per frame.

## Self-sufficient handles

The model, generalised from Metal: **any object you hold keeps alive everything
it depends on** — the object that created it, the object it wraps, and the
objects it references. Holding one handle is always enough; user code never
keeps a parent around "for" a child and never sequences teardown.
Examples, not the whole list: a `Buffer` keeps its `Device` alive; a
`CommandBufferRecorder` or `ExecutableCommandBuffer` keeps its pool (and through it the device) alive; a
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
   `Swapchain`, `RenderBuffer`, `Buffer`, `Texture`, `UniformBlock`) — callers convert to `shared_ptr`
   when they need to share or record the object. A type that hands out
   dependents (calls `shared_from_this()`: `Instance`, `Device`,
   `CommandBufferPool`, `FencePool`, `DescriptorHeap`) must be created as `shared_ptr`, since
   `shared_from_this()` on a `unique_ptr`-owned object throws
   `std::bad_weak_ptr` at runtime. Enforced, not documented: those types have
   private constructors, deleted copy and move constructors, and are created only
   through a factory returning `shared_ptr` (`Instance::create`, `Instance::makeDevice`,
   `Device::createCommandBufferPool`, `Device::createDescriptorHeap`, `FencePool::create`). `Semaphore` is a
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
   A `GraphicsPipeline` retains its `ShaderProgram`. Pipelines have no layout and a program
   owns no Vulkan object: its shaders hold the device.
   The `vk::raii` base is `private` in `Semaphore`, `TimelineSemaphore`,
   `GraphicsPipeline`, `Buffer`, `Texture`, `PhysicalDevice`, `Device`, `Instance`, `Fence`,
   `Queue` and `CommandBuffer`, with a `using` for the calls that have callers,
   so a caller cannot slice the wrapper into the bare handle and drop the
   object that keeps its dependencies alive. `Device::raii()`,
   `PhysicalDevice::raiiPhysicalDevice()` and `Instance::raii()` hand the raii
   object to the constructors of raii children (`vk::raii::Fence{device->raii(), ...}`).
6. **RAII wrappers bracket state.** A wrapper that puts its object into a state
   (recording, mapped, rendering) enters it in the constructor and leaves it in
   the destructor: `CommandBufferRecorder` begins the command buffer in its
   constructor and ends it in `finish()`, which hands out the
   `ExecutableCommandBuffer` (dropping the recorder instead discards the
   recording, and its destructor asserts that no `RenderingPass` is alive);
   `beginRendering()` returns a `RenderingPass` whose
   constructor records `vkCmdBeginRendering` and whose destructor records
   `vkCmdEndRendering`. Draw and bind commands exist only on the pass.
7. **Destruction respects external synchronization.** The last reference can
   drop where the owner drops it, so destructors must respect Vulkan's
   external-synchronization rules (e.g. command buffers vs. their pool). These
   are **render-thread only**, because they take no lock and the last
   reference returns the object to a pool or touches an externally synchronized
   queue or swapchain: `FencePool` and `FenceHandle` (its destructor waits for
   the fence, then returns it to the pool), `CommandBufferRecorder` and
   `ExecutableCommandBuffer` (their destructors discard the command buffer into
   its pool), `CommandBufferPool` (its destructor waits for the submitted
   fences), `Queue::submit`,
   `Swapchain::present`, dropping a `Swapchain::Presentation` (it holds a
   `FenceHandle`), `AcquiredSwapchainImage` (its destructor releases an
   unpresented image on the swapchain) and `FrameObjects::retainUntilPresented`.
   The `Renderer` makes this hold by ownership: it owns the frame slots, the
   fence pool and the swapchain, only its render thread uses them while it
   runs, and `~Renderer` joins that thread before it destroys them.

   **Threads.** Two threads work with a `Renderer`. The main thread (the caller of `submitFrame`)
   owns the renderables and runs `Renderable::onDraw`. The render thread owns the objects of the
   list above and runs no user code. Main and render thread share the packet queue (one slot, a
   mutex), `mRequestedExtent` (an atomic), and `mExit` and `mRenderThread` (under `mStateMutex`).
   The pending-renderable list is shared between the callers of `addRenderable` and the submitting
   thread; it has a mutex. The `Scene`, its `GameObject`s and its `Behaviour`s are main-thread
   state: `Application` updates the scene and runs `onDraw` on the main thread, and only the packet
   crosses to the render thread. `createMesh` and `createMaterial` are safe from any thread: VMA is
   created without `EXTERNALLY_SYNCHRONIZED`, so the allocator locks internally, and the shader
   cache is only read after its constructor. A packet holds meshes, materials and a camera view,
   none of the objects above, so it may die on any thread. `onDraw` may run inside an SDL event
   watch on the main thread (`SwapchainSync.md`, "Live resize"): no renderer lock is held, but it
   must not wait for a thread that pushes SDL events.

   All destructors are `noexcept`. A Vulkan call that can fail in a destructor goes through
   `inDestructor` (`Graphics/Vulkan/InDestructor.hpp`), which logs a
   `vk::SystemError` and lets the teardown continue: device loss cannot be
   recovered from there, and a logged release beats an unlogged
   `std::terminate`. `VOG_ASSERT` is allowed in a destructor only for an
   invariant.
8. **Every type is tested.** Drop the dependency, make a real Vulkan call
   through the dependent, drop it, assert the dependency expired and that the
   validation layer raised no error while the dependent was destroyed — a child
   object that outlives its device is reported at `vkDestroyDevice`
   (`DeviceLifetimeTest.cpp`, `VulkanFixture::expectDependentHoldsDevice`).
   Covered: `Device` (its `Instance`), `Buffer`, `FencePool`, `CommandBufferPool`,
   `CommandBufferRecorder`, `ExecutableCommandBuffer`, `Semaphore`,
   `TimelineSemaphore`, `Queue`,
   `RenderBuffer`, `Shader`, `GraphicsPipeline`, `DescriptorHeap`, `Texture`, `UniformBlock`,
   `FrameObjects`, `FrameObjectManager`, `ShaderProgramCache`, `Swapchain`,
   `SwapchainGeneration` (private to the library, reached through the
   `AcquiredSwapchainImage` and `Presentation` that hold it) and
   `AcquiredSwapchainImage` (`SwapchainTest.cpp`, on a headless surface via
   `VK_EXT_headless_surface`; the generation is covered by
   `acquiredImageKeepsSwapchainAlive`, which holds nothing else). `Shader`, `RenderBuffer`, `Semaphore`, `ExecutableCommandBuffer`,
   `DescriptorHeap`, `Texture` and `UniformBlock` expose no Vulkan call, so their
   destruction is the call under test (the texture and the block also expire their heap);
   for `ShaderProgramCache` it is the lookup. A `ShaderProgram` owns no Vulkan object, so it
   has no test of its own: its shaders hold the device (`Shader_keepsDeviceAlive`).
   `Device` (its `Instance`) and `Queue` are ownership-only: no Vulkan call is
   reachable through a transfer `Queue`, and the `Device` test checks the
   expiry order without the validation layer. A `FenceHandle` is only
   reachable through the pool that holds it, and that pool keeps the device alive
   on its own, so the handle's retention has no test that could fail
   independently (`submittedCommandBufferKeepsDeviceAlive` runs the path).

## Rules when extending the API

- Any new recording call that references a GPU resource **must** accept a
  `shared_ptr` and retain it through `CommandBufferRecorder`; it and
  `Queue::submit` (for semaphores) are the only callers of
  `CommandBuffer::retain`. Raw `vk::` handles may only be used transiently
  inside the call.
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
  between acquire and present (a Vulkan error, or out of memory) reaches it; see
  `SwapchainSync.md`.

- **Device vertex limits are not validated.** `createMesh` checks the stride against the
  attributes, but not against the device limits for vertex stride and attribute offset:
  `PhysicalDevice` exposes no `getProperties`.

- **The pipeline cache never evicts.** A pipeline stays until `~Renderer`, even if no mesh or
  material uses its vertex layout, shader program and color format any more.

- **Heap-referenced resources** (textures and uniform blocks on the descriptor heap): a draw that
  references one by heap index must retain it through `RenderingPass::useResource`, which
  nothing enforces. The `Renderer` draws with none yet.

- **Out-of-date and suboptimal paths are untested.** A headless surface never
  reports them, so no automated test reaches the acquire/present
  `eErrorOutOfDateKHR` / `eSuboptimalKHR` handling or the `SurfaceLostKHRError`
  rethrow in `Swapchain::present`. The example's resize and close runs on a real
  window are the only coverage.

- **Windows and macOS CI only build.** Those jobs do not run the tests.

- **Wayland and X11 have no CI.** The Linux jobs build and test on a headless
  surface; the Wayland and X11 surface paths have not run in CI.

## Performance note

Per-bind retention costs a `shared_ptr` copy plus a container insert on hot
recording paths. This is the accepted trade-off at current scale. If profiling
ever shows it matters (thousands of draws per frame), the escape hatch is
frame-level deletion queues paid per destruction event instead of per bind —
do not switch preemptively.
