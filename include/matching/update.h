#ifndef MATCHING_UPDATE_H
#define MATCHING_UPDATE_H

#include <cstdint>

namespace matching {

enum class UpdateType : std::uint8_t
{
    ADD = 1,
    UPDATE = 2,
    DELETE = 3
};

template <typename OutputStream>
OutputStream& operator<<(OutputStream& stream, UpdateType type);

} // namespace matching

#include "update.inl"

#endif // MATCHING_UPDATE_H
