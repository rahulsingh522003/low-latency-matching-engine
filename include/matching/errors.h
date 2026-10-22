#ifndef MATCHING_ERRORS_H
#define MATCHING_ERRORS_H

#include <cstdint>

namespace matching {

enum class ErrorCode : std::uint8_t
{
    OK = 0,
    SYMBOL_DUPLICATE,
    SYMBOL_NOT_FOUND,
    SYMBOL_IN_USE,
    ORDER_BOOK_DUPLICATE,
    ORDER_BOOK_NOT_FOUND,
    ORDER_BOOK_NOT_EMPTY,
    ORDER_DUPLICATE,
    ORDER_NOT_FOUND,
    ORDER_ID_INVALID,
    ORDER_TYPE_INVALID,
    ORDER_PARAMETER_INVALID,
    ORDER_QUANTITY_INVALID
};

template <typename OutputStream>
OutputStream& operator<<(OutputStream& stream, ErrorCode error);

} // namespace matching

#include "errors.inl"

#endif // MATCHING_ERRORS_H
