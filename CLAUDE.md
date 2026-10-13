# VOG

C++20 Vulkan rendering engine. Module layering (each links only downward):

```
Common
  ├─ Math          (glm wrappers; consumed by Engine)
  ├─ CVarSystem    (SHARED; parked — only its unit tests link it today)
  └─ Graphics      (Vulkan wrappers; links Common only)
       └─ Engine   (Renderer, Renderable / RenderItem)
            └─ Application (SDL3 shell)
                 └─ Examples
```

`CLAUDE.md` previously implied Graphics depended on Math/CVarSystem; the CMake graph
does not. CVarSystem is built but not part of the product path until something
above Common consumes it.

## Critical: automatic resource ownership (Metal-like)

Resource management is eased by design: recording a resource into a command
buffer/recorder makes that entity share ownership until the GPU finished using
it (fence-keyed release on frame-slot reuse) — the same model the Metal API
provides. Letting a `shared_ptr` die at scope end is safe even for in-flight
resources — do NOT diagnose "destroyed while GPU in use" bugs or add deletion
queues / wait-idle calls before tracing the retention chain. Any new recording
API must retain its resource via `CommandBuffer::addBoundResource` (use
`CommandBufferRecorder`; `CommandBuffer` inherits `vk::raii::CommandBuffer`
privately).
Details: `docs/ResourceOwnership.md`.

## Build & test

- Configure once: `cmake --preset debug` (Conan-generated presets; full
  dependency setup is `conan build .`)
- Build: `cmake --build build/build/Debug`
- Test: `ctest --test-dir build/build/Debug --output-on-failure` (23+ gtest
  units; Vulkan tests run headless via `VulkanFixture`)
- Run example: `./build/build/Debug/src/VOG/Examples/ApplicationExample/ApplicationExample --shader-storage-path src/VOG/Graphics/resources/shaders`
  (Windows / macOS / Linux with Wayland or X11; Linux WSI is selected from the SDL window backend)

## Conventions

- Members `mCamelCase`; constants `kCamelCase`/`gCamelCase`; enums `eValue`.
- clang-format + clang-tidy enforced by CI (cpp-linter); format before pushing.
  CI pins clang-format/tidy **22**; local VS-bundled tools may be older.
- Conventional-commit style subjects (`fix:`, `feat:`, `ci:`, `refactor:`).
- Tests mirror `<Module>/Test/Unit/`, target name `VOG.<Module>.Unit`.
