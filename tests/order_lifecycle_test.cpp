#include "matching/market_manager.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <vector>

namespace {

enum class EventType
{
    ADD_ORDER,
    UPDATE_ORDER,
    DELETE_ORDER,
    EXECUTE_ORDER,
    ADD_LEVEL,
    UPDATE_LEVEL,
    DELETE_LEVEL,
    UPDATE_ORDER_BOOK
};

struct Event
{
    EventType type;
    matching::SymbolId symbol_id;
    matching::OrderId order_id{0};
    matching::Price price{0};
    matching::Quantity quantity{0};
    matching::Quantity execution_quantity{0};
    std::size_t order_count{0};
    bool top{false};
};

class LifecycleHandler final : public matching::MarketHandler
{
public:
    matching::MarketManager* manager{nullptr};
    std::array<Event, 256> events{};
    std::size_t event_count{0};
    bool callback_state_valid{true};

    void reset() noexcept
    {
        event_count = 0;
        callback_state_valid = true;
    }

protected:
    void on_add_order(const matching::Order& order) noexcept override
    {
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_order(order.id) == &order);
        record({EventType::ADD_ORDER,
                order.symbol_id,
                order.id,
                order.price,
                order.quantity});
    }

    void on_update_order(const matching::Order& order) noexcept override
    {
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_order(order.id) == &order);
        record({EventType::UPDATE_ORDER,
                order.symbol_id,
                order.id,
                order.price,
                order.quantity});
    }

    void on_delete_order(const matching::Order& order) noexcept override
    {
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_order(order.id) == nullptr);
        record({EventType::DELETE_ORDER,
                order.symbol_id,
                order.id,
                order.price,
                order.quantity});
    }

    void on_execute_order(const matching::Order& order,
                          matching::Price execution_price,
                          matching::Quantity execution_quantity) noexcept override
    {
        const matching::Order* active_order =
            (manager != nullptr) ? manager->find_order(order.id) : nullptr;
        callback_state_valid =
            callback_state_valid && (manager != nullptr) &&
            ((order.quantity == 0) ? (active_order == nullptr)
                                   : (active_order == &order));
        record({EventType::EXECUTE_ORDER,
                order.symbol_id,
                order.id,
                execution_price,
                order.quantity,
                execution_quantity});
    }

    void on_add_level(const matching::OrderBook& order_book,
                      const matching::Level& level,
                      bool top) noexcept override
    {
        record_level(EventType::ADD_LEVEL, order_book, level, top);
    }

    void on_update_level(const matching::OrderBook& order_book,
                         const matching::Level& level,
                         bool top) noexcept override
    {
        record_level(EventType::UPDATE_LEVEL, order_book, level, top);
    }

    void on_delete_level(const matching::OrderBook& order_book,
                         const matching::Level& level,
                         bool top) noexcept override
    {
        record_level(EventType::DELETE_LEVEL, order_book, level, top);
    }

    void on_update_order_book(const matching::OrderBook& order_book,
                              bool top) noexcept override
    {
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_order_book(order_book.symbol().id) ==
                                &order_book);
        record({EventType::UPDATE_ORDER_BOOK,
                order_book.symbol().id,
                0,
                0,
                0,
                0,
                0,
                top});
    }

private:
    void record(const Event& event) noexcept
    {
        if (event_count < events.size())
            events[event_count++] = event;
        else
            callback_state_valid = false;
    }

    void record_level(EventType type,
                      const matching::OrderBook& order_book,
                      const matching::Level& level,
                      bool top) noexcept
    {
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_order_book(order_book.symbol().id) ==
                                &order_book);
        record({type,
                order_book.symbol().id,
                0,
                level.price,
                level.total_quantity,
                0,
                level.order_count,
                top});
    }
};

const matching::Symbol RELIANCE(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
const matching::Symbol TCS(2, {'T', 'C', 'S', ' ', ' ', ' ', ' ', ' '});

void initialize_market(matching::MarketManager& manager, bool second_symbol = false)
{
    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);

    if (second_symbol)
    {
        REQUIRE(manager.add_symbol(TCS) == matching::ErrorCode::OK);
        REQUIRE(manager.add_order_book(TCS.id) == matching::ErrorCode::OK);
    }
}

