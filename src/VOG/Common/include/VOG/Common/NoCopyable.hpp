#pragma once

namespace VOG::Common
{
class NoCopyable
{
    NoCopyable(const NoCopyable&) = delete;
    NoCopyable& operator=(const NoCopyable&) = delete;

public:
    NoCopyable() = default;
    ~NoCopyable() = default;
};
} // namespace VOG::Common
