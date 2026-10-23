#include "matching/manager/market_manager.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <limits>
#include <new>
#include <optional>

namespace matching {

MarketHandler MarketManager::default_handler_;

MarketManager::~MarketManager() noexcept
{
    active_orders_.for_each(
        [this](const OrderId&, OrderNode* const& order) noexcept
        {
            assert((order != nullptr) && "Active-order registry contains a null order");
            if (order == nullptr)
                return;

            OrderBook* order_book = find_order_book_mutable(order->symbol_id);
            assert((order_book != nullptr) && "Active order has no owning order book");
            if (order_book == nullptr)
                return;

            if (order->is_limit() && (order->is_gtc() || order->is_aon()))
                static_cast<void>(order_book->delete_order(*order));
            else if (order->is_stop() || order->is_stop_limit())
                order_book->delete_stop_order(*order);
            else if (order->is_trailing_stop() || order->is_trailing_stop_limit())
                order_book->delete_trailing_stop_order(*order);
            else
            {
                assert(false && "Passive cleanup found a non-resting order");
                return;
            }

            order_pool_.release(*order);
        });
    active_orders_.clear();

    assert((order_pool_.live_count() == 0) && "Order pool contains unregistered orders");

    for (OrderBook* order_book : order_books_)
    {
        if (order_book == nullptr)
            continue;

        assert(order_book->empty() && "MarketManager destroyed with a non-empty order book");
        order_book_pool_.release(*order_book);
    }
    order_books_.clear();
    order_book_count_ = 0;

    for (Symbol* symbol : symbols_)
    {
        if (symbol != nullptr)
            symbol_pool_.release(*symbol);
    }
    symbols_.clear();
    symbol_count_ = 0;

    assert((order_book_pool_.live_count() == 0) &&
           "Order-book pool contains live objects after teardown");
    assert((symbol_pool_.live_count() == 0) &&
           "Symbol pool contains live objects after teardown");
    assert((level_pool_.live_count() == 0) &&
           "Level pool contains live objects after teardown");
}

ErrorCode MarketManager::add_symbol(const Symbol& symbol)
{
    const std::size_t index = static_cast<std::size_t>(symbol.id);
    if ((index < symbols_.size()) && (symbols_[index] != nullptr))
        return ErrorCode::SYMBOL_DUPLICATE;

    Symbol* stored_symbol = symbol_pool_.create(symbol);
    if (stored_symbol == nullptr)
        throw std::bad_alloc();

    try
    {
        if (index >= symbols_.size())
            symbols_.resize(index + 1, nullptr);
    }
    catch (...)
    {
        symbol_pool_.release(*stored_symbol);
        throw;
    }

    symbols_[index] = stored_symbol;
    ++symbol_count_;
    market_handler_.on_add_symbol(*stored_symbol);
    return ErrorCode::OK;
}

ErrorCode MarketManager::delete_symbol(SymbolId symbol_id) noexcept
{
    const std::size_t index = static_cast<std::size_t>(symbol_id);
    if ((index >= symbols_.size()) || (symbols_[index] == nullptr))
        return ErrorCode::SYMBOL_NOT_FOUND;

    if ((index < order_books_.size()) && (order_books_[index] != nullptr))
        return ErrorCode::SYMBOL_IN_USE;

    Symbol* symbol = symbols_[index];
    symbols_[index] = nullptr;
    --symbol_count_;

    market_handler_.on_delete_symbol(*symbol);
    symbol_pool_.release(*symbol);
    return ErrorCode::OK;
}

ErrorCode MarketManager::add_order_book(SymbolId symbol_id)
{
    // Check whether symbol exist or not
    const Symbol* symbol = find_symbol(symbol_id);
    if (symbol == nullptr)
        return ErrorCode::SYMBOL_NOT_FOUND;

    const std::size_t index = static_cast<std::size_t>(symbol_id);
    if ((index < order_books_.size()) && (order_books_[index] != nullptr))
        return ErrorCode::ORDER_BOOK_DUPLICATE;

    OrderBook* order_book = order_book_pool_.create(level_pool_, *symbol);
    if (order_book == nullptr)
        throw std::bad_alloc();

    try
    {
        if (index >= order_books_.size())
            order_books_.resize(index + 1, nullptr);
    }
    catch (...)
    {
        order_book_pool_.release(*order_book);
        throw;
    }

    order_books_[index] = order_book;
    ++order_book_count_;
    market_handler_.on_add_order_book(*order_book);
    return ErrorCode::OK;
}

ErrorCode MarketManager::delete_order_book(SymbolId symbol_id) noexcept
{
    OrderBook* order_book = find_order_book_mutable(symbol_id);
    if (order_book == nullptr)
        return ErrorCode::ORDER_BOOK_NOT_FOUND;

    if (!order_book->empty())
        return ErrorCode::ORDER_BOOK_NOT_EMPTY;

    const std::size_t index = static_cast<std::size_t>(symbol_id);
    order_books_[index] = nullptr;
    --order_book_count_;

    market_handler_.on_delete_order_book(*order_book);
    order_book_pool_.release(*order_book);
    return ErrorCode::OK;
}

ErrorCode MarketManager::resolve_active_order(SymbolId symbol_id,
                                              OrderId order_id,
                                              OrderBook*& order_book,
                                              OrderNode*& order) noexcept
{
    order_book = find_order_book_mutable(symbol_id);
    order = nullptr;

    if (order_book == nullptr)
        return ErrorCode::ORDER_BOOK_NOT_FOUND;

    OrderNode* const* stored_order = active_orders_.find(order_id);
    if ((stored_order == nullptr) || (*stored_order == nullptr) ||
        ((*stored_order)->symbol_id != symbol_id))
    {
        return ErrorCode::ORDER_NOT_FOUND;
    }

    order = *stored_order;
    return ErrorCode::OK;
}

bool MarketManager::is_supported_passive_order(const Order& order) noexcept
{
    return (order.is_limit() && (order.is_gtc() || order.is_aon())) ||
           order.is_stop() || order.is_stop_limit() ||
           order.is_trailing_stop() || order.is_trailing_stop_limit();
}

LevelNode* MarketManager::find_order_level(OrderBook& order_book,
                                           const Order& order) noexcept
{
    if (order.is_limit())
    {
        OrderBook::Levels& levels = order.is_buy() ? order_book.bids_ : order_book.asks_;
        return OrderBook::find_level(levels, order.price);
    }

    if (order.is_stop() || order.is_stop_limit())
    {
        OrderBook::Levels& levels =
            order.is_buy() ? order_book.buy_stops_ : order_book.sell_stops_;
        return OrderBook::find_level(levels, order.stop_price);
    }

    if (order.is_trailing_stop() || order.is_trailing_stop_limit())
    {
        OrderBook::Levels& levels = order.is_buy()
                                      ? order_book.trailing_buy_stops_
                                      : order_book.trailing_sell_stops_;
        return OrderBook::find_level(levels, order.stop_price);
    }

    return nullptr;
}

const Level* MarketManager::find_order_level(const OrderBook& order_book,
                                             const Order& order) noexcept
{
    if (order.is_limit())
        return order.is_buy() ? order_book.find_bid(order.price)
                              : order_book.find_ask(order.price);

    if (order.is_stop() || order.is_stop_limit())
    {
        return order.is_buy() ? order_book.find_buy_stop(order.stop_price)
                              : order_book.find_sell_stop(order.stop_price);
    }

    if (order.is_trailing_stop() || order.is_trailing_stop_limit())
    {
        return order.is_buy()
                   ? order_book.find_trailing_buy_stop(order.stop_price)
                   : order_book.find_trailing_sell_stop(order.stop_price);
    }

    return nullptr;
}

bool MarketManager::can_store_quantity(const OrderBook& order_book,
                                       const Order& order) noexcept
{
    const Level* target_level = find_order_level(order_book, order);
    if (target_level == nullptr)
        return true;

    return target_level->total_quantity <=
           (std::numeric_limits<Quantity>::max() - order.quantity);
}

MarketManager::RelocationStatus MarketManager::prepare_new_order_level(
    OrderBook& order_book,
    const Order& order,
    LevelNode*& destination_level) noexcept
{
    assert(order.is_limit() && (order.is_gtc() || order.is_aon()) &&
           "Only a persistent limit order can require a resting level");

    destination_level = find_order_level(order_book, order);
    if (destination_level != nullptr)
    {
        Quantity resting_quantity = order.quantity;
        if (destination_level->total_quantity >
            (std::numeric_limits<Quantity>::max() - resting_quantity))
        {
            if (matching_enabled_)
            {
                const Quantity executable =
                    calculate_executable_quantity(order_book, order, order.price);
                resting_quantity -= executable;
            }

            if (destination_level->total_quantity >
                (std::numeric_limits<Quantity>::max() - resting_quantity))
            {
                return RelocationStatus::QUANTITY_OVERFLOW;
            }
        }

        return RelocationStatus::READY;
    }

    if (level_pool_.free_count() > 0)
        return RelocationStatus::READY;

    const std::size_t capacity = level_pool_.capacity();
    if (capacity == std::numeric_limits<std::size_t>::max())
        return RelocationStatus::ALLOCATION_FAILURE;

    return level_pool_.reserve(capacity + 1) ? RelocationStatus::READY
                                             : RelocationStatus::ALLOCATION_FAILURE;
}

MarketManager::RelocationStatus MarketManager::prepare_relocation(
    OrderBook& order_book,
    const OrderNode& current_order,
    const Order& replacement,
    LevelNode*& destination_level) noexcept
{
    assert((current_order.level != nullptr) &&
           "Relocated order must belong to a level");
    destination_level = nullptr;

    const bool same_regular_index = current_order.is_limit() && replacement.is_limit();
    const bool same_stop_index =
        (current_order.is_stop() || current_order.is_stop_limit()) &&
        (replacement.is_stop() || replacement.is_stop_limit());
    const bool same_trailing_index =
        (current_order.is_trailing_stop() || current_order.is_trailing_stop_limit()) &&
        (replacement.is_trailing_stop() || replacement.is_trailing_stop_limit());

    const bool same_index = (current_order.side == replacement.side) &&
                            (same_regular_index || same_stop_index ||
                             same_trailing_index);
    const bool same_key =
        same_regular_index ? (current_order.price == replacement.price)
                           : (current_order.stop_price == replacement.stop_price);

    LevelNode* target_level =
        (same_index && same_key) ? current_order.level
                                 : find_order_level(order_book, replacement);

    if (target_level != nullptr)
    {
        Quantity quantity_before_add = target_level->total_quantity;
        if (target_level == current_order.level)
        {
            assert((quantity_before_add >= current_order.quantity) &&
                   "Current order exceeds its level aggregate");
            quantity_before_add -= current_order.quantity;
        }

        if (quantity_before_add >
            (std::numeric_limits<Quantity>::max() - replacement.quantity))
        {
            return RelocationStatus::QUANTITY_OVERFLOW;
        }

        if ((target_level != current_order.level) ||
            (current_order.level->order_count > 1))
        {
            destination_level = target_level;
        }

        return RelocationStatus::READY;
    }

    if (current_order.level->order_count == 1)
        return RelocationStatus::READY;

    if (level_pool_.free_count() > 0)
        return RelocationStatus::READY;

    const std::size_t capacity = level_pool_.capacity();
    if (capacity == std::numeric_limits<std::size_t>::max())
        return RelocationStatus::ALLOCATION_FAILURE;

    return level_pool_.reserve(capacity + 1) ? RelocationStatus::READY
                                             : RelocationStatus::ALLOCATION_FAILURE;
}

bool MarketManager::add_to_order_book(OrderBook& order_book,
                                      OrderNode& order,
                                      std::optional<LevelUpdate>& level_update) noexcept
{
    level_update.reset();

    if (order.is_limit() && (order.is_gtc() || order.is_aon()))
    {
        level_update = order_book.add_order(order);
        return level_update.has_value();
    }

    if (order.is_stop() || order.is_stop_limit())
        return order_book.add_stop_order(order);

    if (order.is_trailing_stop() || order.is_trailing_stop_limit())
        return order_book.add_trailing_stop_order(order);

    return false;
}

bool MarketManager::add_to_order_book_at_level(
    OrderBook& order_book,
    OrderNode& order,
    LevelNode* level,
    std::optional<LevelUpdate>& level_update) noexcept
{
    level_update.reset();

    if (order.is_limit() && (order.is_gtc() || order.is_aon()))
    {
        level_update = order_book.add_order(order, level);
        return level_update.has_value();
    }

    if (order.is_stop() || order.is_stop_limit())
        return order_book.add_stop_order(order, level);

    if (order.is_trailing_stop() || order.is_trailing_stop_limit())
        return order_book.add_trailing_stop_order(order, level);

    return false;
}

void MarketManager::reduce_in_order_book(
    OrderBook& order_book,
    OrderNode& order,
    Quantity reduction,
    std::optional<LevelUpdate>& level_update) noexcept
{
    level_update.reset();

    if (order.is_limit() && (order.is_gtc() || order.is_aon()))
        level_update = order_book.reduce_order(order, reduction);
    else if (order.is_stop() || order.is_stop_limit())
        order_book.reduce_stop_order(order, reduction);
    else
    {
        assert((order.is_trailing_stop() || order.is_trailing_stop_limit()) &&
               "Unsupported passive order reduction");
        order_book.reduce_trailing_stop_order(order, reduction);
    }
}

void MarketManager::delete_from_order_book(
    OrderBook& order_book,
    OrderNode& order,
    std::optional<LevelUpdate>& level_update) noexcept
{
    level_update.reset();

    if (order.is_limit() && (order.is_gtc() || order.is_aon()))
        level_update = order_book.delete_order(order);
    else if (order.is_stop() || order.is_stop_limit())
        order_book.delete_stop_order(order);
    else
    {
        assert((order.is_trailing_stop() || order.is_trailing_stop_limit()) &&
               "Unsupported passive order deletion");
        order_book.delete_trailing_stop_order(order);
    }
}

bool MarketManager::is_stop_triggered(const OrderBook& order_book,
                                      const Order& order) noexcept
{
    assert((order.is_stop() || order.is_stop_limit() ||
            order.is_trailing_stop() || order.is_trailing_stop_limit()) &&
           "Trigger check requires a stop-family order");

    const Price market_price = order.is_buy() ? order_book.market_ask_price()
                                               : order_book.market_bid_price();
    if ((order.is_buy() &&
         (market_price == std::numeric_limits<Price>::max())) ||
        (order.is_sell() && (market_price == 0)))
    {
        return false;
    }

    return order.is_buy() ? (market_price >= order.stop_price)
                          : (market_price <= order.stop_price);
}

ErrorCode MarketManager::add_triggered_order(OrderBook& order_book,
                                             const Order& order)
{
    assert(is_stop_triggered(order_book, order) &&
           "Immediate activation requires an eligible stop");

    if (active_orders_.contains(order.id))
        return ErrorCode::ORDER_DUPLICATE;

    Order incoming(order);
    if (incoming.is_stop() || incoming.is_trailing_stop())
    {
        market_handler_.on_add_order(incoming);

        const bool preserve_fok = incoming.is_fok();
        incoming.type = OrderType::MARKET;
        incoming.price = 0;
        incoming.stop_price = 0;
        incoming.time_in_force = preserve_fok ? OrderTimeInForce::FOK
                                              : OrderTimeInForce::IOC;
        incoming.max_display_quantity = Order::FULLY_DISPLAYED;
        incoming.trailing_distance = 0;
        incoming.trailing_step = 0;
        market_handler_.on_update_order(incoming);

        Price execution_bound = 0;
        const bool has_opposite_liquidity =
            resolve_execution_bound(order_book, incoming, execution_bound);
        const bool can_fill =
            !incoming.is_fok() ||
            (has_opposite_liquidity &&
             (calculate_executable_quantity(
                  order_book, incoming, execution_bound) == incoming.quantity));
        if (has_opposite_liquidity && can_fill)
        {
            static_cast<void>(
                match_incoming(order_book, incoming, execution_bound));
        }

        market_handler_.on_delete_order(incoming);
        match_order_book(order_book);
        return ErrorCode::OK;
    }

    assert((incoming.is_stop_limit() || incoming.is_trailing_stop_limit()) &&
           "Unsupported immediate stop-family order");
    incoming.type = OrderType::LIMIT;
    incoming.stop_price = 0;
    incoming.slippage = Order::UNLIMITED_SLIPPAGE;
    incoming.trailing_distance = 0;
    incoming.trailing_step = 0;

    Price execution_bound = 0;
    const bool has_opposite_liquidity =
        resolve_execution_bound(order_book, incoming, execution_bound);
    const bool requires_complete_fill = incoming.is_fok() || incoming.is_aon();
    const bool can_fill =
        !requires_complete_fill ||
        (has_opposite_liquidity &&
         (calculate_executable_quantity(order_book, incoming, execution_bound) ==
          incoming.quantity));
    const bool persistent = incoming.is_gtc() || incoming.is_aon();
    const bool may_leave_residual =
        persistent && (!incoming.is_aon() || !can_fill);

    LevelNode* destination_level = nullptr;
    if (may_leave_residual)
    {
        const RelocationStatus preparation =
            prepare_new_order_level(order_book, incoming, destination_level);
        if (preparation == RelocationStatus::QUANTITY_OVERFLOW)
            return ErrorCode::ORDER_QUANTITY_INVALID;
        if (preparation == RelocationStatus::ALLOCATION_FAILURE)
            throw std::bad_alloc();

        active_orders_.reserve(active_orders_.size() + 1);
        if (!order_pool_.reserve(order_pool_.live_count() + 1))
            throw std::bad_alloc();
    }

    market_handler_.on_add_order(order);
    market_handler_.on_update_order(incoming);

    if (has_opposite_liquidity && can_fill)
    {
        static_cast<void>(match_incoming(order_book, incoming, execution_bound));
    }

    if ((incoming.quantity > 0) && persistent)
    {
        OrderNode* stored_order = order_pool_.create(incoming);
        if (stored_order == nullptr)
        {
            assert(false && "Reserved activated residual slot is unavailable");
            std::terminate();
        }

        const bool inserted = active_orders_.insert(stored_order->id, stored_order);
        if (!inserted)
        {
            assert(false && "Prechecked activated order ID became unavailable");
            std::terminate();
        }

        std::optional<LevelUpdate> level_update;
        if (!add_to_order_book_at_level(
                order_book, *stored_order, destination_level, level_update))
        {
            assert(false && "Preflighted activated residual insertion must not fail");
            std::terminate();
        }
        publish_level_update(order_book, *level_update);
    }
    else
        market_handler_.on_delete_order(incoming);

    match_order_book(order_book);
    return ErrorCode::OK;
}

ErrorCode MarketManager::add_order(const Order& order)
{
    const ErrorCode validation = order.validate();
    if (validation != ErrorCode::OK)
        return validation;

    OrderBook* order_book = find_order_book_mutable(order.symbol_id);
    if (order_book == nullptr)
        return ErrorCode::ORDER_BOOK_NOT_FOUND;

    const bool regular_order = order.is_market() || order.is_limit();
    const bool trailing_order =
        order.is_trailing_stop() || order.is_trailing_stop_limit();

    if (!regular_order && !is_supported_passive_order(order))
        return ErrorCode::ORDER_TYPE_INVALID;

    Order prepared_order(order);
    if (trailing_order && !prepare_trailing_order(*order_book, prepared_order))
        return ErrorCode::ORDER_PARAMETER_INVALID;

    if (!regular_order && matching_enabled_ &&
        is_stop_triggered(*order_book, prepared_order))
    {
        return add_triggered_order(*order_book, prepared_order);
    }

    const bool persistent_limit =
        prepared_order.is_limit() &&
        (prepared_order.is_gtc() || prepared_order.is_aon());

    if (regular_order && matching_enabled_)
    {
        if (active_orders_.contains(prepared_order.id))
            return ErrorCode::ORDER_DUPLICATE;

        Order incoming(prepared_order);
        Price execution_bound = 0;
        const bool has_opposite_liquidity =
            resolve_execution_bound(*order_book, incoming, execution_bound);
        const bool requires_complete_fill = incoming.is_fok() || incoming.is_aon();
        const bool can_fill =
            !requires_complete_fill ||
            (has_opposite_liquidity &&
             (calculate_executable_quantity(*order_book, incoming, execution_bound) ==
              incoming.quantity));

        LevelNode* destination_level = nullptr;
        const bool may_leave_residual =
            persistent_limit && (!incoming.is_aon() || !can_fill);
        if (may_leave_residual)
        {
            const RelocationStatus preparation =
                prepare_new_order_level(*order_book, prepared_order, destination_level);
            if (preparation == RelocationStatus::QUANTITY_OVERFLOW)
                return ErrorCode::ORDER_QUANTITY_INVALID;
            if (preparation == RelocationStatus::ALLOCATION_FAILURE)
                throw std::bad_alloc();

            active_orders_.reserve(active_orders_.size() + 1);
            if (!order_pool_.reserve(order_pool_.live_count() + 1))
                throw std::bad_alloc();
        }

        market_handler_.on_add_order(incoming);

        if (has_opposite_liquidity && can_fill)
            static_cast<void>(match_incoming(*order_book, incoming, execution_bound));

        if ((incoming.quantity > 0) && persistent_limit)
        {
            OrderNode* stored_order = order_pool_.create(incoming);
            if (stored_order == nullptr)
            {
                assert(false && "Reserved residual order slot is unavailable");
                std::terminate();
            }

            const bool inserted = active_orders_.insert(stored_order->id, stored_order);
            if (!inserted)
            {
                assert(false && "Prechecked residual order ID became unavailable");
                std::terminate();
            }

            std::optional<LevelUpdate> level_update;
            if (!add_to_order_book_at_level(
                    *order_book, *stored_order, destination_level, level_update))
            {
                assert(false && "Preflighted residual insertion must not fail");
                std::terminate();
            }
            publish_level_update(*order_book, *level_update);

        }
        else
            market_handler_.on_delete_order(incoming);

        match_order_book(*order_book);
        return ErrorCode::OK;
    }

    if (regular_order && !persistent_limit)
    {
        if (active_orders_.contains(prepared_order.id))
            return ErrorCode::ORDER_DUPLICATE;

        market_handler_.on_add_order(prepared_order);
        market_handler_.on_delete_order(prepared_order);
        return ErrorCode::OK;
    }

    OrderNode* stored_order = order_pool_.create(prepared_order);
    if (stored_order == nullptr)
        throw std::bad_alloc();

    bool inserted = false;
    try
    {
        inserted = active_orders_.insert(stored_order->id, stored_order);
    }
    catch (...)
    {
        order_pool_.release(*stored_order);
        throw;
    }

    if (!inserted)
    {
        order_pool_.release(*stored_order);
        return ErrorCode::ORDER_DUPLICATE;
    }

    std::optional<LevelUpdate> level_update;
    const bool stored = add_to_order_book(*order_book, *stored_order, level_update);

    if (!stored)
    {
        const bool quantity_valid = can_store_quantity(*order_book, prepared_order);
        const bool erased = active_orders_.erase(stored_order->id);
        assert(erased && "Failed order-book insertion must erase its registry entry");
        static_cast<void>(erased);
        order_pool_.release(*stored_order);

        if (!quantity_valid)
            return ErrorCode::ORDER_QUANTITY_INVALID;
        throw std::bad_alloc();
    }

    market_handler_.on_add_order(*stored_order);
    if (level_update.has_value())
        publish_level_update(*order_book, *level_update);
    process_if_enabled(*order_book);
    return ErrorCode::OK;
}

ErrorCode MarketManager::reduce_order(SymbolId symbol_id,
                                      OrderId order_id,
                                      Quantity reduction) noexcept
{
    if (order_id == 0)
        return ErrorCode::ORDER_ID_INVALID;
    if (reduction == 0)
        return ErrorCode::ORDER_QUANTITY_INVALID;

    OrderBook* order_book = nullptr;
    OrderNode* order = nullptr;
    const ErrorCode resolution =
        resolve_active_order(symbol_id, order_id, order_book, order);
    if (resolution != ErrorCode::OK)
        return resolution;
    if (reduction > order->quantity)
        return ErrorCode::ORDER_QUANTITY_INVALID;

    const bool complete = reduction == order->quantity;
    std::optional<LevelUpdate> level_update;
    reduce_in_order_book(*order_book, *order, reduction, level_update);

    if (complete)
    {
        const bool erased = active_orders_.erase(order_id);
        assert(erased && "Completed reduction must erase its registry entry");
        static_cast<void>(erased);
        market_handler_.on_delete_order(*order);
    }
    else
        market_handler_.on_update_order(*order);

    if (level_update.has_value())
        publish_level_update(*order_book, *level_update);

    if (complete)
        order_pool_.release(*order);

    process_if_enabled(*order_book);

    return ErrorCode::OK;
}

ErrorCode MarketManager::modify_order(SymbolId symbol_id,
                                      OrderId order_id,
                                      Price new_price,
                                      Quantity new_quantity)
{
    if (order_id == 0)
        return ErrorCode::ORDER_ID_INVALID;
    if (new_quantity == 0)
        return ErrorCode::ORDER_QUANTITY_INVALID;

    OrderBook* order_book = nullptr;
    OrderNode* order = nullptr;
    const ErrorCode resolution =
        resolve_active_order(symbol_id, order_id, order_book, order);
    if (resolution != ErrorCode::OK)
        return resolution;

    Order replacement(static_cast<const Order&>(*order));
    replacement.price = new_price;
    replacement.quantity = new_quantity;

    const ErrorCode validation = replacement.validate();
    if (validation != ErrorCode::OK)
        return validation;
    if (!is_supported_passive_order(replacement))
        return replacement.is_limit() ? ErrorCode::ORDER_PARAMETER_INVALID
                                      : ErrorCode::ORDER_TYPE_INVALID;

    if ((new_price == order->price) && (new_quantity == order->quantity))
        return ErrorCode::OK;

    if ((new_price == order->price) && (new_quantity < order->quantity))
    {
        std::optional<LevelUpdate> level_update;
        reduce_in_order_book(
            *order_book, *order, order->quantity - new_quantity, level_update);
        market_handler_.on_update_order(*order);
        if (level_update.has_value())
            publish_level_update(*order_book, *level_update);
        process_if_enabled(*order_book);
        return ErrorCode::OK;
    }

    LevelNode* destination_level = nullptr;
    const RelocationStatus relocation =
        prepare_relocation(*order_book, *order, replacement, destination_level);
    if (relocation == RelocationStatus::QUANTITY_OVERFLOW)
        return ErrorCode::ORDER_QUANTITY_INVALID;
    if (relocation == RelocationStatus::ALLOCATION_FAILURE)
        throw std::bad_alloc();

    std::optional<LevelUpdate> old_level_update;
    std::optional<LevelUpdate> new_level_update;

    delete_from_order_book(*order_book, *order, old_level_update);
    static_cast<Order&>(*order) = replacement;

    if (!add_to_order_book_at_level(
            *order_book, *order, destination_level, new_level_update))
    {
        // Every recoverable failure was handled before the old order was
        // unlinked, so this branch indicates an internal invariant failure.
        assert(false && "Preflighted modification must not fail during insertion");
        std::terminate();
    }

    market_handler_.on_update_order(*order);
    if (old_level_update.has_value())
        publish_level_update(*order_book, *old_level_update);
    if (new_level_update.has_value())
        publish_level_update(*order_book, *new_level_update);
    process_if_enabled(*order_book);
    return ErrorCode::OK;
}

ErrorCode MarketManager::replace_order(SymbolId symbol_id,
                                       OrderId order_id,
                                       OrderId new_order_id,
                                       Price new_price,
                                       Quantity new_quantity)
{
    if ((order_id == 0) || (new_order_id == 0))
        return ErrorCode::ORDER_ID_INVALID;
    if (new_quantity == 0)
        return ErrorCode::ORDER_QUANTITY_INVALID;
    if (new_price == 0)
        return ErrorCode::ORDER_PARAMETER_INVALID;

    OrderBook* order_book = nullptr;
    OrderNode* order = nullptr;
    const ErrorCode resolution =
        resolve_active_order(symbol_id, order_id, order_book, order);
    if (resolution != ErrorCode::OK)
        return resolution;
    if (!order->is_limit() || (!order->is_gtc() && !order->is_aon()))
        return ErrorCode::ORDER_TYPE_INVALID;

    const Order replacement = Order::Limit(new_order_id,
                                           symbol_id,
                                           order->side,
                                           new_price,
                                           new_quantity,
                                           order->time_in_force);
    return replace_resolved_order(*order_book, *order, replacement);
}

ErrorCode MarketManager::replace_order(SymbolId symbol_id,
                                       OrderId order_id,
                                       const Order& replacement)
{
    if (order_id == 0)
        return ErrorCode::ORDER_ID_INVALID;

    const ErrorCode validation = replacement.validate();
    if (validation != ErrorCode::OK)
        return validation;
    if (replacement.symbol_id != symbol_id)
        return ErrorCode::ORDER_PARAMETER_INVALID;
    if (!is_supported_passive_order(replacement))
        return replacement.is_limit() ? ErrorCode::ORDER_PARAMETER_INVALID
                                      : ErrorCode::ORDER_TYPE_INVALID;

    OrderBook* order_book = nullptr;
    OrderNode* order = nullptr;
    const ErrorCode resolution =
        resolve_active_order(symbol_id, order_id, order_book, order);
    if (resolution != ErrorCode::OK)
        return resolution;

    return replace_resolved_order(*order_book, *order, replacement);
}

ErrorCode MarketManager::replace_resolved_order(OrderBook& order_book,
                                                OrderNode& order,
                                                Order replacement)
{
    if (replacement.id == order.id)
        return ErrorCode::ORDER_DUPLICATE;

    if ((replacement.is_trailing_stop() || replacement.is_trailing_stop_limit()) &&
        !prepare_trailing_order(order_book, replacement, &order))
    {
        return ErrorCode::ORDER_PARAMETER_INVALID;
    }
    const bool inserted = active_orders_.insert(replacement.id, &order);
    if (!inserted)
        return ErrorCode::ORDER_DUPLICATE;

    LevelNode* destination_level = nullptr;
    const RelocationStatus relocation =
        prepare_relocation(order_book, order, replacement, destination_level);
    if (relocation == RelocationStatus::QUANTITY_OVERFLOW)
    {
        const bool erased = active_orders_.erase(replacement.id);
        assert(erased && "Invalid replacement must erase the reserved new ID");
        static_cast<void>(erased);
        return ErrorCode::ORDER_QUANTITY_INVALID;
    }

    if (relocation == RelocationStatus::ALLOCATION_FAILURE)
    {
        const bool erased = active_orders_.erase(replacement.id);
        assert(erased && "Failed replacement reservation must erase the new ID");
        static_cast<void>(erased);
        throw std::bad_alloc();
    }

    const Order original(static_cast<const Order&>(order));
    std::optional<LevelUpdate> old_level_update;
    std::optional<LevelUpdate> new_level_update;

    delete_from_order_book(order_book, order, old_level_update);
    static_cast<Order&>(order) = replacement;

    if (!add_to_order_book_at_level(
            order_book, order, destination_level, new_level_update))
    {
        // Every recoverable failure was handled before the old order was
        // unlinked, so this branch indicates an internal invariant failure.
        assert(false && "Preflighted replacement must not fail during insertion");
        std::terminate();
    }

    const bool old_id_erased = active_orders_.erase(original.id);
    assert(old_id_erased && "Successful replacement must erase the original ID");
    static_cast<void>(old_id_erased);

    market_handler_.on_delete_order(original);
    market_handler_.on_add_order(order);
    if (old_level_update.has_value())
        publish_level_update(order_book, *old_level_update);
    if (new_level_update.has_value())
        publish_level_update(order_book, *new_level_update);
    process_if_enabled(order_book);
    return ErrorCode::OK;
}

ErrorCode MarketManager::delete_order(SymbolId symbol_id, OrderId order_id) noexcept
{
    if (order_id == 0)
        return ErrorCode::ORDER_ID_INVALID;

    OrderBook* order_book = nullptr;
    OrderNode* order = nullptr;
    const ErrorCode resolution =
        resolve_active_order(symbol_id, order_id, order_book, order);
    if (resolution != ErrorCode::OK)
        return resolution;

    std::optional<LevelUpdate> level_update;
    delete_from_order_book(*order_book, *order, level_update);

    const bool erased = active_orders_.erase(order_id);
    assert(erased && "Deleted order must be erased from the registry");
    static_cast<void>(erased);

    market_handler_.on_delete_order(*order);
    if (level_update.has_value())
        publish_level_update(*order_book, *level_update);
    order_pool_.release(*order);

    process_if_enabled(*order_book);
    return ErrorCode::OK;
}

ErrorCode MarketManager::execute_order(SymbolId symbol_id,
                                       OrderId order_id,
                                       Quantity execution_quantity) noexcept
{
    if (order_id == 0)
        return ErrorCode::ORDER_ID_INVALID;
    if (execution_quantity == 0)
        return ErrorCode::ORDER_QUANTITY_INVALID;

    OrderBook* order_book = nullptr;
    OrderNode* order = nullptr;
    const ErrorCode resolution =
        resolve_active_order(symbol_id, order_id, order_book, order);
    if (resolution != ErrorCode::OK)
        return resolution;
    if (order->price == 0)
        return ErrorCode::ORDER_PARAMETER_INVALID;

    return execute_resolved_order(
        *order_book, *order, order->price, execution_quantity);
}

ErrorCode MarketManager::execute_order(SymbolId symbol_id,
                                       OrderId order_id,
                                       Price execution_price,
                                       Quantity execution_quantity) noexcept
{
    if (order_id == 0)
        return ErrorCode::ORDER_ID_INVALID;
    if (execution_quantity == 0)
        return ErrorCode::ORDER_QUANTITY_INVALID;
    if (execution_price == 0)
        return ErrorCode::ORDER_PARAMETER_INVALID;

    OrderBook* order_book = nullptr;
    OrderNode* order = nullptr;
    const ErrorCode resolution =
        resolve_active_order(symbol_id, order_id, order_book, order);
    if (resolution != ErrorCode::OK)
        return resolution;

    return execute_resolved_order(
        *order_book, *order, execution_price, execution_quantity);
}

ErrorCode MarketManager::execute_resolved_order(OrderBook& order_book,
                                                OrderNode& order,
                                                Price execution_price,
                                                Quantity execution_quantity) noexcept
{
    if (execution_quantity > order.quantity)
        return ErrorCode::ORDER_QUANTITY_INVALID;
    if (order.is_aon() && (execution_quantity != order.quantity))
        return ErrorCode::ORDER_QUANTITY_INVALID;

    const bool complete = execution_quantity == order.quantity;
    order_book.update_last_price(order, execution_price);
    order_book.update_matching_price(order, execution_price);

    std::optional<LevelUpdate> level_update;
    reduce_in_order_book(order_book, order, execution_quantity, level_update);

    if (complete)
    {
        const bool erased = active_orders_.erase(order.id);
        assert(erased && "Completed execution must erase its registry entry");
        static_cast<void>(erased);
    }

    market_handler_.on_execute_order(order, execution_price, execution_quantity);
    if (complete)
        market_handler_.on_delete_order(order);
    else
        market_handler_.on_update_order(order);

    if (level_update.has_value())
        publish_level_update(order_book, *level_update);

    if (complete)
        order_pool_.release(order);
    process_if_enabled(order_book);
    return ErrorCode::OK;
}

void MarketManager::enable_matching() noexcept
{
    matching_enabled_ = true;
    match();
}

void MarketManager::match() noexcept
{
    for (OrderBook* order_book : order_books_)
    {
        if (order_book != nullptr)
            match_order_book(*order_book);
    }
}

bool MarketManager::resolve_execution_bound(const OrderBook& order_book,
                                             const Order& order,
                                             Price& bound) noexcept
{
    const LevelNode* opposite_best = order.is_buy() ? order_book.best_ask_
                                                    : order_book.best_bid_;
    if (opposite_best == nullptr)
        return false;

    if (order.is_limit())
    {
        bound = order.price;
        return true;
    }

    assert(order.is_market() && "Only market and limit orders can match on arrival");
    if (order.is_buy())
    {
        const Price room = std::numeric_limits<Price>::max() - opposite_best->price;
        bound = (order.slippage > room) ? std::numeric_limits<Price>::max()
                                        : opposite_best->price + order.slippage;
    }
    else
    {
        bound = (order.slippage > opposite_best->price)
                    ? 0
                    : opposite_best->price - order.slippage;
    }

    return true;
}

bool MarketManager::is_price_eligible(const Order& order,
                                      Price level_price,
                                      Price bound) noexcept
{
    return order.is_buy() ? (level_price <= bound) : (level_price >= bound);
}

Quantity MarketManager::calculate_executable_quantity(const OrderBook& order_book,
                                                       const Order& order,
                                                       Price bound) noexcept
{
    Quantity remaining = order.quantity;
    const LevelNode* level = order.is_buy() ? order_book.best_ask_
                                           : order_book.best_bid_;

    while ((level != nullptr) && is_price_eligible(order, level->price, bound))
    {
        for (const OrderNode* passive = level->orders.front(); passive != nullptr;
             passive = passive->next)
        {
            assert((passive->quantity > 0) &&
                   "Executable order must have positive quantity");

            // A resting AON order is indivisible. If it is larger than the
            // incoming remainder, FIFO priority prevents looking behind it.
            if (passive->is_aon() && (passive->quantity > remaining))
                return order.quantity - remaining;

            const Quantity execution_quantity =
                std::min(remaining, passive->quantity);
            remaining -= execution_quantity;
            if (remaining == 0)
                return order.quantity;
        }

        level = order_book.next_level(*level);
    }

    return order.quantity - remaining;
}

bool MarketManager::match_incoming(OrderBook& order_book,
                                   Order& incoming,
                                   Price bound,
                                   bool registered_incoming) noexcept
{
    while (incoming.quantity > 0)
    {
        LevelNode* level = incoming.is_buy() ? order_book.best_ask_
                                             : order_book.best_bid_;
        if ((level == nullptr) ||
            !is_price_eligible(incoming, level->price, bound))
        {
            return false;
        }

        OrderNode* passive = level->orders.front();
        assert((passive != nullptr) && "Populated best level must have a FIFO front");
        assert((passive->quantity > 0) && "Passive order must have open quantity");

        if (passive->is_aon() && (passive->quantity > incoming.quantity))
            return true;

        const Quantity execution_quantity =
            std::min(incoming.quantity, passive->quantity);
        execute_incoming_trade(order_book,
                               incoming,
                               *passive,
                               passive->price,
                               execution_quantity,
                               registered_incoming);
    }

    return false;
}

Quantity MarketManager::calculate_crossed_chain(
    const OrderBook& order_book) noexcept
{
    const LevelNode* bid_level = order_book.best_bid_;
    const LevelNode* ask_level = order_book.best_ask_;
    if ((bid_level == nullptr) || (ask_level == nullptr))
        return 0;

    const OrderNode* bid = bid_level->orders.front();
    const OrderNode* ask = ask_level->orders.front();
    if ((bid == nullptr) || (ask == nullptr))
        return 0;

    Quantity bid_remaining = bid->quantity;
    Quantity ask_remaining = ask->quantity;
    Quantity chain_quantity = 0;
    bool bid_aon_open = false;
    bool ask_aon_open = false;

    const auto advance = [&order_book](const LevelNode*& level,
                                       const OrderNode*& order) noexcept
    {
        if (order->next != nullptr)
        {
            order = order->next;
            return;
        }

        level = order_book.next_level(*level);
        order = (level != nullptr) ? level->orders.front() : nullptr;
    };

    // This is a mutation-free dry run. It stops at the first volume boundary
    // at which every AON order touched on both sides has been fully consumed.
    while ((bid != nullptr) && (ask != nullptr))
    {
        if (bid_level->price < ask_level->price)
            return 0;

        assert((bid_remaining > 0) && (ask_remaining > 0) &&
               "Matching-chain cursors must have positive quantity");
        const Quantity execution_quantity =
            std::min(bid_remaining, ask_remaining);
        if (chain_quantity >
            (std::numeric_limits<Quantity>::max() - execution_quantity))
        {
            return 0;
        }

        chain_quantity += execution_quantity;
        bid_remaining -= execution_quantity;
        ask_remaining -= execution_quantity;

        if (bid->is_aon())
            bid_aon_open = bid_remaining > 0;
        if (ask->is_aon())
            ask_aon_open = ask_remaining > 0;

        if (bid_remaining == 0)
        {
            advance(bid_level, bid);
            if (bid != nullptr)
                bid_remaining = bid->quantity;
        }
        if (ask_remaining == 0)
        {
            advance(ask_level, ask);
            if (ask != nullptr)
                ask_remaining = ask->quantity;
        }

        if (!bid_aon_open && !ask_aon_open)
            return chain_quantity;
    }

    return 0;
}

void MarketManager::process_if_enabled(OrderBook& order_book) noexcept
{
    if (!matching_enabled_)
    {
        order_book.reset_matching_price();
        return;
    }

    const bool crossed = (order_book.best_bid_ != nullptr) &&
                         (order_book.best_ask_ != nullptr) &&
                         (order_book.best_bid_->price >= order_book.best_ask_->price);
    const bool has_advanced_orders =
        (order_book.best_buy_stop_ != nullptr) ||
        (order_book.best_sell_stop_ != nullptr) ||
        (order_book.best_trailing_buy_stop_ != nullptr) ||
        (order_book.best_trailing_sell_stop_ != nullptr);

    if (crossed || has_advanced_orders)
        match_order_book(order_book);
    else
        order_book.reset_matching_price();
}

void MarketManager::match_order_book(OrderBook& order_book) noexcept
{
    for (;;)
    {
        while ((order_book.best_bid_ != nullptr) &&
               (order_book.best_ask_ != nullptr) &&
               (order_book.best_bid_->price >= order_book.best_ask_->price))
        {
            OrderNode* bid = order_book.best_bid_->orders.front();
            OrderNode* ask = order_book.best_ask_->orders.front();
            assert((bid != nullptr) && "Crossed best bid must have a FIFO front");
            assert((ask != nullptr) && "Crossed best ask must have a FIFO front");

            if (bid->is_aon() || ask->is_aon())
            {
                const Quantity chain_quantity = calculate_crossed_chain(order_book);
                if (chain_quantity == 0)
                    break;

                execute_crossed_chain(order_book, chain_quantity);
                continue;
            }

            const Quantity execution_quantity =
                std::min(bid->quantity, ask->quantity);

            // A stored cross has no recoverable aggressor without an arrival field.
            // The ask price is therefore the manager-wide deterministic auction rule.
            execute_crossed_trade(
                order_book, *bid, *ask, ask->price, execution_quantity);
        }

        if (activate_advanced_orders(order_book))
            continue;

        const bool has_execution_reference =
            (order_book.matching_bid_price_ != 0) ||
            (order_book.matching_ask_price_ != std::numeric_limits<Price>::max());
        if (has_execution_reference)
        {
            // First evaluate the execution that just occurred. Then clear that
            // temporary reference and evaluate the newly exposed top of book.
            order_book.reset_matching_price();
            if (activate_advanced_orders(order_book))
                continue;
        }

        break;
    }

    order_book.reset_matching_price();
}

bool MarketManager::activate_advanced_orders(OrderBook& order_book) noexcept
{
    const Price ask_price = order_book.market_ask_price();
    if (ask_price != std::numeric_limits<Price>::max())
    {
        if (activate_stop_level(order_book, order_book.best_buy_stop_, ask_price))
            return true;
        if (activate_stop_level(
                order_book, order_book.best_trailing_buy_stop_, ask_price))
        {
            return true;
        }
    }

    if (reprice_trailing_orders(order_book, OrderSide::BUY))
        return true;

    const Price bid_price = order_book.market_bid_price();
    if (bid_price != 0)
    {
        if (activate_stop_level(order_book, order_book.best_sell_stop_, bid_price))
            return true;
        if (activate_stop_level(
                order_book, order_book.best_trailing_sell_stop_, bid_price))
        {
            return true;
        }
    }

    return reprice_trailing_orders(order_book, OrderSide::SELL);
}

bool MarketManager::activate_stop_level(OrderBook& order_book,
                                        LevelNode* level,
                                        Price market_price) noexcept
{
    if (level == nullptr)
        return false;

    const bool triggered = level->is_ask() ? (market_price >= level->price)
                                           : (market_price <= level->price);
    if (!triggered)
        return false;

    bool activated = false;
    for (;;)
    {
        OrderNode* order = level->orders.front();
        assert((order != nullptr) && "Triggered stop level must contain an order");
        if (order == nullptr)
            break;

        const bool last_order = level->order_count == 1;
        const bool result = (order->is_stop() || order->is_trailing_stop())
                                ? activate_market_stop(order_book, *order)
                                : activate_limit_stop(order_book, *order);
        if (!result)
            break;

        activated = true;
        if (last_order)
            break;
    }

    return activated;
}

bool MarketManager::activate_market_stop(OrderBook& order_book,
                                         OrderNode& order) noexcept
{
    const bool trailing = order.is_trailing_stop();
    assert((trailing || order.is_stop()) &&
           "Market-stop activation requires a plain or trailing stop");

    if (trailing)
        order_book.delete_trailing_stop_order(order);
    else
        order_book.delete_stop_order(order);

    const bool preserve_fok = order.is_fok();
    order.type = OrderType::MARKET;
    order.price = 0;
    order.stop_price = 0;
    order.time_in_force = preserve_fok ? OrderTimeInForce::FOK
                                       : OrderTimeInForce::IOC;
    order.max_display_quantity = Order::FULLY_DISPLAYED;
    order.trailing_distance = 0;
    order.trailing_step = 0;

    market_handler_.on_update_order(order);

    Price execution_bound = 0;
    const bool has_opposite_liquidity =
        resolve_execution_bound(order_book, order, execution_bound);
    const bool can_fill =
        !order.is_fok() ||
        (has_opposite_liquidity &&
         (calculate_executable_quantity(order_book, order, execution_bound) ==
          order.quantity));

    if (has_opposite_liquidity && can_fill)
    {
        static_cast<void>(
            match_incoming(order_book, order, execution_bound, true));
    }

    if (order.quantity > 0)
    {
        const bool erased = active_orders_.erase(order.id);
        assert(erased && "Cancelled activated stop must leave the registry");
        static_cast<void>(erased);
    }
    else
    {
        assert((active_orders_.find(order.id) == nullptr) &&
               "Filled activated stop must already have left the registry");
    }

    market_handler_.on_delete_order(order);
    order_pool_.release(order);
    return true;
}

bool MarketManager::activate_limit_stop(OrderBook& order_book,
                                        OrderNode& order) noexcept
{
    const bool trailing = order.is_trailing_stop_limit();
    assert((trailing || order.is_stop_limit()) &&
           "Limit-stop activation requires a plain or trailing stop-limit");

    Order activated(static_cast<const Order&>(order));
    activated.type = OrderType::LIMIT;
    activated.stop_price = 0;
    activated.slippage = Order::UNLIMITED_SLIPPAGE;
    activated.trailing_distance = 0;
    activated.trailing_step = 0;

    Price execution_bound = 0;
    const bool has_opposite_liquidity =
        resolve_execution_bound(order_book, activated, execution_bound);
    const bool requires_complete_fill = activated.is_fok() || activated.is_aon();
    const bool can_fill =
        !requires_complete_fill ||
        (has_opposite_liquidity &&
         (calculate_executable_quantity(order_book, activated, execution_bound) ==
          activated.quantity));
    const bool persistent = activated.is_gtc() || activated.is_aon();
    const bool may_leave_residual =
        persistent && (!activated.is_aon() || !can_fill);

    LevelNode* destination_level = nullptr;
    if (may_leave_residual)
    {
        const RelocationStatus preparation =
            prepare_new_order_level(order_book, activated, destination_level);
        if (preparation != RelocationStatus::READY)
        {
            assert(false && "Triggered stop-limit capacity was not available");
            std::terminate();
        }
    }

    if (trailing)
        order_book.delete_trailing_stop_order(order);
    else
        order_book.delete_stop_order(order);

    static_cast<Order&>(order) = activated;
    market_handler_.on_update_order(order);

    if (has_opposite_liquidity && can_fill)
    {
        static_cast<void>(
            match_incoming(order_book, order, execution_bound, true));
    }

    if ((order.quantity > 0) && persistent)
    {
        std::optional<LevelUpdate> level_update;
        if (!add_to_order_book_at_level(
                order_book, order, destination_level, level_update))
        {
            assert(false && "Preflighted activated limit insertion must not fail");
            std::terminate();
        }

        assert(level_update.has_value() &&
               "Activated resting limit must publish a level update");
        publish_level_update(order_book, *level_update);
        return true;
    }

    if (order.quantity > 0)
    {
        const bool erased = active_orders_.erase(order.id);
        assert(erased && "Cancelled activated stop-limit must leave the registry");
        static_cast<void>(erased);
    }
    else
    {
        assert((active_orders_.find(order.id) == nullptr) &&
               "Filled activated stop-limit must already have left the registry");
    }

    market_handler_.on_delete_order(order);
    order_pool_.release(order);
    return true;
}

bool MarketManager::reprice_trailing_orders(OrderBook& order_book,
                                            OrderSide side) noexcept
{
    const bool buy = side == OrderSide::BUY;
    LevelNode* first_level = buy ? order_book.best_trailing_buy_stop_
                                 : order_book.best_trailing_sell_stop_;
    if (first_level == nullptr)
        return false;

    const Price new_market_price = buy ? order_book.market_trailing_ask_price()
                                       : order_book.market_trailing_bid_price();
    if ((buy && (new_market_price == std::numeric_limits<Price>::max())) ||
        (!buy && (new_market_price == 0)))
    {
        return false;
    }

    Price& cached_market_price = buy ? order_book.trailing_ask_price_
                                     : order_book.trailing_bid_price_;
    const Price old_market_price = cached_market_price;
    cached_market_price = new_market_price;

    const bool favorable = buy ? (new_market_price < old_market_price)
                               : (new_market_price > old_market_price);
    if (!favorable)
        return false;

    bool any_repriced = false;
    LevelNode* level = first_level;
    while (level != nullptr)
    {
        bool level_changed = false;
        OrderNode* order = level->orders.front();
        while (order != nullptr)
        {
            OrderNode* next_order = order->next;
            Order replacement(static_cast<const Order&>(*order));
            if (!prepare_trailing_order(order_book, replacement))
            {
                order = next_order;
                continue;
            }

            if (replacement.stop_price != order->stop_price)
            {
                LevelNode* destination_level = nullptr;
                const RelocationStatus relocation = prepare_relocation(
                    order_book, *order, replacement, destination_level);
                if (relocation != RelocationStatus::READY)
                {
                    assert(false && "Trailing repricing capacity was not available");
                    std::terminate();
                }

                order_book.delete_trailing_stop_order(*order);
                static_cast<Order&>(*order) = replacement;
                if (!order_book.add_trailing_stop_order(*order, destination_level))
                {
                    assert(false && "Preflighted trailing repricing must not fail");
                    std::terminate();
                }

                market_handler_.on_update_order(*order);
                any_repriced = true;
                level_changed = true;
            }

            order = next_order;
        }

        if (level_changed)
        {
            level = buy ? order_book.best_trailing_buy_stop_
                        : order_book.best_trailing_sell_stop_;
        }
        else
        {
            level = const_cast<LevelNode*>(
                order_book.next_trailing_stop_level(*level));
        }
    }

    return any_repriced;
}

void MarketManager::execute_crossed_chain(OrderBook& order_book,
                                          Quantity execution_quantity) noexcept
{
    assert((execution_quantity > 0) && "Matching chain must be non-empty");

    while (execution_quantity > 0)
    {
        OrderNode* bid = order_book.best_bid_->orders.front();
        OrderNode* ask = order_book.best_ask_->orders.front();
        assert((bid != nullptr) && (ask != nullptr) &&
               "Precalculated chain must retain both sides");
        assert((bid->price >= ask->price) &&
               "Precalculated chain must contain only crossed prices");

        const Quantity matched =
            std::min(execution_quantity, std::min(bid->quantity, ask->quantity));
        execute_crossed_trade(order_book, *bid, *ask, ask->price, matched);
        execution_quantity -= matched;
    }
}

void MarketManager::execute_incoming_trade(OrderBook& order_book,
                                           Order& incoming,
                                           OrderNode& passive,
                                           Price execution_price,
                                           Quantity execution_quantity,
                                           bool registered_incoming) noexcept
{
    assert((execution_quantity > 0) && "Matched quantity must be positive");
    assert((execution_quantity <= incoming.quantity) &&
           "Execution exceeds incoming quantity");
    assert((execution_quantity <= passive.quantity) &&
           "Execution exceeds passive quantity");
    assert((passive.level != nullptr) &&
           "Passive order must belong to a price level");

    const bool passive_complete = execution_quantity == passive.quantity;
    std::optional<LevelUpdate> passive_level_update(
        order_book.reduce_order(passive, execution_quantity));
    incoming.quantity -= execution_quantity;
    const bool incoming_complete = incoming.quantity == 0;

    if (passive_complete)
    {
        const bool erased = active_orders_.erase(passive.id);
        assert(erased && "Completed passive order must leave the registry");
        static_cast<void>(erased);
    }
    if (registered_incoming && incoming_complete)
    {
        const bool erased = active_orders_.erase(incoming.id);
        assert(erased && "Completed activated order must leave the registry");
        static_cast<void>(erased);
    }
    order_book.update_last_price(passive, execution_price);
    order_book.update_matching_price(passive, execution_price);
    order_book.update_last_price(incoming, execution_price);
    order_book.update_matching_price(incoming, execution_price);

    publish_execution_result(order_book,
                             passive,
                             execution_price,
                             execution_quantity,
                             passive_level_update);
    market_handler_.on_execute_order(
        incoming, execution_price, execution_quantity);

    if (passive_complete)
        order_pool_.release(passive);
}

void MarketManager::execute_crossed_trade(OrderBook& order_book,
                                          OrderNode& bid,
                                          OrderNode& ask,
                                          Price execution_price,
                                          Quantity execution_quantity) noexcept
{
    assert((execution_quantity > 0) && "Matched quantity must be positive");
    assert((execution_quantity <= bid.quantity) && "Execution exceeds bid quantity");
    assert((execution_quantity <= ask.quantity) && "Execution exceeds ask quantity");

    const bool bid_complete = execution_quantity == bid.quantity;
    const bool ask_complete = execution_quantity == ask.quantity;

    std::optional<LevelUpdate> ask_level_update(
        order_book.reduce_order(ask, execution_quantity));
    std::optional<LevelUpdate> bid_level_update(
        order_book.reduce_order(bid, execution_quantity));

    if (ask_complete)
    {
        const bool erased = active_orders_.erase(ask.id);
        assert(erased && "Completed ask must leave the registry");
        static_cast<void>(erased);
    }
    if (bid_complete)
    {
        const bool erased = active_orders_.erase(bid.id);
        assert(erased && "Completed bid must leave the registry");
        static_cast<void>(erased);
    }

    order_book.update_last_price(ask, execution_price);
    order_book.update_matching_price(ask, execution_price);
    order_book.update_last_price(bid, execution_price);
    order_book.update_matching_price(bid, execution_price);

    publish_execution_result(order_book,
                             ask,
                             execution_price,
                             execution_quantity,
                             ask_level_update);
    publish_execution_result(order_book,
                             bid,
                             execution_price,
                             execution_quantity,
                             bid_level_update);

    if (ask_complete)
        order_pool_.release(ask);
    if (bid_complete)
        order_pool_.release(bid);
}

void MarketManager::publish_execution_result(
    const OrderBook& order_book,
    OrderNode& order,
    Price execution_price,
    Quantity execution_quantity,
    const std::optional<LevelUpdate>& level_update) noexcept
{
    market_handler_.on_execute_order(order, execution_price, execution_quantity);
    if (order.quantity == 0)
        market_handler_.on_delete_order(order);
    else
        market_handler_.on_update_order(order);

    if (level_update.has_value())
        publish_level_update(order_book, *level_update);
}

bool MarketManager::prepare_trailing_order(OrderBook& order_book,
                                           Order& order,
                                           const OrderNode* excluded_order) noexcept
{
    assert((order.is_trailing_stop() || order.is_trailing_stop_limit()) &&
           "Only trailing orders require trigger preparation");

    const Price previous_stop_price = order.stop_price;
    const Price adjusted_stop_price =
        order_book.calculate_trailing_stop_price(order, excluded_order);
    Price adjusted_limit_price = order.price;

    if (order.is_trailing_stop_limit())
    {
        if (order.price >= previous_stop_price)
        {
            const Price offset = order.price - previous_stop_price;
            if (adjusted_stop_price >
                (std::numeric_limits<Price>::max() - offset))
            {
                return false;
            }

            adjusted_limit_price = adjusted_stop_price + offset;
        }
        else
        {
            const Price offset = previous_stop_price - order.price;
            if (adjusted_stop_price <= offset)
                return false;

            adjusted_limit_price = adjusted_stop_price - offset;
        }
    }

    assert((adjusted_stop_price > 0) && "Prepared trailing trigger must be positive");
    assert((!order.is_trailing_stop_limit() || (adjusted_limit_price > 0)) &&
           "Prepared trailing limit price must be positive");

    order.stop_price = adjusted_stop_price;
    order.price = adjusted_limit_price;
    return true;
}

void MarketManager::publish_level_update(const OrderBook& order_book,
                                         const LevelUpdate& update) noexcept
{
    switch (update.type)
    {
        case UpdateType::ADD:
            market_handler_.on_add_level(order_book, update.level, update.top);
            break;
        case UpdateType::UPDATE:
            market_handler_.on_update_level(order_book, update.level, update.top);
            break;
        case UpdateType::DELETE:
            market_handler_.on_delete_level(order_book, update.level, update.top);
            break;
    }

    market_handler_.on_update_order_book(order_book, update.top);
}

} // namespace matching
