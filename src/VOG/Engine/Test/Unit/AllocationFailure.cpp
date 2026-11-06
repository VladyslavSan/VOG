#include "AllocationFailure.hpp"

#include <cstdlib>
#include <new>

namespace
{
/** Allocations that may still succeed on this thread; negative while nothing is armed. */
thread_local std::int64_t gAllocationsLeft = -1;

void*
allocate(std::size_t size)
{
    // Armed: the Nth call throws, and the counter goes to -1, which disarms it again.
    if (gAllocationsLeft >= 0)
    {
        const bool fail = gAllocationsLeft == 0;
        --gAllocationsLeft;
        if (fail)
        {
            throw std::bad_alloc{};
        }
    }
    // malloc(0) may return null; operator new must not.
    if (void* memory = std::malloc(size != 0 ? size : 1))
    {
        return memory;
    }

    throw std::bad_alloc{};
}

void
release(void* memory) noexcept
{
    std::free(memory);
}
} // namespace

namespace VOG::Tests
{
FailAllocation::FailAllocation(std::int64_t allocationsBefore)
{
    gAllocationsLeft = allocationsBefore;
}

FailAllocation::~FailAllocation() { gAllocationsLeft = -1; }
} // namespace VOG::Tests

// The global allocation functions of the test binary. The aligned forms stay the library's: the
// code under test makes no over-aligned allocation.
// NOLINTBEGIN(readability-inconsistent-declaration-parameter-name): the library chooses the names
// of these parameters, and they differ between standard libraries.
void*
operator new(std::size_t size)
{
    return allocate(size);
}

void*
operator new[](std::size_t size)
{
    return allocate(size);
}

void
operator delete(void* memory) noexcept
{
    release(memory);
}

void
operator delete[](void* memory) noexcept
{
    release(memory);
}

void
operator delete(void* memory, std::size_t /*size*/) noexcept
{
    release(memory);
}

void
operator delete[](void* memory, std::size_t /*size*/) noexcept
{
    release(memory);
}

// NOLINTEND(readability-inconsistent-declaration-parameter-name)
