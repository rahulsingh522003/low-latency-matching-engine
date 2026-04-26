#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lob
{

    using OrderId = std::uint64_t;
    using Price = std::int64_t;
    using Quantity = std::uint64_t;

    enum class Side
    {
        Buy,
        Sell
    };

    enum class OrderType
    {
        Limit,
        Market
    };

    struct Order
    {
        OrderId id;
        Side side;
        OrderType type;
        Price price;
        Quantity quantity;
    };

    struct Trade
    {
        OrderId buy_order_id;
        OrderId sell_order_id;
        Price price;
        Quantity quantity;
    };

    struct BookLevel
    {
        Price price;
        Quantity total_quantity;
    };

    using OrderList = std::list<Order>;
    using BidBook = std::map<Price, OrderList, std::greater<Price>>;
    using AskBook = std::map<Price, OrderList>;

    struct OrderLocation
    {
        Side side;
        Price price;
        OrderList::iterator iterator;
    };

    struct RestingOrderView {
        OrderId id;
        Price price;
        Quantity quantity;
    };

    class OrderBook
    {
    public:
        OrderBook() = default;  // Default constructor

        bool add_order(const Order &order);
        bool cancel_order(OrderId order_id);

        [[nodiscard]] const std::vector<Trade> &trades() const;
        void clear_trades();

        [[nodiscard]] std::optional<Price> best_bid() const;
        [[nodiscard]] std::optional<Price> best_ask() const;
        [[nodiscard]] std::optional<Price> spread() const;
        [[nodiscard]] std::vector<BookLevel> levels(Side side, std::size_t depth) const;
        [[nodiscard]] std::vector<RestingOrderView> resting_orders(Side side) const;

        [[nodiscard]] Quantity total_quantity_at_price(Side side, Price price) const;
        

    private:
        BidBook bids_;
        AskBook asks_;
        std::unordered_map<OrderId, OrderLocation> order_index_;
        std::vector<Trade> trades_;

        void process_buy_order(Order order);
        void process_sell_order(Order order);

        void match_buy_order(Order &incoming);
        void match_sell_order(Order &incoming);

        void rest_order(Order &&order);

        void remove_bid_level_if_empty(Price price);
        void remove_ask_level_if_empty(Price price);
    };

} // namespace lob