#include <cassert>

namespace matching {

template <typename OutputStream>
inline OutputStream& operator<<(OutputStream& stream, LevelType type)
{
    switch (type)
    {
        case LevelType::BID:
            stream << "BID";
            break;
        case LevelType::ASK:
            stream << "ASK";
            break;
        default:
            stream << "<unknown>";
            break;
    }

    return stream;
}

inline Level::Level(LevelType level_type, Price level_price) noexcept
    : type(level_type),
      price(level_price),
      total_quantity(0),
      displayed_quantity(0),
      order_count(0)
{
    assert(((type == LevelType::BID) || (type == LevelType::ASK)) &&
           "Level type must be BID or ASK");
    assert((price > 0) && "Level price must be positive");
}

template <typename OutputStream>
inline OutputStream& operator<<(OutputStream& stream, const Level& level)
{
    stream << "Level(Type=" << level.type << "; Price=" << level.price
           << "; TotalQuantity=" << level.total_quantity
           << "; DisplayedQuantity=" << level.displayed_quantity
           << "; OrderCount=" << level.order_count << ")";
    return stream;
}

inline LevelNode::LevelNode(LevelType level_type, Price level_price) noexcept
    : Level(level_type, level_price)
{
}

inline LevelUpdate::LevelUpdate(UpdateType update_type,
                                const Level& level_snapshot,
                                bool is_top) noexcept
    : type(update_type),
      level(level_snapshot),
      top(is_top)
{
}

template <typename OutputStream>
inline OutputStream& operator<<(OutputStream& stream, const LevelUpdate& update)
{
    stream << "LevelUpdate(Type=" << update.type << "; Level=" << update.level
           << "; Top=" << (update.top ? "true" : "false") << ")";
    return stream;
}

} // namespace matching
