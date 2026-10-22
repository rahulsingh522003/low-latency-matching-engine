#ifndef MATCHING_LEVEL_H
#define MATCHING_LEVEL_H

#include "containers/intrusive_avl_tree.h"
#include "containers/intrusive_list.h"
#include "order.h"
#include "types.h"
#include "update.h"

#include <cstddef>
#include <cstdint>

namespace matching {

enum class LevelType : std::uint8_t
{
    BID = 0,
    ASK = 1
};

template <typename OutputStream>
OutputStream& operator<<(OutputStream& stream, LevelType type);

struct Level
{
    LevelType type;
    Price price;
    Quantity total_quantity;
    Quantity displayed_quantity;
    std::size_t order_count;

    Level() = delete;
    Level(LevelType level_type, Price level_price) noexcept;
    Level(const Level&) noexcept = default;
    Level(Level&&) noexcept = default;
    ~Level() noexcept = default;

    Level& operator=(const Level&) noexcept = default;
    Level& operator=(Level&&) noexcept = default;

    [[nodiscard]] bool is_bid() const noexcept { return type == LevelType::BID; }
    [[nodiscard]] bool is_ask() const noexcept { return type == LevelType::ASK; }
    [[nodiscard]] Quantity hidden_quantity() const noexcept
    {
        return total_quantity - displayed_quantity;
    }
};

template <typename OutputStream>
OutputStream& operator<<(OutputStream& stream, const Level& level);

struct LevelNode : public Level, public containers::IntrusiveAvlTree<LevelNode>::Node
{
    containers::IntrusiveList<OrderNode> orders;

    LevelNode() = delete;
    LevelNode(LevelType level_type, Price level_price) noexcept;
    LevelNode(const LevelNode&) = delete;
    LevelNode(LevelNode&&) = delete;
    ~LevelNode() noexcept = default;

    LevelNode& operator=(const LevelNode&) = delete;
    LevelNode& operator=(LevelNode&&) = delete;

    friend bool operator<(const LevelNode& left, const LevelNode& right) noexcept
    {
        return left.price < right.price;
    }
};

struct LevelUpdate
{
    UpdateType type;
    Level level;
    bool top;

    LevelUpdate(UpdateType update_type, const Level& level_snapshot, bool is_top) noexcept;
    LevelUpdate(const LevelUpdate&) noexcept = default;
    LevelUpdate(LevelUpdate&&) noexcept = default;
    ~LevelUpdate() noexcept = default;

    LevelUpdate& operator=(const LevelUpdate&) noexcept = default;
    LevelUpdate& operator=(LevelUpdate&&) noexcept = default;
};

template <typename OutputStream>
OutputStream& operator<<(OutputStream& stream, const LevelUpdate& update);

} // namespace matching

#include "level.inl"

#endif // MATCHING_LEVEL_H
