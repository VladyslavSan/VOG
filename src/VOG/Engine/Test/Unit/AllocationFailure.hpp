#pragma once

#include <cstdint>

namespace VOG::Tests
{
/**
 * Makes the Nth allocation of the calling thread through operator new throw std::bad_alloc, for
 * the lifetime of the object. AllocationFailure.cpp replaces the global operator new of the test
 * binary; while nothing is armed it only allocates; aligned and malloc allocations are never
 * counted. Other threads are not affected. Sweep @p allocationsBefore from 0 up to the first value
 * that lets the code under test succeed to hit every allocation it makes.
 *
 * Disarm (destroy it) before the test allocates again to check the outcome, since the check
 * itself would be a victim.
 */
class FailAllocation
{
public:
    /** The allocation after @p allocationsBefore successful ones fails; the first is 0. */
    explicit FailAllocation(std::int64_t allocationsBefore);
    ~FailAllocation();

    FailAllocation(const FailAllocation&)            = delete;
    FailAllocation(FailAllocation&&)                 = delete;
    FailAllocation& operator=(const FailAllocation&) = delete;
    FailAllocation& operator=(FailAllocation&&)      = delete;
};
} // namespace VOG::Tests
