#ifndef MATCHING_SYMBOL_H
#define MATCHING_SYMBOL_H

#include "types.h"

#include <array>
#include <cstddef>
#include <type_traits>

namespace matching {

using SymbolName = std::array<char, 8>;

struct Symbol
{
    SymbolId id;
    SymbolName name;

    Symbol() = delete;
    constexpr Symbol(SymbolId symbol_id, const SymbolName& symbol_name) noexcept;

    friend constexpr bool operator==(const Symbol&, const Symbol&) noexcept = default;
};

static_assert(sizeof(SymbolName) == 8);
static_assert(sizeof(Symbol) == (sizeof(SymbolId) + sizeof(SymbolName)));
static_assert(std::is_trivially_copyable_v<Symbol>);

template <typename OutputStream>
OutputStream& operator<<(OutputStream& stream, const Symbol& symbol);

} // namespace matching

#include "symbol.inl"

#endif // MATCHING_SYMBOL_H