void require_event_types(const LifecycleHandler& handler,
                         std::initializer_list<EventType> expected)
{
    REQUIRE(handler.event_count == expected.size());

    std::size_t index = 0;
    for (const EventType type : expected)
    {
        REQUIRE(handler.events[index].type == type);
        ++index;
    }
}

[[nodiscard]] const matching::LevelNode* bid_level(
    const matching::MarketManager& manager,
    matching::SymbolId symbol_id,
    matching::Price price)
{
    const matching::OrderBook* order_book = manager.find_order_book(symbol_id);
    REQUIRE(order_book != nullptr);
    return static_cast<const matching::LevelNode*>(order_book->find_bid(price));
}

[[nodiscard]] const matching::LevelNode* buy_stop_level(
    const matching::MarketManager& manager,
    matching::SymbolId symbol_id,
    matching::Price price)
{
    const matching::OrderBook* order_book = manager.find_order_book(symbol_id);
    REQUIRE(order_book != nullptr);
    return static_cast<const matching::LevelNode*>(order_book->find_buy_stop(price));
}

[[nodiscard]] const matching::LevelNode* trailing_buy_stop_level(
    const matching::MarketManager& manager,
    matching::SymbolId symbol_id,
    matching::Price price)
{
    const matching::OrderBook* order_book = manager.find_order_book(symbol_id);
    REQUIRE(order_book != nullptr);
    return static_cast<const matching::LevelNode*>(
        order_book->find_trailing_buy_stop(price));
}

[[nodiscard]] std::vector<matching::OrderId> fifo_ids(const matching::LevelNode* level)
{
    std::vector<matching::OrderId> result;
    if (level == nullptr)
        return result;

    for (const matching::OrderNode* order = level->orders.front(); order != nullptr;
         order = order->next)
    {
        result.push_back(order->id);
    }
    return result;
}

} // namespace

