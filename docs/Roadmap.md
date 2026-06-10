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

## Next: swapchain semaphore correctness

Two latent sync issues, independent of resize (work today by coincidence of
FIFO + matched image/frame counts; unsafe under mailbox or when the driver
returns more images than `minImageCount`):

1. **Render-finished (present-wait) semaphore must be per swapchain image,
   not per frame-in-flight.** Move it from `FrameObjects` into
   `Swapchain::SwapchainImageSyncData`, indexed by the *acquired image
   index*; let `Swapchain::present()` own the wait internally (drop the
   `waitSemaphores` parameter). Reuse is safe once that image is re-acquired.
2. **Acquire semaphores must not be reused while potentially pending.**
   Either fence-gate reuse (the currently dead `SwapchainImageSyncData::fence`)
   or size the acquire ring `imageCount + 1`.
3. While in there: clamp `minImageCount` against
   `surfaceCapabilities.minImageCount/maxImageCount` (currently passed
   through unclamped — VUID violation on surfaces with a higher minimum).

## Phase 4: Vulkan 1.3 + dynamic rendering

- Bump `VK_API_VERSION_1_2` → `1_3` (Instance.cpp). MoltenVK and Mesa
  lavapipe both support 1.3.
- Enable `vk::PhysicalDeviceVulkan13Features{ .synchronization2, .dynamicRendering }`;
  drop the `VK_KHR_synchronization2` extension entry (core in 1.3).
- Add `CommandBufferRecorder::beginRendering()/endRendering()` built from
  `AttachmentInterface` (the non-legacy `GraphicsPipeline::Parameters` path
  already emits `vk::PipelineRenderingCreateInfo`).
- Delete the legacy path: `RenderPass`, `Framebuffer`,
  `Device::createRenderPass/createFramebuffer`,
  `GraphicsPipeline::ParametersLegacy`, `beginRenderPass/endRenderPass`.

## Phase 5: typed config + RenderItem seam + demo extraction

- `Renderer::Config` struct instead of round-tripping through
  `JSONContainer` (JSON stays at the file-loading edge only).
- Minimal data-driven scene seam: `RenderItem` POD (pipeline, vertex buffer,
  vertex count, push-constant blob); `Renderable` interface with
  `prepare(ResourceContext&)` (once) and `collect(const FrameContext&, vector<RenderItem>&)`
  (per frame). Scene aggregates items; only the Renderer records commands.
- Move the triangle demo into `Examples/ApplicationExample` as a
  `Renderable`; pipeline/buffer created once in `prepare()` — ends the
  per-frame pipeline creation.
- Cleanups: `VOG::Scene` namespace → `VOG::Engine`, `m_` → `m` member naming,
  Engine links the `VOG::Math` alias.

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
