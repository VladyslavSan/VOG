#pragma once

namespace VOG::Common
{
class NoCopyable
{
public:
    NoCopyable() = default;

    NoCopyable(const NoCopyable&) = delete;
    NoCopyable(NoCopyable&&)      = default;
};
} // namespace VOG::Common
