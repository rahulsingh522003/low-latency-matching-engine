namespace matching {

inline constexpr Symbol::Symbol(SymbolId symbol_id, const SymbolName& symbol_name) noexcept
    : id(symbol_id),
      name(symbol_name)
{
}

template <typename OutputStream>
inline OutputStream& operator<<(OutputStream& stream, const Symbol& symbol)
{
    std::size_t length = symbol.name.size();
    while ((length > 0) && ((symbol.name[length - 1] == ' ') || (symbol.name[length - 1] == '\0')))
        --length;

    stream << "Symbol(Id=" << symbol.id << "; Name=\"";
    for (std::size_t index = 0; index < length; ++index)
        stream << symbol.name[index];
    stream << "\")";

    return stream;
}

} // namespace matching
