#include "order_book.hpp"
#include <iterator>
#include <algorithm>

namespace lob
{

    const std::vector<Trade> &OrderBook::trades() const
    {
        return trades_;
    }

    void OrderBook::clear_trades()
    {
        trades_.clear();
    }

    std::optional<Price> OrderBook::best_bid() const
    {
        if (bids_.empty())
        {
            return std::nullopt;
        }

        return bids_.begin()->first;
    }

    std::optional<Price> OrderBook::best_ask() const
    {
        if (asks_.empty())
        {
            return std::nullopt;
        }

        return asks_.begin()->first;
    }

    std::optional<Price> OrderBook::spread() const
    {
        const auto bid = best_bid();
        const auto ask = best_ask();

        if (!bid.has_value() || !ask.has_value())
        {
            return std::nullopt;
        }

        return *ask - *bid;
    }

    Quantity OrderBook::total_quantity_at_price(Side side, Price price) const
    {
        Quantity total = 0;

        if (side == Side::Buy)
        {
            const auto it = bids_.find(price);
            if (it == bids_.end())
            {
                return 0;
            }

            for (const auto &order : it->second)
            {
                total += order.quantity;
            }
        }
        else
        {
            const auto it = asks_.find(price);
            if (it == asks_.end())
            {
                return 0;
            }

            for (const auto &order : it->second)
            {
                total += order.quantity;
            }
        }

        return total;
    }

    void OrderBook::rest_order(Order &&order)
    {
        if (order.quantity == 0)
        {
            return;
        }

        if (order.side == Side::Buy)
        {
            auto &level = bids_[order.price];
            level.push_back(std::move(order));

            auto it = std::prev(level.end());
            order_index_[it->id] = OrderLocation{
                Side::Buy,
                it->price,
                it};
        }
        else
        {
            auto &level = asks_[order.price];
            level.push_back(std::move(order));

            auto it = std::prev(level.end());
            order_index_[it->id] = OrderLocation{
                Side::Sell,
                it->price,
                it};
        }
    }

    bool OrderBook::add_order(const Order &order)
    {
        if (order.quantity == 0)
        {
            return false;
        }

        if (order.type == OrderType::Limit && order.price <= 0)
        {
            return false;
        }

        if (order_index_.count(order.id) != 0)
        {
            return false;
        }

        if (order.side == Side::Buy)
        {
            process_buy_order(order);
        }
        else
        {
            process_sell_order(order);
        }

        return true;
    }

    bool OrderBook::cancel_order(OrderId order_id)
    {
        const auto index_it = order_index_.find(order_id);
        if (index_it == order_index_.end())
        {
            return false;
        }

        const OrderLocation &location = index_it->second;

        if (location.side == Side::Buy)
        {
            auto book_it = bids_.find(location.price);
            if (book_it == bids_.end())
            {
                return false;
            }

            book_it->second.erase(location.iterator);

            if (book_it->second.empty())
            {
                bids_.erase(book_it);
            }
        }
        else
        {
            auto book_it = asks_.find(location.price);
            if (book_it == asks_.end())
            {
                return false;
            }

            book_it->second.erase(location.iterator);

            if (book_it->second.empty())
            {
                asks_.erase(book_it);
            }
        }

        order_index_.erase(index_it);
        return true;
    }

    void OrderBook::remove_bid_level_if_empty(Price price)
    {
        const auto it = bids_.find(price);
        if (it != bids_.end() && it->second.empty())
        {
            bids_.erase(it);
        }
    }

    void OrderBook::remove_ask_level_if_empty(Price price)
    {
        const auto it = asks_.find(price);
        if (it != asks_.end() && it->second.empty())
        {
            asks_.erase(it);
        }
    }

    void OrderBook::match_buy_order(Order &incoming)
    {
        while (incoming.quantity > 0 && !asks_.empty())
        {
            auto best_ask_it = asks_.begin();
            const Price best_ask_price = best_ask_it->first;

            if (incoming.type == OrderType::Limit && incoming.price < best_ask_price)
            {
                break;
            }

            auto &resting_orders = best_ask_it->second;

            while (incoming.quantity > 0 && !resting_orders.empty())
            {
                auto resting_it = resting_orders.begin();
                Order &resting = *resting_it;

                const Quantity executed_quantity =
                    std::min(incoming.quantity, resting.quantity);

                trades_.push_back(Trade{
                    incoming.id,
                    resting.id,
                    resting.price,
                    executed_quantity});

                incoming.quantity -= executed_quantity;
                resting.quantity -= executed_quantity;

                if (resting.quantity == 0)
                {
                    order_index_.erase(resting.id);
                    resting_orders.erase(resting_it);
                }
            }

            remove_ask_level_if_empty(best_ask_price);
        }
    }

    void OrderBook::match_sell_order(Order &incoming)
    {
        while (incoming.quantity > 0 && !bids_.empty())
        {
            auto best_bid_it = bids_.begin();
            const Price best_bid_price = best_bid_it->first;

            if (incoming.type == OrderType::Limit && incoming.price > best_bid_price)
            {
                break;
            }

            auto &resting_orders = best_bid_it->second;

            while (incoming.quantity > 0 && !resting_orders.empty())
            {
                auto resting_it = resting_orders.begin();
                Order &resting = *resting_it;

                const Quantity executed_quantity =
                    std::min(incoming.quantity, resting.quantity);

                trades_.push_back(Trade{
                    resting.id,
                    incoming.id,
                    resting.price,
                    executed_quantity});

                incoming.quantity -= executed_quantity;
                resting.quantity -= executed_quantity;

                if (resting.quantity == 0)
                {
                    order_index_.erase(resting.id);
                    resting_orders.erase(resting_it);
                }
            }

            remove_bid_level_if_empty(best_bid_price);
        }
    }

    void OrderBook::process_buy_order(Order order)
    {
        match_buy_order(order);

        if (order.quantity > 0 && order.type == OrderType::Limit)
        {
            rest_order(std::move(order));
        }
    }

    void OrderBook::process_sell_order(Order order)
    {
        match_sell_order(order);

        if (order.quantity > 0 && order.type == OrderType::Limit)
        {
            rest_order(std::move(order));
        }
    }

    std::vector<BookLevel> OrderBook::levels(Side side, std::size_t depth) const
    {
        std::vector<BookLevel> result;
        result.reserve(depth);

        if (depth == 0)
        {
            return result;
        }

        if (side == Side::Buy)
        {
            for (auto it = bids_.begin(); it != bids_.end() && result.size() < depth; ++it)
            {
                Quantity total = 0;
                for (const auto &order : it->second)
                {
                    total += order.quantity;
                }

                result.push_back(BookLevel{it->first, total});
            }
        }
        else
        {
            for (auto it = asks_.begin(); it != asks_.end() && result.size() < depth; ++it)
            {
                Quantity total = 0;
                for (const auto &order : it->second)
                {
                    total += order.quantity;
                }

                result.push_back(BookLevel{it->first, total});
            }
        }

        return result;
    }

    std::vector<RestingOrderView> OrderBook::resting_orders(Side side) const
    {
        std::vector<RestingOrderView> result;
        if (side == Side::Buy)
        {
            for (const auto &[price, orders] : bids_)
            {
                for (const auto &order : orders)
                {
                    result.push_back(RestingOrderView{
                        order.id,
                        price,
                        order.quantity});
                }
            }
        }
        else
        {
            for (const auto &[price, orders] : asks_)
            {
                for (const auto &order : orders)
                {
                    result.push_back(RestingOrderView{
                        order.id,
                        price,
                        order.quantity});
                }
            }
        }

        return result;
    }

} // namespace lob