TEST_CASE("Reductions preserve FIFO and reject invalid quantities atomically",
          "[order_lifecycle]")
{
    LifecycleHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager, true);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, RELIANCE.id, 18'525, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, RELIANCE.id, 18'525, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, RELIANCE.id, 18'525, 30)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.reduce_order(RELIANCE.id, 0, 1) ==
            matching::ErrorCode::ORDER_ID_INVALID);
    REQUIRE(manager.reduce_order(RELIANCE.id, 2, 0) ==
            matching::ErrorCode::ORDER_QUANTITY_INVALID);
    REQUIRE(manager.reduce_order(RELIANCE.id, 2, 21) ==
            matching::ErrorCode::ORDER_QUANTITY_INVALID);
    REQUIRE(manager.reduce_order(TCS.id, 2, 1) ==
            matching::ErrorCode::ORDER_NOT_FOUND);
    REQUIRE(manager.reduce_order(99, 2, 1) ==
            matching::ErrorCode::ORDER_BOOK_NOT_FOUND);
    REQUIRE(manager.reduce_order(RELIANCE.id, 99, 1) ==
            matching::ErrorCode::ORDER_NOT_FOUND);
    REQUIRE(handler.event_count == 0);
    REQUIRE(manager.find_order(2)->quantity == 20);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 18'525)) ==
            std::vector<matching::OrderId>{1, 2, 3});

    REQUIRE(manager.reduce_order(RELIANCE.id, 2, 5) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(2)->quantity == 15);
    const matching::LevelNode* level = bid_level(manager, RELIANCE.id, 18'525);
    REQUIRE(level->total_quantity == 55);
    REQUIRE(level->order_count == 3);
    REQUIRE(fifo_ids(level) == std::vector<matching::OrderId>{1, 2, 3});
    require_event_types(handler,
                        {EventType::UPDATE_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});
    REQUIRE(handler.events[0].quantity == 15);
    REQUIRE(handler.events[1].quantity == 55);
    REQUIRE(handler.callback_state_valid);

    handler.reset();
    REQUIRE(manager.reduce_order(RELIANCE.id, 2, 15) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(2) == nullptr);
    level = bid_level(manager, RELIANCE.id, 18'525);
    REQUIRE(level->total_quantity == 40);
    REQUIRE(level->order_count == 2);
    REQUIRE(fifo_ids(level) == std::vector<matching::OrderId>{1, 3});
    require_event_types(handler,
                        {EventType::DELETE_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});
    REQUIRE(handler.events[0].quantity == 0);

    handler.reset();
    REQUIRE(manager.reduce_order(RELIANCE.id, 1, 10) == matching::ErrorCode::OK);
    REQUIRE(manager.reduce_order(RELIANCE.id, 3, 30) == matching::ErrorCode::OK);
    REQUIRE(bid_level(manager, RELIANCE.id, 18'525) == nullptr);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(handler.events[4].type == EventType::DELETE_LEVEL);
    REQUIRE(handler.events[4].quantity == 0);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Stop and trailing reductions stay outside displayed depth",
          "[order_lifecycle]")
{
    LifecycleHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);

    REQUIRE(manager.add_order(matching::Order::BuyStop(10, RELIANCE.id, 18'600, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::BuyStopLimit(11, RELIANCE.id, 18'600, 18'610, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::TrailingSellStop(12, RELIANCE.id, 18'000, 8, 100)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.reduce_order(RELIANCE.id, 10, 4) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(10)->quantity == 6);
    REQUIRE(fifo_ids(buy_stop_level(manager, RELIANCE.id, 18'600)) ==
            std::vector<matching::OrderId>{10, 11});
    require_event_types(handler, {EventType::UPDATE_ORDER});

    handler.reset();
    REQUIRE(manager.reduce_order(RELIANCE.id, 10, 6) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(10) == nullptr);
    REQUIRE(fifo_ids(buy_stop_level(manager, RELIANCE.id, 18'600)) ==
            std::vector<matching::OrderId>{11});
    require_event_types(handler, {EventType::DELETE_ORDER});

    handler.reset();
    REQUIRE(manager.delete_order(RELIANCE.id, 11) == matching::ErrorCode::OK);
    REQUIRE(manager.reduce_order(RELIANCE.id, 12, 8) == matching::ErrorCode::OK);
    require_event_types(handler, {EventType::DELETE_ORDER, EventType::DELETE_ORDER});
    REQUIRE(manager.find_order_book(RELIANCE.id)->best_buy_stop() == nullptr);
    REQUIRE(manager.find_order_book(RELIANCE.id)->best_trailing_sell_stop() == nullptr);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Modify retains priority only for a same-price quantity decrease",
          "[order_lifecycle]")
{
    LifecycleHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager, true);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, RELIANCE.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, RELIANCE.id, 100, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, RELIANCE.id, 100, 30)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.modify_order(RELIANCE.id, 1, 100, 5) == matching::ErrorCode::OK);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{1, 2, 3});
    REQUIRE(bid_level(manager, RELIANCE.id, 100)->total_quantity == 55);
    require_event_types(handler,
                        {EventType::UPDATE_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});

    handler.reset();
    REQUIRE(manager.modify_order(RELIANCE.id, 1, 100, 5) == matching::ErrorCode::OK);
    REQUIRE(handler.event_count == 0);

    REQUIRE(manager.modify_order(RELIANCE.id, 1, 100, 15) == matching::ErrorCode::OK);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{2, 3, 1});
    REQUIRE(bid_level(manager, RELIANCE.id, 100)->total_quantity == 65);
    require_event_types(handler,
                        {EventType::UPDATE_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});

    handler.reset();
    REQUIRE(manager.modify_order(RELIANCE.id, 2, 101, 20) == matching::ErrorCode::OK);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{3, 1});
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 101)) ==
            std::vector<matching::OrderId>{2});
    REQUIRE(manager.find_order_book(RELIANCE.id)->best_bid()->price == 101);
    require_event_types(handler,
                        {EventType::UPDATE_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK,
                         EventType::ADD_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});

    handler.reset();
    REQUIRE(manager.modify_order(RELIANCE.id, 1, 101, 15) == matching::ErrorCode::OK);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{3});
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 101)) ==
            std::vector<matching::OrderId>{2, 1});
    REQUIRE(bid_level(manager, RELIANCE.id, 101)->total_quantity == 35);
    require_event_types(handler,
                        {EventType::UPDATE_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});

    handler.reset();
    REQUIRE(manager.modify_order(RELIANCE.id, 2, 0, 20) ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(manager.modify_order(TCS.id, 2, 101, 20) ==
            matching::ErrorCode::ORDER_NOT_FOUND);
    REQUIRE(manager.modify_order(RELIANCE.id, 2, 101, 0) ==
            matching::ErrorCode::ORDER_QUANTITY_INVALID);
    REQUIRE(handler.event_count == 0);
    REQUIRE(manager.find_order(2)->price == 101);
    REQUIRE(manager.find_order(2)->quantity == 20);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Stop modifications preserve trigger indexes and apply FIFO priority rules",
          "[order_lifecycle]")
{
    LifecycleHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);

    REQUIRE(manager.add_order(
                matching::Order::BuyStopLimit(1, RELIANCE.id, 110, 112, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::BuyStopLimit(2, RELIANCE.id, 110, 113, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyStop(3, RELIANCE.id, 120, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::TrailingBuyStopLimit(
                4, RELIANCE.id, 130, 131, 8, 5)) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::TrailingBuyStopLimit(
                5, RELIANCE.id, 130, 132, 9, 5)) == matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.modify_order(RELIANCE.id, 1, 114, 10) == matching::ErrorCode::OK);
    REQUIRE(fifo_ids(buy_stop_level(manager, RELIANCE.id, 110)) ==
            std::vector<matching::OrderId>{2, 1});
    REQUIRE(manager.find_order(1)->price == 114);
    require_event_types(handler, {EventType::UPDATE_ORDER});

    handler.reset();
    REQUIRE(manager.modify_order(RELIANCE.id, 2, 113, 25) == matching::ErrorCode::OK);
    REQUIRE(fifo_ids(buy_stop_level(manager, RELIANCE.id, 110)) ==
            std::vector<matching::OrderId>{1, 2});
    REQUIRE(buy_stop_level(manager, RELIANCE.id, 110)->total_quantity == 35);
    require_event_types(handler, {EventType::UPDATE_ORDER});

    handler.reset();
    REQUIRE(manager.modify_order(RELIANCE.id, 1, 114, 6) == matching::ErrorCode::OK);
    REQUIRE(fifo_ids(buy_stop_level(manager, RELIANCE.id, 110)) ==
            std::vector<matching::OrderId>{1, 2});
    REQUIRE(manager.modify_order(RELIANCE.id, 3, 1, 5) ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    require_event_types(handler, {EventType::UPDATE_ORDER});

    handler.reset();
    REQUIRE(manager.modify_order(RELIANCE.id, 4, 133, 8) == matching::ErrorCode::OK);
    REQUIRE(fifo_ids(trailing_buy_stop_level(manager, RELIANCE.id, 130)) ==
            std::vector<matching::OrderId>{5, 4});
    REQUIRE(manager.find_order(4)->price == 133);
    require_event_types(handler, {EventType::UPDATE_ORDER});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Replace changes identity transactionally and always loses priority",
          "[order_lifecycle]")
{
    LifecycleHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager, true);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, RELIANCE.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, RELIANCE.id, 100, 20)) ==
            matching::ErrorCode::OK);
    const matching::Order* original_storage = manager.find_order(1);
    handler.reset();

    REQUIRE(manager.replace_order(RELIANCE.id, 1, 1, 101, 15) ==
            matching::ErrorCode::ORDER_DUPLICATE);
    REQUIRE(manager.replace_order(RELIANCE.id, 1, 2, 101, 15) ==
            matching::ErrorCode::ORDER_DUPLICATE);
    REQUIRE(handler.event_count == 0);
    REQUIRE(manager.find_order(1) == original_storage);
    REQUIRE(manager.find_order(1)->price == 100);

    REQUIRE(manager.replace_order(RELIANCE.id, 1, 10, 100, 15) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(manager.find_order(10) == original_storage);
    REQUIRE(manager.find_order(10)->price == 100);
    REQUIRE(manager.find_order(10)->quantity == 15);
    REQUIRE(manager.active_order_count() == 2);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{2, 10});
    require_event_types(handler,
                        {EventType::DELETE_ORDER,
                         EventType::ADD_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});

    handler.reset();
    REQUIRE(manager.replace_order(RELIANCE.id, 10, 12, 101, 15) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(10) == nullptr);
    REQUIRE(manager.find_order(12) == original_storage);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{2});
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 101)) ==
            std::vector<matching::OrderId>{12});
    require_event_types(handler,
                        {EventType::DELETE_ORDER,
                         EventType::ADD_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK,
                         EventType::ADD_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});

    handler.reset();
    const matching::Order stop_replacement =
        matching::Order::BuyStop(20, RELIANCE.id, 105, 7);
    REQUIRE(manager.replace_order(RELIANCE.id, 2, stop_replacement) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(2) == nullptr);
    REQUIRE(manager.find_order(20) != nullptr);
    REQUIRE(bid_level(manager, RELIANCE.id, 100) == nullptr);
    REQUIRE(fifo_ids(buy_stop_level(manager, RELIANCE.id, 105)) ==
            std::vector<matching::OrderId>{20});
    require_event_types(handler,
                        {EventType::DELETE_ORDER,
                         EventType::ADD_ORDER,
                         EventType::DELETE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});

    handler.reset();
    REQUIRE(manager.replace_order(
                RELIANCE.id,
                12,
                matching::Order::BuyLimit(30, TCS.id, 102, 3)) ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(manager.replace_order(
                RELIANCE.id,
                12,
                matching::Order::BuyMarket(31, RELIANCE.id, 3)) ==
            matching::ErrorCode::ORDER_TYPE_INVALID);
    REQUIRE(manager.replace_order(RELIANCE.id, 20, 21, 106, 4) ==
            matching::ErrorCode::ORDER_TYPE_INVALID);
    REQUIRE(handler.event_count == 0);
    REQUIRE(manager.find_order(12) != nullptr);
    REQUIRE(manager.find_order(20) != nullptr);

    handler.reset();
    const matching::Order trailing_replacement =
        matching::Order::TrailingBuyStop(21, RELIANCE.id, 106, 4, 5);
    REQUIRE(manager.replace_order(RELIANCE.id, 20, trailing_replacement) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(20) == nullptr);
    REQUIRE(manager.find_order(21) != nullptr);
    REQUIRE(manager.find_order_book(RELIANCE.id)->best_buy_stop() == nullptr);
    REQUIRE(fifo_ids(trailing_buy_stop_level(manager, RELIANCE.id, 106)) ==
            std::vector<matching::OrderId>{21});
    require_event_types(handler, {EventType::DELETE_ORDER, EventType::ADD_ORDER});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Aggregate overflow is rejected before lifecycle state changes",
          "[order_lifecycle]")
{
    LifecycleHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);

    constexpr matching::Quantity maximum =
        std::numeric_limits<matching::Quantity>::max();
    REQUIRE(manager.add_order(
                matching::Order::BuyLimit(1, RELIANCE.id, 100, maximum - 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, RELIANCE.id, 100, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(bid_level(manager, RELIANCE.id, 100)->total_quantity == maximum);
    handler.reset();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, RELIANCE.id, 100, 1)) ==
            matching::ErrorCode::ORDER_QUANTITY_INVALID);
    REQUIRE(manager.modify_order(RELIANCE.id, 2, 100, 6) ==
            matching::ErrorCode::ORDER_QUANTITY_INVALID);
    REQUIRE(manager.replace_order(RELIANCE.id, 2, 20, 100, 6) ==
            matching::ErrorCode::ORDER_QUANTITY_INVALID);

    REQUIRE(handler.event_count == 0);
    REQUIRE(manager.active_order_count() == 2);
    REQUIRE(manager.find_order(2)->quantity == 5);
    REQUIRE(manager.find_order(20) == nullptr);
    REQUIRE(bid_level(manager, RELIANCE.id, 100)->total_quantity == maximum);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{1, 2});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Delete unlinks first middle last stop and trailing orders",
          "[order_lifecycle]")
{
    LifecycleHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager, true);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, RELIANCE.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, RELIANCE.id, 100, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, RELIANCE.id, 100, 30)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyStop(4, RELIANCE.id, 110, 40)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::TrailingSellStop(5, RELIANCE.id, 90, 50, 5)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.delete_order(TCS.id, 2) == matching::ErrorCode::ORDER_NOT_FOUND);
    REQUIRE(manager.delete_order(RELIANCE.id, 0) ==
            matching::ErrorCode::ORDER_ID_INVALID);
    REQUIRE(manager.delete_order(RELIANCE.id, 99) ==
            matching::ErrorCode::ORDER_NOT_FOUND);
    REQUIRE(handler.event_count == 0);

    REQUIRE(manager.delete_order(RELIANCE.id, 2) == matching::ErrorCode::OK);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{1, 3});
    require_event_types(handler,
                        {EventType::DELETE_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});
    REQUIRE(handler.events[0].quantity == 20);

    handler.reset();
    REQUIRE(manager.delete_order(RELIANCE.id, 1) == matching::ErrorCode::OK);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{3});
    REQUIRE(manager.delete_order(RELIANCE.id, 3) == matching::ErrorCode::OK);
    REQUIRE(bid_level(manager, RELIANCE.id, 100) == nullptr);
    REQUIRE(handler.events[4].type == EventType::DELETE_LEVEL);

    handler.reset();
    REQUIRE(manager.delete_order(RELIANCE.id, 4) == matching::ErrorCode::OK);
    REQUIRE(manager.delete_order(RELIANCE.id, 5) == matching::ErrorCode::OK);
    require_event_types(handler, {EventType::DELETE_ORDER, EventType::DELETE_ORDER});
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(RELIANCE.id)->empty());
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("External execution reports deltas and preserves partial-fill priority",
          "[order_lifecycle]")
{
    LifecycleHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager, true);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, RELIANCE.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, RELIANCE.id, 100, 20)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.execute_order(RELIANCE.id, 1, 4) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1)->quantity == 6);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{1, 2});
    REQUIRE(bid_level(manager, RELIANCE.id, 100)->total_quantity == 26);
    require_event_types(handler,
                        {EventType::EXECUTE_ORDER,
                         EventType::UPDATE_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});
    REQUIRE(handler.events[0].price == 100);
    REQUIRE(handler.events[0].quantity == 6);
    REQUIRE(handler.events[0].execution_quantity == 4);

    handler.reset();
    REQUIRE(manager.execute_order(RELIANCE.id, 1, 7) ==
            matching::ErrorCode::ORDER_QUANTITY_INVALID);
    REQUIRE(manager.execute_order(TCS.id, 1, 1) ==
            matching::ErrorCode::ORDER_NOT_FOUND);
    REQUIRE(manager.execute_order(RELIANCE.id, 1, 0) ==
            matching::ErrorCode::ORDER_QUANTITY_INVALID);
    REQUIRE(handler.event_count == 0);
    REQUIRE(manager.find_order(1)->quantity == 6);

    REQUIRE(manager.execute_order(RELIANCE.id, 1, 99, 6) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(fifo_ids(bid_level(manager, RELIANCE.id, 100)) ==
            std::vector<matching::OrderId>{2});
    require_event_types(handler,
                        {EventType::EXECUTE_ORDER,
                         EventType::DELETE_ORDER,
                         EventType::UPDATE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});
    REQUIRE(handler.events[0].price == 99);
    REQUIRE(handler.events[0].quantity == 0);
    REQUIRE(handler.events[0].execution_quantity == 6);

    handler.reset();
    REQUIRE(manager.execute_order(RELIANCE.id, 2, 0, 1) ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(manager.execute_order(RELIANCE.id, 2, 20) == matching::ErrorCode::OK);
    REQUIRE(bid_level(manager, RELIANCE.id, 100) == nullptr);
    require_event_types(handler,
                        {EventType::EXECUTE_ORDER,
                         EventType::DELETE_ORDER,
                         EventType::DELETE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("External execution supports dormant orders only with a valid price",
          "[order_lifecycle]")
{
    LifecycleHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);

    REQUIRE(manager.add_order(matching::Order::BuyStop(10, RELIANCE.id, 110, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::BuyStopLimit(11, RELIANCE.id, 120, 121, 6)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.execute_order(RELIANCE.id, 10, 2) ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(handler.event_count == 0);
    REQUIRE(manager.find_order(10)->quantity == 5);

    REQUIRE(manager.execute_order(RELIANCE.id, 10, 111, 2) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(10)->quantity == 3);
    require_event_types(handler, {EventType::EXECUTE_ORDER, EventType::UPDATE_ORDER});

    handler.reset();
    REQUIRE(manager.execute_order(RELIANCE.id, 10, 112, 3) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.execute_order(RELIANCE.id, 11, 6) == matching::ErrorCode::OK);
    require_event_types(handler,
                        {EventType::EXECUTE_ORDER,
                         EventType::DELETE_ORDER,
                         EventType::EXECUTE_ORDER,
                         EventType::DELETE_ORDER});
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(RELIANCE.id)->empty());
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Executing a non-best order leaves the best ask unchanged",
          "[order_lifecycle]")
{
    LifecycleHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager, true);

    REQUIRE(manager.add_order(
                matching::Order::SellLimit(1, RELIANCE.id, 23'000, 15)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::SellLimit(2, RELIANCE.id, 25'000, 25)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order_book(RELIANCE.id)->best_ask()->price == 23'000);
    handler.reset();

    REQUIRE(manager.execute_order(RELIANCE.id, 2, 25) ==
            matching::ErrorCode::OK);

    const matching::OrderBook* book = manager.find_order_book(RELIANCE.id);
    REQUIRE(book->best_ask() != nullptr);
    REQUIRE(book->best_ask()->price == 23'000);
    REQUIRE(book->find_ask(25'000) == nullptr);
    REQUIRE(manager.find_order(1) != nullptr);
    REQUIRE(manager.find_order(1)->quantity == 15);
    REQUIRE(manager.find_order(2) == nullptr);
    REQUIRE(manager.active_order_count() == 1);
    require_event_types(handler,
                        {EventType::EXECUTE_ORDER,
                         EventType::DELETE_ORDER,
                         EventType::DELETE_LEVEL,
                         EventType::UPDATE_ORDER_BOOK});
    REQUIRE_FALSE(handler.events[2].top);
    REQUIRE_FALSE(handler.events[3].top);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("External executions update the market reference used by trailing orders",
          "[order_lifecycle]")
{
    matching::MarketManager manager;
    initialize_market(manager);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, RELIANCE.id, 19'000, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, RELIANCE.id, 18'900, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.execute_order(RELIANCE.id, 2, 18'525, 10) ==
            matching::ErrorCode::OK);

    REQUIRE(manager.add_order(
                matching::Order::TrailingSellStop(3, RELIANCE.id, 18'000, 5, 100)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(3) != nullptr);
    REQUIRE(manager.find_order(3)->stop_price == 18'425);
    REQUIRE(manager.find_order_book(RELIANCE.id)->best_trailing_sell_stop()->price ==
            18'425);

    const matching::Order replacement =
        matching::Order::TrailingSellStop(4, RELIANCE.id, 18'000, 5, 100);
    REQUIRE(manager.replace_order(RELIANCE.id, 1, replacement) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(manager.find_order(4) != nullptr);
    REQUIRE(manager.find_order(4)->stop_price == 18'000);
    REQUIRE(manager.find_order_book(RELIANCE.id)->best_bid() == nullptr);
    REQUIRE(manager.find_order_book(RELIANCE.id)->find_trailing_sell_stop(18'000) !=
            nullptr);
}
