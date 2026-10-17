# Roadmap

Working plan for evolving VOG into a rendering engine. Order matters — each
step builds on the previous. Completed work is kept for context.

## Done

- **Threading & correctness groundwork** (#8): mutex-guarded render-state
  transitions, `std::jthread` + stop tokens, render-thread exception safety,
  CVarSystem accessor fixes + unit tests.
- **Resource ownership documentation** (#9): the Metal-like
  recording-retains model, see `ResourceOwnership.md`.
- **Swapchain recreation on resize** (#10): `build()`/`recreate()` split,
  status-based acquire/present via
  `VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS`, wait-idle teardown,
  zero-extent (minimize) back-off, resizable example window.
- **Swapchain semaphore correctness**: `imageAvailableSemaphore` is per swapchain
  image (owned by `SwapchainImage`, keyed by the index returned from
  `vkAcquireNextImageKHR`), while `renderFinishedSemaphore` is per
  frame-in-flight slot (owned by `FrameObjects`, reuse gated by the slot's submit
  fence and present fence — the submit fence alone does not prove the presentation
  engine consumed the wait, see "Present retains" below).
  The swap trick solves the chicken-and-egg for acquire semaphores: one spare is
  passed to acquire, then swapped into `mImages[K]` after K is known — the
  evicted semaphore is provably unsignaled because re-acquiring K proves the
  prior full cycle completed (see `docs/SwapchainSync.md`). `minImageCount`
  clamped against `surfaceCapabilities.min/maxImageCount` (maxImageCount==0 =
  unlimited, handled explicitly); `recreate()` updates `mExtent` from surface
  capabilities before rebuilding.
- **Vulkan 1.3 + dynamic rendering** (Phase 4): instance API version bumped to
  1.3 and `vk::PhysicalDeviceVulkan13Features{ .synchronization2,
  .dynamicRendering }` enabled, so the `VK_KHR_synchronization2` extension entry
  is gone (core in 1.3). `CommandBufferRecorder::beginRendering()/endRendering()`
  build `vk::RenderingInfo` from `AttachmentInterface` attachments carrying their
  own load/store ops and clear values, retaining each one. The legacy path is
  deleted: `RenderPass`, `Framebuffer`,
  `Device::createRenderPass/createFramebuffer`,
  `GraphicsPipeline::ParametersLegacy` and `beginRenderPass/endRenderPass` are
  gone; `GraphicsPipeline::Parameters` with its `RenderpassDescription` is the
  only pipeline path.
- **Typed config + RenderItem seam + demo extraction** (Phase 5): the renderer
  takes a `Renderer::Config` struct (app/engine name, layers, extensions,
  frames in flight, shader source path) instead of round-tripping through
  `JSONContainer`, so JSON stays at the file-loading edge. Draws are described
  as data: a `RenderItem` value type (pipeline, vertex buffer, vertex count,
  push-constant blob) produced by `Renderable::prepare(ResourceContext&)`
  (once, on the render thread) and `Renderable::collect(const FrameContext&,
  vector<RenderItem>&)` (per frame) — the renderer is the only place that
  records commands. The quad demo moved to
  `Examples/ApplicationExample/SpinningQuadsRenderable`, which builds its
  buffer and pipeline once in `prepare()`, ending the per-frame pipeline
  creation; renderables are registered through `Application::addRenderable`.
  The unused `VOG::Scene` API (`Scene`, `SceneObject`) is deleted — scene
  representation is deferred until materials/meshes exist. Engine links the
  `VOG::Math` alias.

- **Ownership / sync hygiene**: Bugbot fixes (JSONContainer index bounds, FIFO
  present-mode fallback, `framesInFlight` clamped to swapchain `imageCount`,
  frame-slot advance only after successful acquire); Device no longer cycles
  with FencePool/MemoryAllocator via `DevicePtr` backrefs; `Queue::submit` parks
  CBs only after success; `CommandBufferRecorder` is the only recording surface
  (private `vk::raii::CommandBuffer` inheritance) and brackets the command
  buffer and each rendering scope with guards; `AcquiredSwapchainImage` for
  stable attachment identity; Linux Wayland/X11 WSI; the unfinished
  `DescriptorAllocator` is deleted (descriptors move to bindless).
- **Self-sufficient handles**: wrappers around Vulkan objects hold their device (see
  `ResourceOwnership.md`, "Self-sufficient handles"); `DeviceChild` base for
  `vk::raii` wrappers, leaf factories return `unique_ptr`, `Queue` is handed
  out as an aliasing `shared_ptr`, lifetime tests per type.

- **Foundations** (review stage 1): `VOG_ASSERT` is an abort-only expression; one
  `DeviceRequirements` table drives device selection and device creation; `inDestructor`
  logs a Vulkan failure in a destructor and lets teardown continue; test fixtures take
  options and skip visibly when the validation layer is missing; CI runs a Linux Debug job
  and the macOS tests.
- **Wrapper hygiene** (review stage 2): the `vk::raii` base of every wrapper is private (`Fence`, `Queue` and
  `CommandBuffer` in stage 3), and the roots re-export only the calls that have callers, plus
  `raii()` accessors; `GraphicsPipeline` is built in one step from its shader program's layout and
  the device's pipeline cache.
- **Fences and submission** (review stage 3): a fence is pending only if the submit or
  present call returned (`FenceHandle::arm`); command buffers move through recorder,
  `ExecutableCommandBuffer` and submitted, and a dropped one is reset with its pool;
  `~CommandBufferPool` waits its own fences; `Queue::submit` uses `vkQueueSubmit2` with each
  wait semaphore paired with its stage; all destructors are `noexcept`.

- **Present retains**: no `waitIdle` anywhere in production code.
  `Queue::submit` makes the submitted command buffers retain their wait and signal
  semaphores (binary `Semaphore`); `Swapchain::present` chains a present fence
  (`VK_EXT_swapchain_maintenance1`, required) and returns a `Presentation` that the
  frame slot keeps until its next `onFrameStart`; `~FenceHandle` waits for a fence the
  GPU may still signal, as a safety net; the owners wait explicitly. `recreate()` retires the old
  `SwapchainGeneration` by reference count (supersedes the swapchain-recreation
  wait-idle teardown above and the former "stall-free recreation" milestone); the
  three shutdown validation errors (`vkDestroySemaphore-05149` x2,
  `vkDestroySwapchainKHR-01282`) are gone. Review stage 4 finished it: `SwapchainGeneration`
  is private and built in one step, `Presentation` is move-only and its destructor waits the
  fence, `acquireNextImage` takes a `std::chrono` timeout, and `recreate(extent)` returns
  false for a zero size. See `SwapchainSync.md`.

- **Renderer lifecycle** (review stage 5): `Renderer` is a `final` leaf in a `unique_ptr`. The render thread
  captures `this`, owns nothing and is the last member, so `~Renderer` joins it before any GPU
  state dies and cannot run on it. `start()`, `stop()` (returns the thread's failure as an
  `exception_ptr`) and `getState()` replace `requestRenderChangeState`; `Application::run()`
  polls the state, rethrows a render-thread failure on the main thread and can be called again.
  Renderables are queued in a pending list that no callback runs under, so `prepare()` and
  `collect()` may call `addRenderable()`; a malformed `RenderItem` fails the render thread. A frame
  collects its draws before it acquires an image. `Renderer::resize()` (fed by
  `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`) and one `applyExtent` path size the swapchain, so Wayland
  and headless surfaces resize; the acquire waits 100 ms, so a hidden window neither spins nor
  blocks quitting. Engine and Application have unit tests. See `SwapchainSync.md`.

- **VMA out of the public headers** (review stage 6): no public header includes
  `vk_mem_alloc.h`, and `PublicHeadersTest`, generated from the public header set, keeps it
  that way. `Device::createBuffer` takes `AllocationParameters` with `AllocationFlags`;
  `eHostSequentialWrite` is what `Buffer::mapForWrite()` needs.
- **Docs pass** (review stage 7): current-state text checked against the code; the known
  gaps are listed in `ResourceOwnership.md`.
- **Engine API S1: meshes, materials, camera**: `Renderer::createMesh` and `createMaterial` return
  immutable `MeshPtr` and `MaterialPtr`, and `setCamera` sets an orthographic `Camera`. A
  `Renderable` appends `Draw`s (mesh, material, world matrix) in `collect()`; the renderer builds
  the pipelines in a `PipelineCache` (one per vertex layout, shader program and color format) and
  records the draws, pushing the matrix and the material parameters. `Renderable::prepare`,
  `RenderItem`, `ResourceContext` and `FrameContext::extent` are removed. Engine links Graphics
  PRIVATE, and `VOG.Engine.PublicHeadersTest` fails the build if a public Engine header includes a
  Graphics or Vulkan header. See `ResourceOwnership.md`, "Meshes and materials".
- **Engine API S2: main/render thread split**: `Renderer::submitFrame` calls `Renderable::onDraw`
  (the former `collect`) on the calling thread, the main thread, and queues the draws and the
  camera as one packet in a one-slot queue; the render thread takes the packets, records and
  presents them, and runs no user code. A renderable fills a `RenderQueue` with
  `queue.submit(mesh, material, world)` (the former `DrawList`); a draw without a mesh or a
  material is skipped. `Application::run` submits a frame per loop iteration. An SDL event watch
  passes every size change to the renderer, and submits frames while macOS and Windows run their
  live-resize loop. A mesh is checked against the inputs of the vertex shader when its pipeline is built:
  `GraphicsPipeline` throws `std::invalid_argument` for a location without an attribute or a
  numeric type that does not match. See `ResourceOwnership.md`, "Threads", and `SwapchainSync.md`,
  "Threads".
- **Engine API S3: ECS and GameObject**: `Scene` (a `Renderable`) holds a homegrown entity-component
  registry behind a `GameObject` handle: `Transform` (every object has one), `MeshRenderer` and
  `Camera` components, and `Behaviour` scripts with `start()`, `update()` and `onDestroy()`. A
  component keeps its address until it is removed: the registry stores components in pages that
  never move. `Scene::each` allows every structural change in its visitor, including `update`,
  `clear` and `createGameObject`; it does not visit an object the visitor creates or a first-type
  component it adds, it looks the other types up at each visit, and it destroys a component the
  visitor removes when the outermost `each` returns. `Application::run` calls `Scene::update` in
  each frame, also in the frames of the live-resize event watch, and `FrameContext::deltaSeconds`
  carries the step. `Scene::onDraw` extracts the draws and sets the camera with
  `RenderQueue::setCamera`; the renderer-side `Camera` is now `CameraView`, and
  `Renderer::setCamera` is removed. `~Application` clears the scene while the renderer still
  exists. See `ResourceOwnership.md`, "Threads".

## Later milestones (in rough order)

1. **Engine API (S4-S7)**: continues S3 (see Done).
   - S4: indexed meshes.
   - S5: textures and parameter blocks on descriptor heaps.
   - S6: device-local uploads through staging on the transfer queue.
   - S7: parallel recording.
2. **Render graph**: declared passes with reads/writes; derives barriers,
   layouts and transient attachment lifetimes. Deletes the hand-written
   barrier blocks in the renderer.

## Standing decisions

- **Vulkan-only**; an RHI layer can be reintroduced later if a second
  backend is ever wanted.
- **Layering of the draw API**: `VOG::Graphics` speaks Vulkan (pipelines, shaders, buffers,
  descriptors); the Engine API speaks meshes and materials and does not expose Graphics types.
  Enforced since S1: Engine links Graphics PRIVATE, and `VOG.Engine.PublicHeadersTest` fails the
  build if a public Engine header includes a Graphics or Vulkan header. `Renderer.hpp` still
  forward-declares Graphics types as private members.
- **Descriptors: descriptor heaps (`VK_EXT_descriptor_heap`) only.** lavapipe exposes the extension
  from Mesa 26.2.0, and CI currently has Mesa 25.2.8; MoltenVK 1.4.2 and KosmicKrisp (Mesa main as
  of 2026-10-15) do not expose it.
- Resource lifetimes follow the recording-retains model
  (`ResourceOwnership.md`); new recording APIs must retain through
  `CommandBufferRecorder`.
- Per-command-buffer retention stays the primary lifetime mechanism until
  profiling shows the per-bind cost matters.
