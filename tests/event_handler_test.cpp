#include "matching/events/event_handler.h"
#include "matching/market_handler.h"

#include <catch_amalgamated.hpp>

#include <cstddef>
#include <type_traits>

namespace {

class RecordingEventHandler final : public matching::EventHandler
{
public:
    void record_add_symbol(const matching::Symbol& symbol) noexcept
    {
        on_add_symbol(symbol);
    }

    void record_add_order(const matching::Order& order) noexcept
    {
        on_add_order(order);
    }

    void record_execution(const matching::Order& order,
                          matching::Price price,
                          matching::Quantity quantity) noexcept
    {
        on_execute_order(order, price, quantity);
    }

    std::size_t symbol_additions{0};
    std::size_t order_additions{0};
    std::size_t executions{0};

    matching::SymbolId last_symbol_id{0};
    matching::OrderId last_order_id{0};
    matching::Price last_execution_price{0};
    matching::Quantity last_execution_quantity{0};
    matching::Quantity last_remaining_quantity{0};

protected:
    void on_add_symbol(const matching::Symbol& symbol) noexcept override
    {
        ++symbol_additions;
        last_symbol_id = symbol.id;
    }

    void on_add_order(const matching::Order& order) noexcept override
    {
        ++order_additions;
        last_order_id = order.id;
    }

    void on_execute_order(const matching::Order& order,
                          matching::Price price,
                          matching::Quantity quantity) noexcept override
    {
        ++executions;
        last_order_id = order.id;
        last_execution_price = price;
        last_execution_quantity = quantity;
        last_remaining_quantity = order.quantity;
    }
};

static_assert(std::is_same_v<matching::MarketHandler, matching::EventHandler>);
static_assert(std::is_default_constructible_v<matching::EventHandler>);
static_assert(std::has_virtual_destructor_v<matching::EventHandler>);
static_assert(!std::is_copy_constructible_v<matching::EventHandler>);
static_assert(!std::is_copy_assignable_v<matching::EventHandler>);
static_assert(!std::is_move_constructible_v<matching::EventHandler>);
static_assert(!std::is_move_assignable_v<matching::EventHandler>);

} // namespace

TEST_CASE("Event handler overrides receive public market values", "[event_handler]")
{
    constexpr matching::SymbolId SYMBOL_ID = 7;
    constexpr matching::OrderId ORDER_ID = 42;
    constexpr matching::Price ORDER_PRICE = 18'525;
    constexpr matching::Quantity ORIGINAL_QUANTITY = 100;
    constexpr matching::Quantity EXECUTED_QUANTITY = 40;

    const matching::Symbol symbol(SYMBOL_ID, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::Order order = matching::Order::BuyLimit(
        ORDER_ID, SYMBOL_ID, ORDER_PRICE, ORIGINAL_QUANTITY);
    RecordingEventHandler handler;
    handler.record_add_symbol(symbol);
    handler.record_add_order(order);

    order.quantity -= EXECUTED_QUANTITY;
    handler.record_execution(order, ORDER_PRICE, EXECUTED_QUANTITY);

    REQUIRE(handler.symbol_additions == 1);
    REQUIRE(handler.order_additions == 1);
    REQUIRE(handler.executions == 1);
    REQUIRE(handler.last_symbol_id == SYMBOL_ID);
    REQUIRE(handler.last_order_id == ORDER_ID);
    REQUIRE(handler.last_execution_price == ORDER_PRICE);
    REQUIRE(handler.last_execution_quantity == EXECUTED_QUANTITY);
    REQUIRE(handler.last_remaining_quantity == (ORIGINAL_QUANTITY - EXECUTED_QUANTITY));
}
