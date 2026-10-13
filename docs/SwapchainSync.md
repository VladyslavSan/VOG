# Swapchain Synchronization Design

This document captures the synchronization model used by `Vulkan::Swapchain` — the
rationale for semaphore ownership, the swap trick for per-image acquire semaphores, and
how `VK_EXT_swapchain_maintenance1` (required, no fallback) closes the gap between the
presentation engine and the CPU.

## The three independent indices

A Vulkan frame involves three independent indices that are easy to conflate:

1. **Frame slot** (`FrameObjectManager::mRenderFrame % framesInFlight`) — CPU-side ring
   for recycling command pools. Advanced by the renderer only when an image was acquired
   and a submit will follow (skip / out-of-date paths do not advance). Gated by the
   per-slot submit fence (waited in `CommandBufferPool::reset`, and in `~CommandBufferPool`) and by the slot's present
   fence (waited when `FrameObjects::onFrameStart` drops the slot's `Presentation`).

2. **Swapchain image index** — returned by `vkAcquireNextImageKHR`. The driver picks
   from images the presentation engine has released. Uncorrelated with the frame slot:
   with 2 frames in flight and 3 images, different slots can render to the same image on
   consecutive cycles.

3. **Present order** — `vkQueuePresentKHR` calls may complete out of order under mailbox
   or immediate modes.

The frame fence (slot gate) proves only that the **GPU** finished work for that slot.
It says nothing about the **presentation engine's** own activity.

## Semaphore ownership: per image vs per frame slot

`imageAvailableSemaphore` lives in `SwapchainImage`, keyed by image index.
`renderFinishedSemaphore` lives in `FrameObjects`, keyed by frame-in-flight slot.

### `imageAvailableSemaphore`

Passed to `vkAcquireNextImageKHR` and signaled by the presentation engine once it has
**finished displaying and released** the image. This IS the PE→GPU "presentation done"
signal: the PE cannot signal it until the image is free to be written to again.

The submit waits it on `eColorAttachmentOutput` stage. `Queue::submit` makes the submitted
command buffers retain it until their fence signals, so dropping an old swapchain
generation cannot destroy a semaphore that a pending submit still waits.

### `renderFinishedSemaphore`

Signaled by the graphics submit when the frame is fully rendered. Waited by
`vkQueuePresentKHR`. Owned by `FrameObjects`, one per frame-in-flight slot.

Reuse is gated by **two** fences, because the submit fence alone proves nothing about
the present. The submit fence fires after the GPU finished the submit, so the signal op
has completed — but `vkQueuePresentKHR` then *waits* the semaphore, and Vulkan does not
guarantee that waiting on a submit fence synchronizes with the presentation engine's
consumption of that wait. Signalling the semaphore again while the previous present may
still wait it is invalid (`VUID-vkQueueSubmit-pSignalSemaphores-00067`), however many
images the swapchain has. So `Swapchain::present()` chains a
`VkSwapchainPresentFenceInfoKHR`: its fence does not signal until the presentation engine
reset the payload of every wait semaphore. The slot keeps the `Presentation` (fence,
semaphore, swapchain generation) and drops it in `onFrameStart`, before the next submit
signals the semaphore again; dropping it waits for the fence. Conceptually the semaphore
belongs with the frame because it tracks a submission's render-completion state, not the
image's identity.

## The swap trick for `imageAvailableSemaphore`

**The chicken-and-egg problem:** you must pass the acquire semaphore *into*
`vkAcquireNextImageKHR` before you know which image index comes back.  You cannot
pre-select `images[K].imageAvailableSemaphore` because K is unknown.

**Solution:** maintain one spare semaphore (`spareAcquireSemaphore`) that is always
unsignaled and ready to pass. After the call returns K, swap the spare with
`images[K].imageAvailableSemaphore`:

```
vkAcquireNextImageKHR(spareAcquireSemaphore) → K
std::swap(spareAcquireSemaphore, images[K].imageAvailableSemaphore)
```

After the swap:

| Slot | Contents | State | Used by |
|---|---|---|---|
| `images[K].imageAvailableSemaphore` | freshly-signaled spare | signaled | submit waits it |
| `spareAcquireSemaphore` | evicted K-slot semaphore | unsignaled | next acquire |

**Why the evicted semaphore is safely unsignaled:** The driver returning K means the PE
finished displaying image K and released it. That can only happen after:

1. The prior `images[K].imageAvailableSemaphore` was signaled (when K was last acquired).
2. The graphics submit waited it (consuming the signal → semaphore goes unsignaled).
3. Rendering completed and the submitting slot's `renderFinishedSemaphore` was signaled.
4. `vkQueuePresentKHR` waited that `renderFinishedSemaphore`.
5. The PE displayed image K.
6. The PE released image K (now re-acquirable).

After step 2 the semaphore is unsignaled; it has not been touched since. The new spare
is therefore provably unsignaled and safe to hand to `vkAcquireNextImageKHR`.

### Concrete walkthrough (3 images: S[0], S[1], S[2], spare Sx)

```
Initial:  S[0]=unsig  S[1]=unsig  S[2]=unsig  Sx=unsig

Frame 1:  acquire(Sx) → K=0.  PE signals Sx.
          swap(Sx, S[0]):  S[0]=Sx(signaled)  Sx=old-S[0](unsig)
          submit waits S[0].  present waits RF[0].

Frame 2:  acquire(Sx=old-S[0]) → K=1.  PE signals it.
          swap(Sx, S[1]):  S[1]=signaled  Sx=old-S[1](unsig)
          submit waits S[1].  present waits RF[1].

Frame 3:  acquire(Sx=old-S[1]) → K=0 again.
          Re-acquiring K=0 proves frame 1's full cycle completed:
            S[0] was waited by submit (now unsignaled).
          swap(Sx, S[0]):  S[0]=signaled  Sx=old-S[0](unsig, proven safe)
          submit waits S[0].  present waits RF[0].
```

## The PE→CPU gap, present fences and `recreate()`

`imageAvailableSemaphore` is a **GPU-timeline** signal; it tells the CPU nothing about the
presentation engine. `VK_EXT_swapchain_maintenance1` adds the missing CPU-visible signal:
the **present fence**. Per the spec, the application can destroy the wait semaphores of a
presentation when its fence signalled, and can destroy a swapchain when the fences of all
past presentations that refer to it signalled.

Everything one `vkCreateSwapchainKHR` produces lives in a `SwapchainGeneration`: surface
reference, swapchain, image views, the per-image acquire semaphores and the spare. Two kinds
of holder keep a generation alive:

- a `Presentation` (returned by `Swapchain::present()`, kept by the frame slot until the
  slot restarts): holds the generation, the semaphore the present waits, and the present
  fence. Members are destroyed in reverse, so the fence — and with it the wait for the
  presentation engine — goes first;
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
valid once the image's uses completed, which the command-buffer retention guarantees.

Known gap: an image released unpresented leaves its acquire semaphore signaled or pending.
The next acquire's swap trick can then hand a signaled semaphore to `vkAcquireNextImageKHR`
(`VUID-vkAcquireNextImageKHR-semaphore-01286`), and the generation destroys a semaphore
with a signal pending (MoltenVK can crash there). Only the exception path reaches it.

The extension is required: device selection rejects devices without
`VK_EXT_swapchain_maintenance1` (the EXT name, because the KHR promotion is not exposed
everywhere yet; the types and `sType` values are identical).
