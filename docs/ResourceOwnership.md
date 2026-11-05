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
     `RenderingPass::useResource` retains a `Texture` or a `HeapBuffer` that a draw
     reads by heap index. `copyBuffer`, `copyBufferToTexture` and `copyImageToBuffer` retain the staging buffer (or image)
     and the destination, and `acquire` retains the buffer or texture it takes over.

2. **Submission retains the command buffer.** `Queue::submit` consumes
   `ExecutableCommandBuffer`s (what `CommandBufferRecorder::finish()` returns).
   It makes each retain the wait and signal `Semaphore`s and `TimelineSemaphore`s, so the caller
   can drop them, then calls `vkQueueSubmit2` with a `FenceHandle` whose fence
   becomes pending only if the call returned, then parks each `CommandBuffer`
   with that handle in its pool's submitted list (`Graphics/Vulkan/Queue.cpp`,
   `Graphics/Vulkan/CommandBufferPool.cpp`). If the submit throws, nothing is
   parked and the caller still owns the executables; dropping one is a
   discard, which releases everything it retained at once. The command buffer
   — and therefore everything it retains — stays alive while the GPU works.

3. **Fence wait releases.** When a frame slot is prepared
   (`FrameObjectManager::prepareNextFrame` → `FrameObjects::onFrameStart` →
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

The Engine hands out the value handles `Mesh`, `Texture` and `Material`. A handle is a copyable
class around one private `shared_ptr<const MeshData>` (`TextureData`, `MaterialData`), so a copy
shares the data and costs no allocation; a default-constructed handle is empty. The chain behind
a handle is the same one:

- A `Mesh` shares a `MeshData`, which owns a `BufferPtr`. The packet that
  `Renderer::submitFrame` queues holds each draw, and so its mesh, from `submitFrame` until the
  render thread has recorded the frame; the queue holds at most one packet.
- `bindVertexBuffers` and `bindIndexBuffer` retain the buffer, not the `Mesh`; the vertices and
  the indices of a mesh are in one buffer. Dropping the last `Mesh` handle while a frame
  is in flight frees the CPU object; the buffer dies when that frame's slot is reused and its
  fence has signalled (`meshBufferOutlivesMeshWhileFrameInFlight`). The draw loop skips a
  pipeline or buffer bind that repeats the previous bind of its kind in the frame; the first
  bind's retain covers the rest, because all draws of a frame are in one command buffer and its
  retained set lives until the fence signals.
- A `Material` shares a `MaterialData`, which holds its `ShaderInfo` (and through it the
  `ShaderProgram`), a `HeapBuffer` that holds its parameters, the `Texture` handles that the
  parameters name, and the parameters as given. A draw pushes the heap indices of the material block
  and of the frame's Frame block (`DrawPushData`); the textures are in the material block, as heap
  indices. The draw records the blocks and the textures with `RenderingPass::useResource`, which
  retains them: dropping the last `Material` handle while a frame is in flight frees the CPU object,
  and the block and the images die when that frame's slot is reused
  (`textureOutlivesMaterialWhileFrameInFlight`, `parameterBlockOutlivesMaterialWhileFrameInFlight`).
- `Renderer::createMesh` and `Renderer::createTexture` copy the bytes into a host-visible staging
  buffer on the calling thread and queue the upload (an `UploadJob`: the staging buffer, the
  destination and an `UploadTicket`) in the `UploadQueue`. Neither destination asks for host
  access, so VMA places it in device-local memory where it can. The render thread records the
  copies on a command buffer of one of the two transfer pools of the `UploadQueue` ring, in every
  iteration that has a job to record, with or without a frame, and drops the job after the transfer
  `Queue::submit` returned. The command buffer holds the staging buffer and the destination until
  the ring uses the pool again, which is two copies later (`uploadsAreRecordedAndStagingReleased`).
  A job whose destination has no handle left (`use_count() == 1`: only the job holds it) is
  dropped without a copy (`uploadsCountDownAndADroppedMeshIsNeverCopied`).
  A frame that is dropped (zero-sized window, out-of-date or skipped acquire) does not hold the
  uploads back (`uploadsGoThroughDroppedFrames`). Heap buffers stay host-visible (see "Uploads and
  the transfer queue").
- The `PipelineTable` holds its pipelines until `~Renderer`; `bindPipeline` retains them per frame.

## Uploads and the transfer queue

An iteration that records copies submits to the transfer queue, and every frame submits to the
graphics queue. Both submits come from the render thread:

1. An iteration with jobs to record takes the next of the two pools of the `UploadQueue` ring
   (command buffer pools of the transfer queue's family, never the pool of a frame slot: a frame
   slot waits for the presentation of its last frame before it can be used, so an iteration that
   only uploads and used a slot would make the frames wait for each other). It resets the pool,
   which waits for the submit of two copies ago, and records `copyBuffer` and `copyBufferToTexture`
   for every job: all the jobs whose resource a draw of the iteration uses (`ticket->referenced`),
   and the other jobs in the order they came until their bytes reach
   `Renderer::Config::backgroundUploadBytesPerFrame`. When the transfer and the graphics family
   differ each copy ends with the release half of a queue family ownership transfer.
   `Queue::submit` on the transfer queue waits for the previous value of the timeline semaphore and
   signals it with the next value (`UploadQueue::mTimeline`, `mValue`). After the submit returned,
   the tickets of the jobs hold that value: `Mesh::isResident()` is true when the semaphore has
   reached it (`TimelineSemaphore::counterValue()`, from any thread). The wait for the previous
   value is there because the two pools do not order their copies.
2. The graphics command buffer of a frame records the matching `acquire` of every copy that was
   submitted since the last frame (`UploadQueue::acquireCopied()`) before the rendering pass
   (nothing, when the families are equal: a texture is ready because its copy recorded the barrier
   to shader-read-only, and a buffer because the graphics submit waits for the copy), and the
   graphics submit waits for `UploadQueue::value()` at `ALL_COMMANDS`. The wait is in every graphics
   submit, not only in one that follows a copy: a draw of a mesh or texture that an earlier
   iteration uploaded is ordered after that copy by the wait. `ALL_COMMANDS` is the only stage that
   can wait for a queue family ownership transfer without
   `VK_DEPENDENCY_QUEUE_FAMILY_OWNERSHIP_TRANSFER_USE_ALL_STAGES_BIT_KHR`, which VOG does not
   use.

`UploadQueue::mValue` is raised after the transfer `Queue::submit` returned, so a submit that
throws leaves it unchanged; it lives as long as the renderer and only grows. `TimelineSemaphore`
holds no counter for that reason. The tickets hold the timeline semaphore, so a handle that
outlives the `Renderer` answers `isResident()` from the semaphore that the device still holds
(`handleOutlivingTheRendererStillAnswersIsResident`).

`createMesh` and `createTexture` enqueue before they return. A packet that draws a resource was
pushed after that, and the render thread, in the iteration that takes the packet, marks the tickets
of the resources of its draws as referenced before it takes the jobs, so the copy of a resource
that a frame draws is recorded in the iteration of that frame (or an earlier iteration already
recorded it), and that frame's wait covers it.

**Queue selection.** `PhysicalDevice::selectQueueFamilies` picks the graphics family and, with its
`dedicatedTransfer` argument, a transfer-only family (a DMA engine) for transfer. The device
passes `PhysicalDevice::kDedicatedTransferFamily`, which is `false`: the transfer queue is the
graphics family's queue on every device. The code for two families (the release and acquire
barriers of `UploadBarriers.hpp`, and the selection of a DMA family) is checked by value only
(`UploadBarriers.*`, `QueueFamilies.*`), because no driver the tests run on has two queue
families; it has never run on a GPU. Flip the constant after an upload has run on an NVIDIA or AMD
GPU. With one family, the two `Queue` objects wrap the same `VkQueue`, and so does the
swapchain's present queue; that is safe only because every submit and present is on the render
thread.

**Why meshes are staged and heap buffers are not.** A mesh is as large as its vertices. For a
buffer that the CPU writes sequentially and the GPU reads, VMA requires host-visible memory and
prefers device-local, which on a discrete GPU without resizable BAR is the 256 MiB BAR heap, and
VMA falls back to system RAM when it is full. A block is at most 16 KiB, and that heap holds many of
them.

**For parallel recording (S7).** The acquires stay in the first command buffer of the frame's graphics
submit. That is a conservative rule: the acquire only needs the release to have been submitted
before it.

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
   `Swapchain`, `RenderTarget`, `Buffer`, `Texture`, `HeapBuffer`) — callers convert to `shared_ptr`
   when they need to share or record the object. A type that hands out
   dependents (calls `shared_from_this()`: `Instance`, `Device`,
   `CommandBufferPool`, `FencePool`, `DescriptorHeap`) must be created as `shared_ptr`, since
   `shared_from_this()` on a `unique_ptr`-owned object throws
   `std::bad_weak_ptr` at runtime. Enforced, not documented: those types have
   private constructors, deleted copy and move constructors, and are created only
   through a factory returning `shared_ptr` (`Instance::create`, `Instance::makeDevice`,
   `Device::createCommandBufferPool`, `Device::createDescriptorHeap`, `FencePool::create`).
   `Semaphore` and `TimelineSemaphore` are leaves that `Device::createSemaphore` and
   `Device::createTimelineSemaphore` return as `shared_ptr`, because every submit shares them with
   the command buffers.
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
   `GraphicsPipeline`, `Buffer`, `Texture`, `RenderTarget`, `PhysicalDevice`, `Device`, `Instance`, `Fence`,
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
   The `Renderer` makes this hold by ownership: its implementation owns the frame slots, the
   fence pool, the swapchain and the pools of the upload ring, only its render thread uses them
   while it runs, and `~Renderer` joins that thread (`FrameLoop::stop()`) before any member of
   the implementation is destroyed. The member order of the implementation protects the
   construction, not this. The members then die in reverse: after the thread the renderables, the
   draw check and the resource factory, then the `UploadQueue`, the packet queue, the shader
   library and the `GpuContext`.

   **Threads.** Two threads work with a `Renderer`. The main thread (the caller of `submitFrame`)
   owns the renderables and runs `Renderable::onDraw`, and with it the check of
   `RenderQueue::submit` (the `DrawCheck`, which takes no lock). The render thread starts in the
   constructor and ends in the destructor; it owns the objects of the list above and runs no
   user code. Main and render thread share the packet queue (one slot, a mutex, which also carries
   the mark that uploads are waiting and the recycled vector of draws), `FrameLoop::mRequestedExtent`
   (an atomic), and the state of the render thread (an atomic) with its failure (written before the
   state becomes `eFailed`).
   The pending-renderable list is shared between the callers of `addRenderable` and the submitting
   thread; it has a mutex. The `Scene`, its `GameObject`s and its `Behaviour`s are main-thread
   state: `Application` updates the scene and runs `onDraw` on the main thread, and only the packet
   crosses to the render thread. `createMesh`, `createTexture` and `createMaterial` are safe from any thread: VMA is
   created without `EXTERNALLY_SYNCHRONIZED`, so the allocator locks internally, the
   `ShaderLibrary` is only read after its constructor, the heap's free list has a mutex, the layouts
   of the meshes are numbered under a mutex, and the jobs of the uploads are pushed to the
   `UploadQueue` under its mutex, which the render thread empties by moving the jobs out. The
   push sets the mark in the packet queue under that queue's mutex before it notifies, so the
   render thread, which waits for a packet or for uploads, cannot sleep through an upload. A packet holds meshes, materials and a camera view,
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
   `RenderTarget`, `Shader`, `GraphicsPipeline`, `DescriptorHeap`, `Texture`, `HeapBuffer`,
   `FrameObjects`, `FrameObjectManager`, `Swapchain`,
   `SwapchainGeneration` (private to the library, reached through the
   `AcquiredSwapchainImage` and `Presentation` that hold it) and
   `AcquiredSwapchainImage` (`SwapchainTest.cpp`, on a headless surface via
   `VK_EXT_headless_surface`; the generation is covered by
   `acquiredImageKeepsSwapchainAlive`, which holds nothing else). `Shader`, `RenderTarget`, `Semaphore`, `ExecutableCommandBuffer`,
   `DescriptorHeap`, `Texture` and `HeapBuffer` expose no Vulkan call, so their
   destruction is the call under test (the texture and the heap buffer also expire their heap).
   A `ShaderProgram` owns no Vulkan object, so it
   has no test of its own: its shaders hold the device (`Shader_keepsDeviceAlive`).
   `Device` (its `Instance`) and `Queue` are ownership-only: the `Queue` test holds the transfer
   handle and calls nothing through it (a submit through the queue is
   `submittedCommandBufferKeepsDeviceAlive`), and the `Device` test checks the
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
  presenting it releases the image but leaves its slot's acquire semaphore signaled or
  pending. The next acquire on that slot can then get a signaled semaphore, and
  `FrameObjects` destroys a semaphore with a signal pending. Only an exception
  between acquire and submit (a Vulkan error, or out of memory) reaches it. An exception
  between submit and present is a separate case, which leaves the slot's render-finished
  semaphore signaled; see `SwapchainSync.md`.

