# VOG

C++20 Vulkan rendering engine. Module layering (each links only downward):
`Common → Math / CVarSystem → Graphics (Vulkan layer) → Engine (renderer, scene) → Application (SDL3 shell) → Examples`.

## Critical: resource ownership mimics the Metal API

Recording a resource into a command buffer/recorder makes that entity share
ownership until the GPU finished using it (fence-keyed release on frame-slot
reuse). Letting a `shared_ptr` die at scope end is safe even for in-flight
resources — do NOT diagnose "destroyed while GPU in use" bugs or add deletion
queues / wait-idle calls before tracing the retention chain. Any new recording
API must retain its resource via `CommandBuffer::addBoundResource`.
Details: `docs/ResourceOwnership.md`.

## Build & test

- Configure once: `cmake --preset debug` (Conan-generated presets; full
  dependency setup is `conan build .`)
- Build: `cmake --build build/build/Debug`
- Test: `ctest --test-dir build/build/Debug --output-on-failure` (23+ gtest
  units; Vulkan tests run headless via `VulkanFixture`)
- Run example: `./build/build/Debug/src/VOG/Examples/ApplicationExample/ApplicationExample --shader-storage-path src/VOG/Graphics/resources/shaders`

## Conventions

- Members `mCamelCase`; constants `kCamelCase`/`gCamelCase`; enums `eValue`.
- clang-format + clang-tidy enforced by CI (cpp-linter); format before pushing.
- Conventional-commit style subjects (`fix:`, `feat:`, `ci:`, `refactor:`).
- Tests mirror `<Module>/Test/Unit/`, target name `VOG.<Module>.Unit`.
