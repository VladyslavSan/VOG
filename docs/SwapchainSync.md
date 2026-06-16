# Swapchain Synchronization Design

This document captures the synchronization model used by `Vulkan::Swapchain` — the
rationale for semaphore ownership, the swap trick for per-image acquire semaphores, and
the remaining gap that `VK_EXT_swapchain_maintenance1` would close.

## The three independent indices

A Vulkan frame involves three independent indices that are easy to conflate:

1. **Frame slot** (`FrameObjectManager::mRenderFrame % framesInFlight`) — CPU-side ring
   for recycling command pools. Advanced by the renderer on every rendered frame. Gated
   by the per-slot fence (waited in `CommandBufferPool::reset`).

2. **Swapchain image index** — returned by `vkAcquireNextImageKHR`. The driver picks
   from images the presentation engine has released. Uncorrelated with the frame slot:
   with 2 frames in flight and 3 images, different slots can render to the same image on
   consecutive cycles.

3. **Present order** — `vkQueuePresentKHR` calls may complete out of order under mailbox
   or immediate modes.

The frame fence (slot gate) proves only that the **GPU** finished work for that slot.
It says nothing about the **presentation engine's** own activity.

## Semaphore ownership: both per swapchain image

Both semaphores live in `SwapchainImage`, keyed by image index.

### `imageAvailableSemaphore`

Passed to `vkAcquireNextImageKHR` and signaled by the presentation engine once it has
**finished displaying and released** the image. This IS the PE→GPU "presentation done"
signal: the PE cannot signal it until the image is free to be written to again.

The submit waits it on `eColorAttachmentOutput` stage.

### `renderFinishedSemaphore`

Signaled by the graphics submit when the frame is fully rendered. Waited by
`vkQueuePresentKHR`. Owned by `FrameObjects`, one per frame-in-flight slot.

Reuse is gated by the per-slot fence (waited in `CommandBufferPool::reset`): the fence
fires after the GPU finishes the submit, which means the signal op has completed. In
practice with `framesInFlight ≤ imageCount` (guaranteed by `minImageCount` clamping),
the presentation engine has had sufficient time to consume the signal before the slot is
recycled. Conceptually it belongs with the frame because it tracks a submission's
render-completion state, not the image's identity.

## The swap trick for `imageAvailableSemaphore`

**The chicken-and-egg problem:** you must pass the acquire semaphore *into*
`vkAcquireNextImageKHR` before you know which image index comes back.  You cannot
pre-select `mImages[K].imageAvailableSemaphore` because K is unknown.

**Solution:** maintain one spare semaphore (`mSpareAcquireSemaphore`) that is always
unsignaled and ready to pass. After the call returns K, swap the spare with
`mImages[K].imageAvailableSemaphore`:

```
vkAcquireNextImageKHR(mSpareAcquireSemaphore) → K
std::swap(mSpareAcquireSemaphore, mImages[K].imageAvailableSemaphore)
```

After the swap:

| Slot | Contents | State | Used by |
|---|---|---|---|
| `mImages[K].imageAvailableSemaphore` | freshly-signaled spare | signaled | submit waits it |
| `mSpareAcquireSemaphore` | evicted K-slot semaphore | unsignaled | next acquire |

**Why the evicted semaphore is safely unsignaled:** The driver returning K means the PE
finished displaying image K and released it. That can only happen after:

1. The prior `mImages[K].imageAvailableSemaphore` was signaled (when K was last acquired).
2. The graphics submit waited it (consuming the signal → semaphore goes unsignaled).
3. Rendering completed and `renderFinishedSemaphore[K]` was signaled.
4. `vkQueuePresentKHR` waited `renderFinishedSemaphore[K]`.
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

## The PE→CPU gap and `recreate()`

`imageAvailableSemaphore` is a **GPU-timeline** signal. The CPU has no direct
notification that the PE finished — there is no fence for this without
`VK_EXT_swapchain_maintenance1`.

`recreate()` works around this by calling `waitIdle()` before destroying the old
swapchain's resources. This is correct but causes a full pipeline stall on resize.
Eliminating it is tracked as a later milestone in the roadmap.
