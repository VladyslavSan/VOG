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
  frame-in-flight slot (owned by `FrameObjects`, reuse gated by the slot fence).
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
  as data: a `RenderItem` POD (pipeline, vertex buffer, vertex count,
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
  with FencePool/MemoryAllocator via `DevicePtr` backrefs; `createBuffer`
  returns `shared_ptr`; `Queue::submit` parks CBs only after success; closed
  `CommandBufferRecorder` (private `vk::raii::CommandBuffer` inheritance);
  `AcquiredSwapchainImage` for stable attachment identity; Linux Wayland/X11
  WSI; unfinished `DescriptorAllocator` / `TimelineSemaphore` removed from the
  public Device/CMake surface.

## Later milestones (in rough order)

1. **Render graph**: declared passes with reads/writes; derives barriers,
   layouts and transient attachment lifetimes. Deletes the hand-written
   barrier blocks in the renderer.
2. **Images & uploads**: `Image`/texture class, staging-buffer upload path on
   the so-far-unused transfer queue.
3. **Materials, meshes, descriptor strategy** (extends `RenderItem`).
4. **Scene representation** decision (ECS vs scene graph) — deferred until
   materials/meshes exist.
5. **Stall-free swapchain recreation** (only if resize hitching ever
   matters): retire old swapchain + views into a graveyard tagged with the
   current frame counter, drained in `acquireNextFrame()` once
   `framesInFlight` frames have passed — reuses the existing per-frame-slot
   fences, no new sync primitives. Not worth the complexity while resize is
   rare and wait-idle works.

## Standing decisions

- **Vulkan-only**; an RHI layer can be reintroduced later if a second
  backend is ever wanted.
- Resource lifetimes follow the recording-retains model
  (`ResourceOwnership.md`); new recording APIs must retain via
  `CommandBuffer::addBoundResource`.
- Per-command-buffer retention stays the primary lifetime mechanism until
  profiling shows the per-bind cost matters.
