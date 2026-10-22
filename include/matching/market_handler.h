#ifndef MATCHING_MARKET_HANDLER_H
#define MATCHING_MARKET_HANDLER_H

#include "level.h"
#include "order.h"
#include "symbol.h"
#include "types.h"

namespace matching {

class MarketManager;
class OrderBook;

class MarketHandler
{
    friend class MarketManager;

public:
    MarketHandler() noexcept = default;
    MarketHandler(const MarketHandler&) = delete;
    MarketHandler(MarketHandler&&) = delete;
    virtual ~MarketHandler() noexcept = default;

    MarketHandler& operator=(const MarketHandler&) = delete;
    MarketHandler& operator=(MarketHandler&&) = delete;

protected:
    // Callbacks run synchronously after a successful logical change. References
    // are valid only for the duration of the call and must not be retained.
    // An accepted incoming order can be a transient value that is absent from
    // the resting-order registry. A surviving GTC residual is registered before
    // its level/book updates are published. Callbacks must not re-enter a
    // mutating MarketManager operation.
    virtual void on_add_symbol(const Symbol&) noexcept {}
    virtual void on_delete_symbol(const Symbol&) noexcept {}

    virtual void on_add_order_book(const OrderBook&) noexcept {}
    virtual void on_update_order_book(const OrderBook&, bool) noexcept {}
    virtual void on_delete_order_book(const OrderBook&) noexcept {}

    // Level and book-update callbacks describe displayed bid/ask depth.
    // Dormant stop indexes are reported through order callbacks only.
    virtual void on_add_level(const OrderBook&, const Level&, bool) noexcept {}
    virtual void on_update_level(const OrderBook&, const Level&, bool) noexcept {}
    virtual void on_delete_level(const OrderBook&, const Level&, bool) noexcept {}

    virtual void on_add_order(const Order&) noexcept {}
    virtual void on_update_order(const Order&) noexcept {}
    virtual void on_delete_order(const Order&) noexcept {}

    virtual void on_execute_order(const Order&, Price, Quantity) noexcept {}
};

} // namespace matching

#endif // MATCHING_MARKET_HANDLER_H
