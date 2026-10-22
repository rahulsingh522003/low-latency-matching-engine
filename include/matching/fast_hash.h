#ifndef MATCHING_FAST_HASH_H
#define MATCHING_FAST_HASH_H

#include <cstddef>
#include <cstdint>

namespace matching {

class FastHash
{
public:
    [[nodiscard]] constexpr std::size_t operator()(std::uint64_t value) const noexcept;
};

} // namespace matching

#include "fast_hash.inl"

#endif // MATCHING_FAST_HASH_H
