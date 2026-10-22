namespace matching {

inline constexpr std::size_t FastHash::operator()(std::uint64_t value) const noexcept
{
    value ^= value >> 33;
    value *= 0xFF51AFD7ED558CCDULL;
    value ^= value >> 33;
    value *= 0xC4CEB9FE1A85EC53ULL;
    value ^= value >> 33;

    return static_cast<std::size_t>(value);
}

} // namespace matching