- **The pipeline cache never evicts.** A pipeline stays until `~Renderer`, even if no mesh or
  material uses its vertex layout, shader program and color format any more.

- **Heap-referenced resources** (textures and heap buffers on the descriptor heap): a draw that
  references one by heap index must retain it through `RenderingPass::useResource`, which
  nothing enforces. The `Renderer` calls it for the Frame block of the frame and for the material
  block and the textures of every draw (`DrawRecording::recordDraws`).

- **Out-of-date and suboptimal paths are untested.** A headless surface never
  reports them, so no automated test reaches the acquire/present
  `eErrorOutOfDateKHR` / `eSuboptimalKHR` handling or the `SurfaceLostKHRError`
  rethrow in `Swapchain::present`. The example's resize and close runs on a real
  window are the only coverage.

- **Windows and macOS CI only build.** Those jobs do not run the tests.

- **Wayland has no CI.** The Linux jobs run the Application tests on X11 under Xvfb and the
  other tests on a headless surface; the Wayland surface path has not run in CI.

## Performance note

Per-bind retention costs a `shared_ptr` copy plus a container insert on hot
recording paths. This is the accepted trade-off at current scale. If profiling
ever shows it matters (thousands of draws per frame), the escape hatch is
frame-level deletion queues paid per destruction event instead of per bind —
do not switch preemptively.
