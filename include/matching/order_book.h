#ifndef MATCHING_ORDER_BOOK_H
#define MATCHING_ORDER_BOOK_H

#include "containers/intrusive_avl_tree.h"
#include "level.h"
#include "memory/object_pool.h"
#include "symbol.h"
#include "types.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <optional>

namespace matching {

class MarketManager;
namespace detail {
class MarketManagerTestAccess;
class OrderBookTestAccess;
}

class OrderBook
{
    friend class MarketManager;
    friend class detail::MarketManagerTestAccess;
    friend class detail::OrderBookTestAccess;

public:
    using Levels = containers::IntrusiveAvlTree<LevelNode>;

    OrderBook(memory::ObjectPool<LevelNode>& level_pool, const Symbol& symbol) noexcept;
    OrderBook(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = delete;
    ~OrderBook() noexcept = default;

    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook& operator=(OrderBook&&) = delete;

    [[nodiscard]] const Symbol& symbol() const noexcept { return symbol_; }
    [[nodiscard]] bool empty() const noexcept { return level_count() == 0; }
    [[nodiscard]] std::size_t level_count() const noexcept
    {
        return bids_.size() + asks_.size() + buy_stops_.size() + sell_stops_.size() +
               trailing_buy_stops_.size() + trailing_sell_stops_.size();
    }

    [[nodiscard]] const Level* best_bid() const noexcept { return best_bid_; }
    [[nodiscard]] const Level* best_ask() const noexcept { return best_ask_; }
    [[nodiscard]] const Level* best_buy_stop() const noexcept { return best_buy_stop_; }
    [[nodiscard]] const Level* best_sell_stop() const noexcept { return best_sell_stop_; }
    [[nodiscard]] const Level* best_trailing_buy_stop() const noexcept
    {
        return best_trailing_buy_stop_;
    }
    [[nodiscard]] const Level* best_trailing_sell_stop() const noexcept
    {
        return best_trailing_sell_stop_;
    }

    [[nodiscard]] const Level* find_bid(Price price) const noexcept;
    [[nodiscard]] const Level* find_ask(Price price) const noexcept;
    [[nodiscard]] const Level* find_buy_stop(Price stop_price) const noexcept;
    [[nodiscard]] const Level* find_sell_stop(Price stop_price) const noexcept;
    [[nodiscard]] const Level* find_trailing_buy_stop(Price stop_price) const noexcept;
    [[nodiscard]] const Level* find_trailing_sell_stop(Price stop_price) const noexcept;

    [[nodiscard]] const Levels& bids() const noexcept { return bids_; }
    [[nodiscard]] const Levels& asks() const noexcept { return asks_; }
    [[nodiscard]] const Levels& buy_stops() const noexcept { return buy_stops_; }
    [[nodiscard]] const Levels& sell_stops() const noexcept { return sell_stops_; }
    [[nodiscard]] const Levels& trailing_buy_stops() const noexcept
    {
        return trailing_buy_stops_;
    }
    [[nodiscard]] const Levels& trailing_sell_stops() const noexcept
    {
        return trailing_sell_stops_;
    }

    template <typename OutputStream>
    friend OutputStream& operator<<(OutputStream& stream, const OrderBook& order_book);

private:
    memory::ObjectPool<LevelNode>& level_pool_;
    Symbol symbol_;

    LevelNode* best_bid_{nullptr};
    LevelNode* best_ask_{nullptr};
    Levels bids_;
    Levels asks_;

    LevelNode* best_buy_stop_{nullptr};
    LevelNode* best_sell_stop_{nullptr};
    Levels buy_stops_;
    Levels sell_stops_;

    LevelNode* best_trailing_buy_stop_{nullptr};
    LevelNode* best_trailing_sell_stop_{nullptr};
    Levels trailing_buy_stops_;
    Levels trailing_sell_stops_;

    Price last_bid_price_{0};
    Price last_ask_price_{std::numeric_limits<Price>::max()};
    Price matching_bid_price_{0};
    Price matching_ask_price_{std::numeric_limits<Price>::max()};
    Price trailing_bid_price_{0};
    Price trailing_ask_price_{std::numeric_limits<Price>::max()};

    template <typename Tree>
    [[nodiscard]] static auto find_level(Tree& levels, Price price) noexcept
        -> decltype(levels.root());

    [[nodiscard]] const LevelNode* next_level(const LevelNode& level) const noexcept;
    [[nodiscard]] LevelNode* create_level(LevelType type, Price price) noexcept;
    void delete_level(LevelNode& level) noexcept;

    [[nodiscard]] std::optional<LevelUpdate> add_order(OrderNode& order) noexcept;
    [[nodiscard]] std::optional<LevelUpdate> add_order(OrderNode& order,
                                                       LevelNode* level) noexcept;
    [[nodiscard]] LevelUpdate reduce_order(OrderNode& order, Quantity reduction) noexcept;
    [[nodiscard]] LevelUpdate delete_order(OrderNode& order) noexcept;

    [[nodiscard]] const LevelNode* next_stop_level(const LevelNode& level) const noexcept;
    [[nodiscard]] LevelNode* create_stop_level(OrderNode& order) noexcept;
    void delete_stop_level(OrderNode& order) noexcept;

    [[nodiscard]] bool add_stop_order(OrderNode& order) noexcept;
    [[nodiscard]] bool add_stop_order(OrderNode& order, LevelNode* level) noexcept;
    void reduce_stop_order(OrderNode& order, Quantity reduction) noexcept;
    void delete_stop_order(OrderNode& order) noexcept;

    [[nodiscard]] const LevelNode* next_trailing_stop_level(
        const LevelNode& level) const noexcept;
    [[nodiscard]] LevelNode* create_trailing_stop_level(OrderNode& order) noexcept;
    void delete_trailing_stop_level(OrderNode& order) noexcept;

    [[nodiscard]] bool add_trailing_stop_order(OrderNode& order) noexcept;
    [[nodiscard]] bool add_trailing_stop_order(OrderNode& order,
                                               LevelNode* level) noexcept;
    void reduce_trailing_stop_order(OrderNode& order, Quantity reduction) noexcept;
    void delete_trailing_stop_order(OrderNode& order) noexcept;

    [[nodiscard]] Price market_bid_price() const noexcept;
    [[nodiscard]] Price market_ask_price() const noexcept;
    [[nodiscard]] Price market_trailing_bid_price() const noexcept;
    [[nodiscard]] Price market_trailing_ask_price() const noexcept;
    void update_last_price(const Order& order, Price price) noexcept;
    void update_matching_price(const Order& order, Price price) noexcept;
    void reset_matching_price() noexcept;

    [[nodiscard]] Price calculate_trailing_stop_price(const Order& order) const noexcept;
    [[nodiscard]] Price calculate_trailing_stop_price(
        const Order& order,
        const OrderNode* excluded_order) const noexcept;
};

} // namespace matching

#include "order_book.inl"

#endif // MATCHING_ORDER_BOOK_H
