#include "matching/order_book.h"

#include <limits>

namespace matching {

namespace {

constexpr Price PERCENTAGE_SCALE = 10'000;

[[nodiscard]] Price percentage_offset(Price market_price, Price basis_points) noexcept
{
    const Price whole = (market_price / PERCENTAGE_SCALE) * basis_points;
    const Price remainder =
        ((market_price % PERCENTAGE_SCALE) * basis_points) / PERCENTAGE_SCALE;
    return whole + remainder;
}

} // namespace

OrderBook::OrderBook(memory::ObjectPool<LevelNode>& level_pool, const Symbol& symbol) noexcept
    : level_pool_(level_pool),
      symbol_(symbol)
{
}

LevelNode* OrderBook::create_level(LevelType type, Price price) noexcept
{
    assert(((type == LevelType::BID) || (type == LevelType::ASK)) &&
           "Price level must be a bid or an ask");

    Levels& levels = (type == LevelType::BID) ? bids_ : asks_;
    assert((find_level(levels, price) == nullptr) && "Duplicate price level detected");

    LevelNode* level = level_pool_.create(type, price);
    if (level == nullptr)
        return nullptr;

    const auto insertion = levels.insert(*level);
    assert(insertion.second && "Duplicate price level detected during insertion");
    if (!insertion.second)
    {
        level_pool_.release(*level);
        return nullptr;
    }

    assert((insertion.first == level) && "AVL tree returned the wrong inserted level");

    if (type == LevelType::BID)
    {
        if ((best_bid_ == nullptr) || (price > best_bid_->price))
            best_bid_ = level;
    }
    else if ((best_ask_ == nullptr) || (price < best_ask_->price))
        best_ask_ = level;

    return level;
}

void OrderBook::delete_level(LevelNode& level) noexcept
{
    assert(level.orders.empty() && "Cannot delete a price level containing orders");
    assert((level.order_count == 0) && "Cannot delete a price level with a non-zero count");
    assert((level.total_quantity == 0) &&
           "Cannot delete a price level with non-zero quantity");
    assert((level.displayed_quantity == 0) &&
           "Cannot delete a price level with displayed quantity");

    if (level.is_bid())
    {
        assert((find_level(bids_, level.price) == &level) &&
               "Bid level does not belong to this order book");

        LevelNode* replacement = best_bid_;
        if (best_bid_ == &level)
            replacement = bids_.previous(level);

        static_cast<void>(bids_.erase(level));
        best_bid_ = replacement;
    }
    else
    {
        assert(level.is_ask() && "Price level has an invalid side");
        assert((find_level(asks_, level.price) == &level) &&
               "Ask level does not belong to this order book");

        LevelNode* replacement = best_ask_;
        if (best_ask_ == &level)
            replacement = asks_.next(level);

        static_cast<void>(asks_.erase(level));
        best_ask_ = replacement;
    }

    level_pool_.release(level);
}

std::optional<LevelUpdate> OrderBook::add_order(OrderNode& order) noexcept
{
    Levels& levels = order.is_buy() ? bids_ : asks_;
    return add_order(order, find_level(levels, order.price));
}

std::optional<LevelUpdate> OrderBook::add_order(OrderNode& order,
                                                LevelNode* level) noexcept
{
    assert((order.symbol_id == symbol_.id) && "Order belongs to a different symbol");
    assert(order.is_limit() && "Only limit orders can rest in the regular order book");
    assert((order.is_gtc() || order.is_aon()) &&
           "Only persistent orders can rest in the regular order book");
    assert((order.is_buy() || order.is_sell()) && "Resting order has an invalid side");
    assert((order.price > 0) && "Resting order price must be positive");
    assert((order.quantity > 0) && "Resting order quantity must be positive");
    assert((order.level == nullptr) && "Order is already attached to a price level");
    assert((order.previous == nullptr) && "Order already has a previous FIFO link");
    assert((order.next == nullptr) && "Order already has a next FIFO link");

#ifndef NDEBUG
    Levels& levels = order.is_buy() ? bids_ : asks_;
    assert(((level == nullptr) || (find_level(levels, order.price) == level)) &&
           "Provided price level does not match the order");
#endif
    UpdateType update_type = UpdateType::UPDATE;

    if (level == nullptr)
    {
        const LevelType level_type = order.is_buy() ? LevelType::BID : LevelType::ASK;
        level = create_level(level_type, order.price);
        if (level == nullptr)
            return std::nullopt;
        update_type = UpdateType::ADD;
    }

    assert((level->type == (order.is_buy() ? LevelType::BID : LevelType::ASK)) &&
           "Order side and price-level side disagree");
    if (level->total_quantity >
        (std::numeric_limits<Quantity>::max() - order.quantity))
    {
        return std::nullopt;
    }
    const Quantity displayed = order.displayed_quantity();
    assert((level->displayed_quantity <=
            (std::numeric_limits<Quantity>::max() - displayed)) &&
           "Price-level displayed quantity overflow");

    assert((level->order_count < std::numeric_limits<std::size_t>::max()) &&
           "Price-level order count overflow");

    level->orders.push_back(order);
    order.level = level;
    level->total_quantity += order.quantity;
    level->displayed_quantity += displayed;
    ++level->order_count;

    const bool top = level == (order.is_buy() ? best_bid_ : best_ask_);
    return std::optional<LevelUpdate>(std::in_place, update_type, *level, top);
}

LevelUpdate OrderBook::reduce_order(OrderNode& order, Quantity reduction) noexcept
{
    assert((order.symbol_id == symbol_.id) && "Order belongs to a different symbol");
    assert(order.is_limit() && "Only limit orders can exist in the regular order book");
    assert((order.is_gtc() || order.is_aon()) &&
           "Only persistent orders can rest in the regular order book");
    assert((order.is_buy() || order.is_sell()) && "Resting order has an invalid side");
    assert((order.level != nullptr) && "Order is not attached to a price level");
    assert((reduction > 0) && "Order reduction must be positive");
    assert((reduction <= order.quantity) && "Order reduction exceeds open quantity");

    LevelNode* level = order.level;
    assert((level->price == order.price) && "Order and price-level prices disagree");
    assert((level->type == (order.is_buy() ? LevelType::BID : LevelType::ASK)) &&
           "Order side and price-level side disagree");
    assert((find_level(order.is_buy() ? bids_ : asks_, order.price) == level) &&
           "Order price level does not belong to this order book");
    assert((level->total_quantity >= reduction) &&
           "Order reduction exceeds price-level quantity");

    const bool top = level == (order.is_buy() ? best_bid_ : best_ask_);
    const Quantity old_displayed = order.displayed_quantity();
    order.quantity -= reduction;
    level->total_quantity -= reduction;
    const Quantity displayed_reduction = old_displayed - order.displayed_quantity();
    assert((level->displayed_quantity >= displayed_reduction) &&
           "Order reduction exceeds displayed level quantity");
    level->displayed_quantity -= displayed_reduction;

    if (order.quantity == 0)
    {
        assert((level->order_count > 0) && "Price level has no order to remove");
        level->orders.unlink(order);
        --level->order_count;
        order.level = nullptr;
    }

    const Level level_snapshot(*level);
    if (level->order_count == 0)
    {
        assert(level->orders.empty() && "Empty price level has linked orders");
        assert((level->total_quantity == 0) && "Empty price level has remaining quantity");
        assert((level->displayed_quantity == 0) &&
               "Empty price level has displayed quantity");
        delete_level(*level);
        return LevelUpdate(UpdateType::DELETE, level_snapshot, top);
    }

    assert(!level->orders.empty() && "Populated price level has no linked orders");
    assert((level->total_quantity > 0) && "Populated price level has zero quantity");
    assert((level->displayed_quantity <= level->total_quantity) &&
           "Displayed quantity exceeds total quantity");
    return LevelUpdate(UpdateType::UPDATE, level_snapshot, top);
}

LevelUpdate OrderBook::delete_order(OrderNode& order) noexcept
{
    assert((order.symbol_id == symbol_.id) && "Order belongs to a different symbol");
    assert(order.is_limit() && "Only limit orders can exist in the regular order book");
    assert((order.is_gtc() || order.is_aon()) &&
           "Only persistent orders can rest in the regular order book");
    assert((order.is_buy() || order.is_sell()) && "Resting order has an invalid side");
    assert((order.level != nullptr) && "Order is not attached to a price level");
    assert((order.quantity > 0) && "Resting order quantity must be positive");

    LevelNode* level = order.level;
    assert((level->price == order.price) && "Order and price-level prices disagree");
    assert((level->type == (order.is_buy() ? LevelType::BID : LevelType::ASK)) &&
           "Order side and price-level side disagree");
    assert((find_level(order.is_buy() ? bids_ : asks_, order.price) == level) &&
           "Order price level does not belong to this order book");
    assert((level->total_quantity >= order.quantity) &&
           "Order quantity exceeds price-level quantity");
    assert((level->order_count > 0) && "Price level has no order to delete");

    const bool top = level == (order.is_buy() ? best_bid_ : best_ask_);
    level->total_quantity -= order.quantity;
    const Quantity displayed = order.displayed_quantity();
    assert((level->displayed_quantity >= displayed) &&
           "Order displayed quantity exceeds price-level quantity");
    level->displayed_quantity -= displayed;
    level->orders.unlink(order);
    --level->order_count;
    order.level = nullptr;

    const Level level_snapshot(*level);
    if (level->order_count == 0)
    {
        assert(level->orders.empty() && "Empty price level has linked orders");
        assert((level->total_quantity == 0) && "Empty price level has remaining quantity");
        assert((level->displayed_quantity == 0) &&
               "Empty price level has displayed quantity");
        delete_level(*level);
        return LevelUpdate(UpdateType::DELETE, level_snapshot, top);
    }

    assert(!level->orders.empty() && "Populated price level has no linked orders");
    assert((level->total_quantity > 0) && "Populated price level has zero quantity");
    assert((level->displayed_quantity <= level->total_quantity) &&
           "Displayed quantity exceeds total quantity");
    return LevelUpdate(UpdateType::UPDATE, level_snapshot, top);
}

LevelNode* OrderBook::create_stop_level(OrderNode& order) noexcept
{
    assert((order.is_stop() || order.is_stop_limit()) &&
           "Only stop orders can create a stop level");
    assert((order.is_buy() || order.is_sell()) && "Stop order has an invalid side");
    assert((order.stop_price > 0) && "Stop trigger price must be positive");

    Levels& levels = order.is_buy() ? buy_stops_ : sell_stops_;
    assert((find_level(levels, order.stop_price) == nullptr) &&
           "Duplicate stop level detected");

    // Buy stops use ASK ordering and sell stops use BID ordering, so LevelType
    // selects the ascending or descending trigger traversal direction.
    const LevelType level_type = order.is_buy() ? LevelType::ASK : LevelType::BID;
    LevelNode* level = level_pool_.create(level_type, order.stop_price);
    if (level == nullptr)
        return nullptr;

    const auto insertion = levels.insert(*level);
    assert(insertion.second && "Duplicate stop level detected during insertion");
    if (!insertion.second)
    {
        level_pool_.release(*level);
        return nullptr;
    }

    assert((insertion.first == level) && "AVL tree returned the wrong stop level");

    if (order.is_buy())
    {
        if ((best_buy_stop_ == nullptr) || (level->price < best_buy_stop_->price))
            best_buy_stop_ = level;
    }
    else if ((best_sell_stop_ == nullptr) || (level->price > best_sell_stop_->price))
        best_sell_stop_ = level;

    return level;
}

void OrderBook::delete_stop_level(OrderNode& order) noexcept
{
    assert((order.is_stop() || order.is_stop_limit()) &&
           "Only stop orders can delete a stop level");
    assert((order.level != nullptr) && "Stop order is not attached to a level");

    LevelNode* level = order.level;
    assert(level->orders.empty() && "Cannot delete a stop level containing orders");
    assert((level->order_count == 0) && "Cannot delete a stop level with a non-zero count");
    assert((level->total_quantity == 0) &&
           "Cannot delete a stop level with non-zero quantity");
    assert((level->displayed_quantity == 0) &&
           "Cannot delete a stop level with displayed quantity");

    if (order.is_buy())
    {
        assert(level->is_ask() && "Buy-stop level must use the ASK traversal type");
        assert((find_level(buy_stops_, level->price) == level) &&
               "Buy-stop level does not belong to this order book");

        if (level == best_buy_stop_)
            best_buy_stop_ = (level->right != nullptr) ? level->right : level->parent;

        static_cast<void>(buy_stops_.erase(*level));
    }
    else
    {
        assert(level->is_bid() && "Sell-stop level must use the BID traversal type");
        assert((find_level(sell_stops_, level->price) == level) &&
               "Sell-stop level does not belong to this order book");

        if (level == best_sell_stop_)
            best_sell_stop_ = (level->left != nullptr) ? level->left : level->parent;

        static_cast<void>(sell_stops_.erase(*level));
    }

    order.level = nullptr;
    level_pool_.release(*level);
}

bool OrderBook::add_stop_order(OrderNode& order) noexcept
{
    Levels& levels = order.is_buy() ? buy_stops_ : sell_stops_;
    return add_stop_order(order, find_level(levels, order.stop_price));
}

bool OrderBook::add_stop_order(OrderNode& order, LevelNode* level) noexcept
{
    assert((order.symbol_id == symbol_.id) && "Stop order belongs to a different symbol");
    assert((order.is_stop() || order.is_stop_limit()) &&
           "Only stop orders can rest in a stop index");
    assert((order.is_buy() || order.is_sell()) && "Stop order has an invalid side");
    assert((order.stop_price > 0) && "Stop trigger price must be positive");
    assert((order.quantity > 0) && "Stop order quantity must be positive");
    assert((order.level == nullptr) && "Stop order is already attached to a level");
    assert((order.previous == nullptr) && "Stop order already has a previous FIFO link");
    assert((order.next == nullptr) && "Stop order already has a next FIFO link");

#ifndef NDEBUG
    Levels& levels = order.is_buy() ? buy_stops_ : sell_stops_;
    assert(((level == nullptr) || (find_level(levels, order.stop_price) == level)) &&
           "Provided stop level does not match the order");
#endif
    if (level == nullptr)
    {
        level = create_stop_level(order);
        if (level == nullptr)
            return false;
    }

    assert((level->type == (order.is_buy() ? LevelType::ASK : LevelType::BID)) &&
           "Stop order side and stop-level traversal type disagree");
    if (level->total_quantity >
        (std::numeric_limits<Quantity>::max() - order.quantity))
    {
        return false;
    }
    const Quantity displayed = order.displayed_quantity();
    assert((level->displayed_quantity <=
            (std::numeric_limits<Quantity>::max() - displayed)) &&
           "Stop-level displayed quantity overflow");

    assert((level->order_count < std::numeric_limits<std::size_t>::max()) &&
           "Stop-level order count overflow");

    level->orders.push_back(order);
    order.level = level;
    level->total_quantity += order.quantity;
    level->displayed_quantity += displayed;
    ++level->order_count;
    return true;
}

void OrderBook::reduce_stop_order(OrderNode& order, Quantity reduction) noexcept
{
    assert((order.symbol_id == symbol_.id) && "Stop order belongs to a different symbol");
    assert((order.is_stop() || order.is_stop_limit()) &&
           "Only stop orders can exist in a stop index");
    assert((order.is_buy() || order.is_sell()) && "Stop order has an invalid side");
    assert((order.level != nullptr) && "Stop order is not attached to a level");
    assert((reduction > 0) && "Stop-order reduction must be positive");
    assert((reduction <= order.quantity) && "Reduction exceeds open stop quantity");

    LevelNode* level = order.level;
    assert((level->price == order.stop_price) && "Order and stop-level prices disagree");
    assert((level->type == (order.is_buy() ? LevelType::ASK : LevelType::BID)) &&
           "Stop order side and stop-level traversal type disagree");
    assert((find_level(order.is_buy() ? buy_stops_ : sell_stops_, order.stop_price) ==
            level) &&
           "Stop level does not belong to this order book");
    assert((level->total_quantity >= reduction) &&
           "Reduction exceeds stop-level quantity");

    const Quantity old_displayed = order.displayed_quantity();
    order.quantity -= reduction;
    level->total_quantity -= reduction;
    const Quantity displayed_reduction = old_displayed - order.displayed_quantity();
    assert((level->displayed_quantity >= displayed_reduction) &&
           "Stop-order reduction exceeds displayed level quantity");
    level->displayed_quantity -= displayed_reduction;

    if (order.quantity == 0)
    {
        assert((level->order_count > 0) && "Stop level has no order to remove");
        level->orders.unlink(order);
        --level->order_count;

        if (level->order_count == 0)
        {
            assert(level->orders.empty() && "Empty stop level has linked orders");
            assert((level->total_quantity == 0) &&
                   "Empty stop level has remaining quantity");
            assert((level->displayed_quantity == 0) &&
                   "Empty stop level has displayed quantity");
            delete_stop_level(order);
            return;
        }

        order.level = nullptr;
    }

    assert(!level->orders.empty() && "Populated stop level has no linked orders");
    assert((level->total_quantity > 0) && "Populated stop level has zero quantity");
    assert((level->displayed_quantity <= level->total_quantity) &&
           "Displayed stop quantity exceeds total quantity");
}

void OrderBook::delete_stop_order(OrderNode& order) noexcept
{
    assert((order.symbol_id == symbol_.id) && "Stop order belongs to a different symbol");
    assert((order.is_stop() || order.is_stop_limit()) &&
           "Only stop orders can exist in a stop index");
    assert((order.is_buy() || order.is_sell()) && "Stop order has an invalid side");
    assert((order.level != nullptr) && "Stop order is not attached to a level");
    assert((order.quantity > 0) && "Stop order quantity must be positive");

    LevelNode* level = order.level;
    assert((level->price == order.stop_price) && "Order and stop-level prices disagree");
    assert((level->type == (order.is_buy() ? LevelType::ASK : LevelType::BID)) &&
           "Stop order side and stop-level traversal type disagree");
    assert((find_level(order.is_buy() ? buy_stops_ : sell_stops_, order.stop_price) ==
            level) &&
           "Stop level does not belong to this order book");
    assert((level->total_quantity >= order.quantity) &&
           "Order quantity exceeds stop-level quantity");
    assert((level->order_count > 0) && "Stop level has no order to delete");

    level->total_quantity -= order.quantity;
    const Quantity displayed = order.displayed_quantity();
    assert((level->displayed_quantity >= displayed) &&
           "Order displayed quantity exceeds stop-level quantity");
    level->displayed_quantity -= displayed;
    level->orders.unlink(order);
    --level->order_count;

    if (level->order_count == 0)
    {
        assert(level->orders.empty() && "Empty stop level has linked orders");
        assert((level->total_quantity == 0) &&
               "Empty stop level has remaining quantity");
        assert((level->displayed_quantity == 0) &&
               "Empty stop level has displayed quantity");
        delete_stop_level(order);
        return;
    }

    order.level = nullptr;
    assert(!level->orders.empty() && "Populated stop level has no linked orders");
    assert((level->total_quantity > 0) && "Populated stop level has zero quantity");
    assert((level->displayed_quantity <= level->total_quantity) &&
           "Displayed stop quantity exceeds total quantity");
}

LevelNode* OrderBook::create_trailing_stop_level(OrderNode& order) noexcept
{
    assert((order.is_trailing_stop() || order.is_trailing_stop_limit()) &&
           "Only trailing orders can create a trailing-stop level");
    assert((order.is_buy() || order.is_sell()) && "Trailing order has an invalid side");
    assert((order.stop_price > 0) && "Trailing trigger price must be positive");

    Levels& levels = order.is_buy() ? trailing_buy_stops_ : trailing_sell_stops_;
    assert((find_level(levels, order.stop_price) == nullptr) &&
           "Duplicate trailing-stop level detected");

    const LevelType level_type = order.is_buy() ? LevelType::ASK : LevelType::BID;
    LevelNode* level = level_pool_.create(level_type, order.stop_price);
    if (level == nullptr)
        return nullptr;

    const auto insertion = levels.insert(*level);
    assert(insertion.second &&
           "Duplicate trailing-stop level detected during insertion");
    if (!insertion.second)
    {
        level_pool_.release(*level);
        return nullptr;
    }

    assert((insertion.first == level) &&
           "AVL tree returned the wrong trailing-stop level");

    if (order.is_buy())
    {
        if ((best_trailing_buy_stop_ == nullptr) ||
            (level->price < best_trailing_buy_stop_->price))
        {
            best_trailing_buy_stop_ = level;
        }
    }
    else if ((best_trailing_sell_stop_ == nullptr) ||
             (level->price > best_trailing_sell_stop_->price))
    {
        best_trailing_sell_stop_ = level;
    }

    return level;
}

void OrderBook::delete_trailing_stop_level(OrderNode& order) noexcept
{
    assert((order.is_trailing_stop() || order.is_trailing_stop_limit()) &&
           "Only trailing orders can delete a trailing-stop level");
    assert((order.level != nullptr) && "Trailing order is not attached to a level");

    LevelNode* level = order.level;
    assert(level->orders.empty() &&
           "Cannot delete a trailing-stop level containing orders");
    assert((level->order_count == 0) &&
           "Cannot delete a trailing-stop level with a non-zero count");
    assert((level->total_quantity == 0) &&
           "Cannot delete a trailing-stop level with non-zero quantity");
    assert((level->displayed_quantity == 0) &&
           "Cannot delete a trailing-stop level with displayed quantity");

    if (order.is_buy())
    {
        assert(level->is_ask() &&
               "Trailing-buy level must use the ASK traversal type");
        assert((find_level(trailing_buy_stops_, level->price) == level) &&
               "Trailing-buy level does not belong to this order book");

        if (level == best_trailing_buy_stop_)
        {
            best_trailing_buy_stop_ =
                (level->right != nullptr) ? level->right : level->parent;
        }

        static_cast<void>(trailing_buy_stops_.erase(*level));
    }
    else
    {
        assert(level->is_bid() &&
               "Trailing-sell level must use the BID traversal type");
        assert((find_level(trailing_sell_stops_, level->price) == level) &&
               "Trailing-sell level does not belong to this order book");

        if (level == best_trailing_sell_stop_)
        {
            best_trailing_sell_stop_ =
                (level->left != nullptr) ? level->left : level->parent;
        }

        static_cast<void>(trailing_sell_stops_.erase(*level));
    }

    order.level = nullptr;
    level_pool_.release(*level);
}

bool OrderBook::add_trailing_stop_order(OrderNode& order) noexcept
{
    Levels& levels = order.is_buy() ? trailing_buy_stops_ : trailing_sell_stops_;
    return add_trailing_stop_order(order, find_level(levels, order.stop_price));
}

bool OrderBook::add_trailing_stop_order(OrderNode& order, LevelNode* level) noexcept
{
    assert((order.symbol_id == symbol_.id) &&
           "Trailing order belongs to a different symbol");
    assert((order.is_trailing_stop() || order.is_trailing_stop_limit()) &&
           "Only trailing orders can rest in a trailing-stop index");
    assert((order.is_buy() || order.is_sell()) && "Trailing order has an invalid side");
    assert((order.stop_price > 0) && "Trailing trigger price must be positive");
    assert((order.quantity > 0) && "Trailing order quantity must be positive");
    assert((order.level == nullptr) && "Trailing order is already attached to a level");
    assert((order.previous == nullptr) &&
           "Trailing order already has a previous FIFO link");
    assert((order.next == nullptr) && "Trailing order already has a next FIFO link");

#ifndef NDEBUG
    Levels& levels = order.is_buy() ? trailing_buy_stops_ : trailing_sell_stops_;
    assert(((level == nullptr) || (find_level(levels, order.stop_price) == level)) &&
           "Provided trailing-stop level does not match the order");
#endif
    if (level == nullptr)
    {
        level = create_trailing_stop_level(order);
        if (level == nullptr)
            return false;
    }

    assert((level->type == (order.is_buy() ? LevelType::ASK : LevelType::BID)) &&
           "Trailing-order side and level traversal type disagree");
    if (level->total_quantity >
        (std::numeric_limits<Quantity>::max() - order.quantity))
    {
        return false;
    }
    const Quantity displayed = order.displayed_quantity();
    assert((level->displayed_quantity <=
            (std::numeric_limits<Quantity>::max() - displayed)) &&
           "Trailing-stop displayed quantity overflow");

    assert((level->order_count < std::numeric_limits<std::size_t>::max()) &&
           "Trailing-stop level order count overflow");

    level->orders.push_back(order);
    order.level = level;
    level->total_quantity += order.quantity;
    level->displayed_quantity += displayed;
    ++level->order_count;
    return true;
}

void OrderBook::reduce_trailing_stop_order(OrderNode& order, Quantity reduction) noexcept
{
    assert((order.symbol_id == symbol_.id) &&
           "Trailing order belongs to a different symbol");
    assert((order.is_trailing_stop() || order.is_trailing_stop_limit()) &&
           "Only trailing orders can exist in a trailing-stop index");
    assert((order.is_buy() || order.is_sell()) && "Trailing order has an invalid side");
    assert((order.level != nullptr) && "Trailing order is not attached to a level");
    assert((reduction > 0) && "Trailing-order reduction must be positive");
    assert((reduction <= order.quantity) &&
           "Reduction exceeds open trailing quantity");

    LevelNode* level = order.level;
    assert((level->price == order.stop_price) &&
           "Order and trailing-stop level prices disagree");
    assert((level->type == (order.is_buy() ? LevelType::ASK : LevelType::BID)) &&
           "Trailing-order side and level traversal type disagree");
    assert((find_level(order.is_buy() ? trailing_buy_stops_ : trailing_sell_stops_,
                       order.stop_price) == level) &&
           "Trailing-stop level does not belong to this order book");
    assert((level->total_quantity >= reduction) &&
           "Reduction exceeds trailing-stop level quantity");

    const Quantity old_displayed = order.displayed_quantity();
    order.quantity -= reduction;
    level->total_quantity -= reduction;
    const Quantity displayed_reduction = old_displayed - order.displayed_quantity();
    assert((level->displayed_quantity >= displayed_reduction) &&
           "Trailing reduction exceeds displayed level quantity");
    level->displayed_quantity -= displayed_reduction;

    if (order.quantity == 0)
    {
        assert((level->order_count > 0) &&
               "Trailing-stop level has no order to remove");
        level->orders.unlink(order);
        --level->order_count;

        if (level->order_count == 0)
        {
            assert(level->orders.empty() &&
                   "Empty trailing-stop level has linked orders");
            assert((level->total_quantity == 0) &&
                   "Empty trailing-stop level has remaining quantity");
            assert((level->displayed_quantity == 0) &&
                   "Empty trailing-stop level has displayed quantity");
            delete_trailing_stop_level(order);
            return;
        }

        order.level = nullptr;
    }

    assert(!level->orders.empty() &&
           "Populated trailing-stop level has no linked orders");
    assert((level->total_quantity > 0) &&
           "Populated trailing-stop level has zero quantity");
    assert((level->displayed_quantity <= level->total_quantity) &&
           "Displayed trailing quantity exceeds total quantity");
}

void OrderBook::delete_trailing_stop_order(OrderNode& order) noexcept
{
    assert((order.symbol_id == symbol_.id) &&
           "Trailing order belongs to a different symbol");
    assert((order.is_trailing_stop() || order.is_trailing_stop_limit()) &&
           "Only trailing orders can exist in a trailing-stop index");
    assert((order.is_buy() || order.is_sell()) && "Trailing order has an invalid side");
    assert((order.level != nullptr) && "Trailing order is not attached to a level");
    assert((order.quantity > 0) && "Trailing order quantity must be positive");

    LevelNode* level = order.level;
    assert((level->price == order.stop_price) &&
           "Order and trailing-stop level prices disagree");
    assert((level->type == (order.is_buy() ? LevelType::ASK : LevelType::BID)) &&
           "Trailing-order side and level traversal type disagree");
    assert((find_level(order.is_buy() ? trailing_buy_stops_ : trailing_sell_stops_,
                       order.stop_price) == level) &&
           "Trailing-stop level does not belong to this order book");
    assert((level->total_quantity >= order.quantity) &&
           "Order quantity exceeds trailing-stop level quantity");
    assert((level->order_count > 0) &&
           "Trailing-stop level has no order to delete");

    level->total_quantity -= order.quantity;
    const Quantity displayed = order.displayed_quantity();
    assert((level->displayed_quantity >= displayed) &&
           "Order displayed quantity exceeds trailing-stop level quantity");
    level->displayed_quantity -= displayed;
    level->orders.unlink(order);
    --level->order_count;

    if (level->order_count == 0)
    {
        assert(level->orders.empty() &&
               "Empty trailing-stop level has linked orders");
        assert((level->total_quantity == 0) &&
               "Empty trailing-stop level has remaining quantity");
        assert((level->displayed_quantity == 0) &&
               "Empty trailing-stop level has displayed quantity");
        delete_trailing_stop_level(order);
        return;
    }

    order.level = nullptr;
    assert(!level->orders.empty() &&
           "Populated trailing-stop level has no linked orders");
    assert((level->total_quantity > 0) &&
           "Populated trailing-stop level has zero quantity");
    assert((level->displayed_quantity <= level->total_quantity) &&
           "Displayed trailing quantity exceeds total quantity");
}

Price OrderBook::calculate_trailing_stop_price(const Order& order) const noexcept
{
    return calculate_trailing_stop_price(order, nullptr);
}

Price OrderBook::calculate_trailing_stop_price(
    const Order& order,
    const OrderNode* excluded_order) const noexcept
{
    assert((order.is_trailing_stop() || order.is_trailing_stop_limit()) &&
           "Trailing-price calculation requires a trailing order");

    const Price old_price = order.stop_price;
    Price market_price = 0;

    if (order.is_buy())
    {
        const LevelNode* best_ask = best_ask_;
        if ((excluded_order != nullptr) && excluded_order->is_limit() &&
            excluded_order->is_sell() && (excluded_order->level != nullptr) &&
            (excluded_order->level == best_ask_) &&
            (excluded_order->level->order_count == 1))
        {
            best_ask = asks_.next(*excluded_order->level);
        }

        const Price best_price = (best_ask != nullptr)
                                     ? best_ask->price
                                     : std::numeric_limits<Price>::max();
        market_price = std::max(last_ask_price_, best_price);
    }
    else
    {
        const LevelNode* best_bid = best_bid_;
        if ((excluded_order != nullptr) && excluded_order->is_limit() &&
            excluded_order->is_buy() && (excluded_order->level != nullptr) &&
            (excluded_order->level == best_bid_) &&
            (excluded_order->level->order_count == 1))
        {
            best_bid = bids_.previous(*excluded_order->level);
        }

        const Price best_price = (best_bid != nullptr) ? best_bid->price : 0;
        market_price = std::min(last_bid_price_, best_price);
    }

    if ((order.is_buy() && (market_price == std::numeric_limits<Price>::max())) ||
        (order.is_sell() && (market_price == 0)))
    {
        return old_price;
    }

    Price distance = 0;
    Price step = 0;

    if (order.trailing_distance > 0)
    {
        if ((order.trailing_step < 0) ||
            (order.trailing_step >= order.trailing_distance))
        {
            return old_price;
        }

        distance = static_cast<Price>(order.trailing_distance);
        step = static_cast<Price>(order.trailing_step);
    }
    else
    {
        if ((order.trailing_distance < -10'000) || (order.trailing_distance == 0) ||
            (order.trailing_step > 0) ||
            (order.trailing_step <= order.trailing_distance))
        {
            return old_price;
        }

        const Price distance_basis_points =
            static_cast<Price>(-order.trailing_distance);
        const Price step_basis_points = static_cast<Price>(-order.trailing_step);
        distance = percentage_offset(market_price, distance_basis_points);
        step = percentage_offset(market_price, step_basis_points);
    }

    if (order.is_buy())
    {
        const Price new_price =
            (market_price < (std::numeric_limits<Price>::max() - distance))
                ? market_price + distance
                : std::numeric_limits<Price>::max();

        if ((new_price < old_price) && ((old_price - new_price) >= step))
            return new_price;
    }
    else
    {
        const Price new_price = (market_price > distance) ? market_price - distance : 0;

        if ((new_price > old_price) && ((new_price - old_price) >= step))
            return new_price;
    }

    return old_price;
}

} // namespace matching
