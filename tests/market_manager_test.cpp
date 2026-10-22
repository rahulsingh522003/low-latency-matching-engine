#include "matching/market_manager.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace matching::detail {

class MarketManagerTestAccess
{
public:
    static LevelNode* create_empty_bid_level(MarketManager& manager,
                                             SymbolId symbol_id,
                                             Price price) noexcept
    {
        OrderBook* order_book = manager.find_order_book_mutable(symbol_id);
        return (order_book != nullptr) ? order_book->create_level(LevelType::BID, price)
                                       : nullptr;
    }

    static void delete_empty_level(MarketManager& manager,
                                   SymbolId symbol_id,
                                   LevelNode& level) noexcept
    {
        OrderBook* order_book = manager.find_order_book_mutable(symbol_id);
        assert((order_book != nullptr) && "Test order book must exist");
        order_book->delete_level(level);
    }

    static std::size_t order_pool_live_count(const MarketManager& manager) noexcept
    {
        return manager.order_pool_.live_count();
    }

    static void update_last_price(MarketManager& manager,
                                  SymbolId symbol_id,
                                  const Order& order,
                                  Price price) noexcept
    {
        OrderBook* order_book = manager.find_order_book_mutable(symbol_id);
        assert((order_book != nullptr) && "Test order book must exist");
        order_book->update_last_price(order, price);
    }
};

} // namespace matching::detail

namespace {

enum class EventType
{
    ADD_SYMBOL,
    DELETE_SYMBOL,
    ADD_ORDER_BOOK,
    DELETE_ORDER_BOOK,
    ADD_ORDER,
    ADD_LEVEL,
    UPDATE_LEVEL,
    UPDATE_ORDER_BOOK
};

struct Event
{
    EventType type;
    matching::SymbolId symbol_id;
    matching::OrderId order_id{0};
    matching::Price price{0};
    matching::Quantity quantity{0};
    std::size_t order_count{0};
    bool top{false};

    friend bool operator==(const Event&, const Event&) noexcept = default;
};

class RecordingHandler final : public matching::MarketHandler
{
public:
    matching::MarketManager* manager{nullptr};
    std::array<Event, 64> events{};
    std::size_t event_count{0};
    std::array<matching::Order, 16> added_orders{};
    std::size_t added_order_count{0};
    bool callback_state_valid{true};

    void record(Event event) noexcept
    {
        if (event_count < events.size())
            events[event_count++] = event;
        else
            callback_state_valid = false;
    }

protected:
    void on_add_symbol(const matching::Symbol& symbol) noexcept override
    {
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_symbol(symbol.id) == &symbol);
        record({EventType::ADD_SYMBOL, symbol.id});
    }

    void on_delete_symbol(const matching::Symbol& symbol) noexcept override
    {
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_symbol(symbol.id) == nullptr);
        record({EventType::DELETE_SYMBOL, symbol.id});
    }

    void on_add_order_book(const matching::OrderBook& order_book) noexcept override
    {
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_order_book(order_book.symbol().id) ==
                                &order_book);
        record({EventType::ADD_ORDER_BOOK, order_book.symbol().id});
    }

    void on_delete_order_book(const matching::OrderBook& order_book) noexcept override
    {
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_order_book(order_book.symbol().id) == nullptr);
        record({EventType::DELETE_ORDER_BOOK, order_book.symbol().id});
    }

    void on_add_order(const matching::Order& order) noexcept override
    {
        const matching::OrderBook* order_book =
            (manager != nullptr) ? manager->find_order_book(order.symbol_id) : nullptr;
        const matching::Level* level = nullptr;
        if (order_book != nullptr)
        {
            if (order.is_limit())
            {
                level = order.is_buy() ? order_book->find_bid(order.price)
                                       : order_book->find_ask(order.price);
            }
            else if (order.is_stop() || order.is_stop_limit())
            {
                level = order.is_buy() ? order_book->find_buy_stop(order.stop_price)
                                       : order_book->find_sell_stop(order.stop_price);
            }
            else if (order.is_trailing_stop() || order.is_trailing_stop_limit())
            {
                level = order.is_buy()
                            ? order_book->find_trailing_buy_stop(order.stop_price)
                            : order_book->find_trailing_sell_stop(order.stop_price);
            }
        }

        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_order(order.id) == &order) &&
                               (order_book != nullptr) && (level != nullptr);

        if (added_order_count < added_orders.size())
            added_orders[added_order_count++] = order;
        else
            callback_state_valid = false;

        record({EventType::ADD_ORDER,
                order.symbol_id,
                order.id,
                order.price,
                order.quantity});
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
                top});
    }

private:
    void record_level(EventType type,
                      const matching::OrderBook& order_book,
                      const matching::Level& level,
                      bool top) noexcept
    {
        const matching::Level* stored_level =
            level.is_bid() ? order_book.find_bid(level.price)
                           : order_book.find_ask(level.price);

        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               (manager->find_order_book(order_book.symbol().id) ==
                                &order_book) &&
                               (stored_level != nullptr) &&
                               (stored_level->type == level.type) &&
                               (stored_level->price == level.price) &&
                               (stored_level->total_quantity == level.total_quantity) &&
                               (stored_level->order_count == level.order_count);
        record({type,
                order_book.symbol().id,
                0,
                level.price,
                level.total_quantity,
                level.order_count,
                top});
    }
};

const matching::Symbol RELIANCE(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
const matching::Symbol TCS(2, {'T', 'C', 'S', ' ', ' ', ' ', ' ', ' '});

} // namespace

static_assert(!std::is_copy_constructible_v<matching::MarketManager>);
static_assert(!std::is_copy_assignable_v<matching::MarketManager>);
static_assert(!std::is_move_constructible_v<matching::MarketManager>);
static_assert(!std::is_move_assignable_v<matching::MarketManager>);

TEST_CASE("Market manager starts as an empty multi-instrument owner", "[market_manager]")
{
    matching::MarketManager manager;

    REQUIRE(manager.symbol_count() == 0);
    REQUIRE(manager.order_book_count() == 0);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.active_order_capacity() == 8'192);
    REQUIRE(manager.find_symbol(0) == nullptr);
    REQUIRE(manager.find_symbol(7) == nullptr);
    REQUIRE(manager.find_order_book(7) == nullptr);
    REQUIRE(manager.find_order(0) == nullptr);
    REQUIRE(manager.find_order(42) == nullptr);
}

TEST_CASE("Market manager owns symbols in a sparse dense-ID index", "[market_manager]")
{
    RecordingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;

    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_symbol(TCS) == matching::ErrorCode::OK);
    REQUIRE(manager.symbol_count() == 2);
    REQUIRE(manager.find_symbol(0) == nullptr);
    REQUIRE(manager.find_symbol(TCS.id) != nullptr);
    REQUIRE(*manager.find_symbol(RELIANCE.id) == RELIANCE);

    const matching::Symbol duplicate_id(
        RELIANCE.id, {'D', 'I', 'F', 'F', 'E', 'R', 'N', 'T'});
    REQUIRE(manager.add_symbol(duplicate_id) == matching::ErrorCode::SYMBOL_DUPLICATE);
    REQUIRE(*manager.find_symbol(RELIANCE.id) == RELIANCE);
    REQUIRE(handler.event_count == 2);

    REQUIRE(manager.delete_symbol(99) == matching::ErrorCode::SYMBOL_NOT_FOUND);
    REQUIRE(manager.delete_symbol(TCS.id) == matching::ErrorCode::OK);
    REQUIRE(manager.find_symbol(TCS.id) == nullptr);
    REQUIRE(manager.symbol_count() == 1);
    REQUIRE(handler.callback_state_valid);

    const std::array<Event, 3> expected{{
        {EventType::ADD_SYMBOL, RELIANCE.id},
        {EventType::ADD_SYMBOL, TCS.id},
        {EventType::DELETE_SYMBOL, TCS.id},
    }};
    REQUIRE(handler.event_count == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
        REQUIRE(handler.events[index] == expected[index]);
}

TEST_CASE("Market manager creates books only for registered symbols", "[market_manager]")
{
    RecordingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;

    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::SYMBOL_NOT_FOUND);
    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_symbol(TCS) == matching::ErrorCode::OK);

    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(TCS.id) == matching::ErrorCode::OK);
    REQUIRE(manager.order_book_count() == 2);

    const matching::OrderBook* reliance_book = manager.find_order_book(RELIANCE.id);
    const matching::OrderBook* tcs_book = manager.find_order_book(TCS.id);
    REQUIRE(reliance_book != nullptr);
    REQUIRE(tcs_book != nullptr);
    REQUIRE(reliance_book != tcs_book);
    REQUIRE(reliance_book->symbol() == RELIANCE);
    REQUIRE(tcs_book->symbol() == TCS);
    REQUIRE(reliance_book->empty());
    REQUIRE(tcs_book->empty());

    REQUIRE(manager.add_order_book(RELIANCE.id) ==
            matching::ErrorCode::ORDER_BOOK_DUPLICATE);
    REQUIRE(manager.delete_symbol(RELIANCE.id) == matching::ErrorCode::SYMBOL_IN_USE);
    REQUIRE(manager.find_symbol(RELIANCE.id) != nullptr);
    REQUIRE(manager.find_order_book(RELIANCE.id) == reliance_book);

    REQUIRE(manager.delete_order_book(RELIANCE.id) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order_book(RELIANCE.id) == nullptr);
    REQUIRE(manager.order_book_count() == 1);
    REQUIRE(manager.delete_order_book(RELIANCE.id) ==
            matching::ErrorCode::ORDER_BOOK_NOT_FOUND);
    REQUIRE(manager.delete_symbol(RELIANCE.id) == matching::ErrorCode::OK);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Deleted symbol and book slots can be reused safely", "[market_manager]")
{
    matching::MarketManager manager;

    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);
    REQUIRE(manager.delete_order_book(RELIANCE.id) == matching::ErrorCode::OK);
    REQUIRE(manager.delete_symbol(RELIANCE.id) == matching::ErrorCode::OK);

    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);
    REQUIRE(manager.symbol_count() == 1);
    REQUIRE(manager.order_book_count() == 1);
}

TEST_CASE("Deleting a non-empty order book is rejected without mutation",
          "[market_manager]")
{
    RecordingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;

    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);

    matching::LevelNode* level =
        matching::detail::MarketManagerTestAccess::create_empty_bid_level(
            manager, RELIANCE.id, 18'525);
    REQUIRE(level != nullptr);

    const matching::OrderBook* original_book = manager.find_order_book(RELIANCE.id);
    const std::size_t original_event_count = handler.event_count;

    REQUIRE(manager.delete_order_book(RELIANCE.id) ==
            matching::ErrorCode::ORDER_BOOK_NOT_EMPTY);
    REQUIRE(manager.find_order_book(RELIANCE.id) == original_book);
    REQUIRE(manager.order_book_count() == 1);
    REQUIRE(handler.event_count == original_event_count);

    matching::detail::MarketManagerTestAccess::delete_empty_level(
        manager, RELIANCE.id, *level);
    REQUIRE(manager.delete_order_book(RELIANCE.id) == matching::ErrorCode::OK);
}

TEST_CASE("Manager destruction releases registered empty books and symbols",
          "[market_manager]")
{
    RecordingHandler handler;

    {
        matching::MarketManager manager(handler);
        handler.manager = &manager;

        REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
        REQUIRE(manager.add_symbol(TCS) == matching::ErrorCode::OK);
        REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);
        REQUIRE(manager.add_order_book(TCS.id) == matching::ErrorCode::OK);
        REQUIRE(handler.callback_state_valid);
    }

    handler.manager = nullptr;
    REQUIRE(handler.event_count == 4);
}

TEST_CASE("Passive limit orders create indexed price levels and preserve FIFO",
          "[market_manager][orders]")
{
    matching::MarketManager manager;
    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);

    const matching::Order first =
        matching::Order::BuyLimit(100, RELIANCE.id, 18'525, 10);
    const matching::Order second =
        matching::Order::BuyLimit(101, RELIANCE.id, 18'525, 20);
    const matching::Order better =
        matching::Order::BuyLimit(102, RELIANCE.id, 18'530, 5);
    const matching::Order crossed_ask =
        matching::Order::SellLimit(103, RELIANCE.id, 18'520, 7);

    REQUIRE(manager.add_order(first) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(second) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(better) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(crossed_ask) == matching::ErrorCode::OK);

    REQUIRE(manager.active_order_count() == 4);
    REQUIRE(manager.find_order(first.id) != nullptr);
    REQUIRE(manager.find_order(first.id) != &first);
    REQUIRE(manager.find_order(first.id)->quantity == first.quantity);

    const matching::OrderBook* order_book = manager.find_order_book(RELIANCE.id);
    REQUIRE(order_book != nullptr);
    REQUIRE(order_book->level_count() == 3);

    const matching::Level* fifo_level = order_book->find_bid(18'525);
    REQUIRE(fifo_level != nullptr);
    REQUIRE(fifo_level->total_quantity == 30);
    REQUIRE(fifo_level->order_count == 2);

    const auto* fifo_node = static_cast<const matching::LevelNode*>(fifo_level);
    REQUIRE(fifo_node->orders.front() != nullptr);
    REQUIRE(fifo_node->orders.back() != nullptr);
    REQUIRE(fifo_node->orders.front()->id == first.id);
    REQUIRE(fifo_node->orders.back()->id == second.id);
    REQUIRE(fifo_node->orders.front()->next == fifo_node->orders.back());
    REQUIRE(fifo_node->orders.back()->previous == fifo_node->orders.front());

    REQUIRE(order_book->best_bid() != nullptr);
    REQUIRE(order_book->best_bid()->price == better.price);
    REQUIRE(order_book->best_ask() != nullptr);
    REQUIRE(order_book->best_ask()->price == crossed_ask.price);
    REQUIRE(order_book->best_bid()->price >= order_book->best_ask()->price);
}

TEST_CASE("Rejected passive additions leave every market index unchanged",
          "[market_manager][orders]")
{
    RecordingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;

    const matching::Order zero_quantity =
        matching::Order::BuyLimit(200, RELIANCE.id, 18'525, 0);
    REQUIRE(manager.add_order(zero_quantity) ==
            matching::ErrorCode::ORDER_QUANTITY_INVALID);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(handler.event_count == 0);

    const matching::Order missing_book =
        matching::Order::BuyLimit(201, RELIANCE.id, 18'525, 10);
    REQUIRE(manager.add_order(missing_book) ==
            matching::ErrorCode::ORDER_BOOK_NOT_FOUND);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(handler.event_count == 0);

    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(missing_book) ==
            matching::ErrorCode::ORDER_BOOK_NOT_FOUND);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(handler.event_count == 1);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Active order identifiers are unique across all instruments",
          "[market_manager][orders]")
{
    RecordingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;

    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_symbol(TCS) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(TCS.id) == matching::ErrorCode::OK);

    const matching::Order original =
        matching::Order::BuyLimit(300, RELIANCE.id, 18'525, 10);
    REQUIRE(manager.add_order(original) == matching::ErrorCode::OK);

    const std::size_t event_count = handler.event_count;
    const std::size_t live_order_count =
        matching::detail::MarketManagerTestAccess::order_pool_live_count(manager);
    const matching::Order duplicate =
        matching::Order::SellLimit(original.id, TCS.id, 3'500, 99);

    REQUIRE(manager.add_order(duplicate) == matching::ErrorCode::ORDER_DUPLICATE);
    REQUIRE(manager.active_order_count() == 1);
    REQUIRE(manager.find_order(original.id) != nullptr);
    REQUIRE(manager.find_order(original.id)->symbol_id == RELIANCE.id);
    REQUIRE(manager.find_order(original.id)->side == matching::OrderSide::BUY);
    REQUIRE(manager.find_order(original.id)->price == original.price);
    REQUIRE(manager.find_order(original.id)->quantity == original.quantity);
    REQUIRE(manager.find_order_book(TCS.id)->empty());
    REQUIRE(handler.event_count == event_count);
    REQUIRE(matching::detail::MarketManagerTestAccess::order_pool_live_count(manager) ==
            live_order_count);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Passive addition callbacks expose completed state in a fixed order",
          "[market_manager][orders][market_handler]")
{
    RecordingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;

    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);

    REQUIRE(manager.add_order(
                matching::Order::BuyLimit(400, RELIANCE.id, 18'525, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::BuyLimit(401, RELIANCE.id, 18'525, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::BuyLimit(402, RELIANCE.id, 18'500, 5)) ==
            matching::ErrorCode::OK);

    REQUIRE(handler.event_count == 11);
    REQUIRE(handler.events[2] ==
            Event{EventType::ADD_ORDER, RELIANCE.id, 400, 18'525, 10, 0, false});
    REQUIRE(handler.events[3] ==
            Event{EventType::ADD_LEVEL, RELIANCE.id, 0, 18'525, 10, 1, true});
    REQUIRE(handler.events[4] ==
            Event{EventType::UPDATE_ORDER_BOOK, RELIANCE.id, 0, 0, 0, 0, true});

    REQUIRE(handler.events[5] ==
            Event{EventType::ADD_ORDER, RELIANCE.id, 401, 18'525, 20, 0, false});
    REQUIRE(handler.events[6] ==
            Event{EventType::UPDATE_LEVEL, RELIANCE.id, 0, 18'525, 30, 2, true});
    REQUIRE(handler.events[7] ==
            Event{EventType::UPDATE_ORDER_BOOK, RELIANCE.id, 0, 0, 0, 0, true});

    REQUIRE(handler.events[8] ==
            Event{EventType::ADD_ORDER, RELIANCE.id, 402, 18'500, 5, 0, false});
    REQUIRE(handler.events[9] ==
            Event{EventType::ADD_LEVEL, RELIANCE.id, 0, 18'500, 5, 1, false});
    REQUIRE(handler.events[10] ==
            Event{EventType::UPDATE_ORDER_BOOK, RELIANCE.id, 0, 0, 0, 0, false});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Manager destruction quietly drains passive orders before books",
          "[market_manager][orders]")
{
    RecordingHandler handler;
    std::size_t event_count_before_destruction = 0;

    {
        matching::MarketManager manager(handler);
        handler.manager = &manager;

        REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
        REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);
        REQUIRE(manager.add_order(
                    matching::Order::BuyLimit(500, RELIANCE.id, 18'525, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(
                    matching::Order::SellLimit(501, RELIANCE.id, 18'550, 20)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(
                    matching::Order::BuyStop(502, RELIANCE.id, 18'600, 30)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::TrailingSellStop(
                    503, RELIANCE.id, 18'400, 40, 100)) == matching::ErrorCode::OK);

        event_count_before_destruction = handler.event_count;
        REQUIRE(manager.active_order_count() == 4);
        REQUIRE(handler.callback_state_valid);
    }

    handler.manager = nullptr;
    REQUIRE(handler.event_count == event_count_before_destruction);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Dormant stop orders use trigger-index FIFO without changing displayed depth",
          "[market_manager][orders][stop]")
{
    RecordingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;

    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);

    const matching::Order buy_first = matching::Order::BuyStop(
        600, RELIANCE.id, 18'600, 10, matching::OrderTimeInForce::IOC);
    const matching::Order buy_second = matching::Order::BuyStopLimit(
        601,
        RELIANCE.id,
        18'600,
        18'610,
        20,
        matching::OrderTimeInForce::FOK);
    const matching::Order buy_later =
        matching::Order::BuyStop(602, RELIANCE.id, 18'700, 30);
    const matching::Order sell_later =
        matching::Order::SellStop(603, RELIANCE.id, 18'400, 40);
    const matching::Order sell_first = matching::Order::SellStopLimit(
        604, RELIANCE.id, 18'500, 18'490, 50);

    REQUIRE(manager.add_order(buy_first) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(buy_second) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(buy_later) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(sell_later) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(sell_first) == matching::ErrorCode::OK);

    const matching::OrderBook* order_book = manager.find_order_book(RELIANCE.id);
    REQUIRE(order_book != nullptr);
    REQUIRE(manager.active_order_count() == 5);
    REQUIRE(order_book->bids().empty());
    REQUIRE(order_book->asks().empty());
    REQUIRE(order_book->best_bid() == nullptr);
    REQUIRE(order_book->best_ask() == nullptr);
    REQUIRE(order_book->level_count() == 4);

    const matching::Level* shared_buy_level = order_book->find_buy_stop(18'600);
    REQUIRE(shared_buy_level != nullptr);
    REQUIRE(shared_buy_level->is_ask());
    REQUIRE(shared_buy_level->total_quantity == 30);
    REQUIRE(shared_buy_level->order_count == 2);

    const auto* shared_buy_node =
        static_cast<const matching::LevelNode*>(shared_buy_level);
    REQUIRE(shared_buy_node->orders.front() != nullptr);
    REQUIRE(shared_buy_node->orders.back() != nullptr);
    REQUIRE(shared_buy_node->orders.front()->id == buy_first.id);
    REQUIRE(shared_buy_node->orders.back()->id == buy_second.id);
    REQUIRE(shared_buy_node->orders.front()->next == shared_buy_node->orders.back());
    REQUIRE(shared_buy_node->orders.back()->previous ==
            shared_buy_node->orders.front());

    REQUIRE(order_book->best_buy_stop() != nullptr);
    REQUIRE(order_book->best_buy_stop()->price == buy_first.stop_price);
    REQUIRE(order_book->best_sell_stop() != nullptr);
    REQUIRE(order_book->best_sell_stop()->price == sell_first.stop_price);
    REQUIRE(manager.find_order(buy_second.id)->time_in_force ==
            matching::OrderTimeInForce::FOK);
    REQUIRE(manager.find_order(sell_first.id)->price == sell_first.price);

    REQUIRE(handler.event_count == 7);
    for (std::size_t index = 2; index < handler.event_count; ++index)
        REQUIRE(handler.events[index].type == EventType::ADD_ORDER);
    REQUIRE(handler.added_order_count == 5);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Trailing orders store calculated triggers without changing the command value",
          "[market_manager][orders][trailing_stop]")
{
    RecordingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;

    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);

    const matching::Order ask =
        matching::Order::SellLimit(700, RELIANCE.id, 18'550, 10);
    const matching::Order bid =
        matching::Order::BuyLimit(701, RELIANCE.id, 18'500, 10);
    REQUIRE(manager.add_order(ask) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(bid) == matching::ErrorCode::OK);

    matching::detail::MarketManagerTestAccess::update_last_price(
        manager, RELIANCE.id, ask, 18'540);
    matching::detail::MarketManagerTestAccess::update_last_price(
        manager, RELIANCE.id, bid, 18'520);

    const matching::Order trailing_buy = matching::Order::TrailingBuyStopLimit(
        702, RELIANCE.id, 19'000, 19'025, 7, 100);
    const matching::Order trailing_sell = matching::Order::TrailingSellStop(
        703, RELIANCE.id, 18'000, 9, 100);

    REQUIRE(manager.add_order(trailing_buy) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(trailing_sell) == matching::ErrorCode::OK);

    REQUIRE(trailing_buy.stop_price == 19'000);
    REQUIRE(trailing_buy.price == 19'025);
    REQUIRE(trailing_sell.stop_price == 18'000);

    const matching::Order* stored_buy = manager.find_order(trailing_buy.id);
    const matching::Order* stored_sell = manager.find_order(trailing_sell.id);
    REQUIRE(stored_buy != nullptr);
    REQUIRE(stored_sell != nullptr);
    REQUIRE(stored_buy->stop_price == 18'650);
    REQUIRE(stored_buy->price == 18'675);
    REQUIRE(stored_sell->stop_price == 18'400);
    REQUIRE(stored_sell->price == 0);

    const matching::OrderBook* order_book = manager.find_order_book(RELIANCE.id);
    REQUIRE(order_book != nullptr);
    REQUIRE(order_book->find_trailing_buy_stop(stored_buy->stop_price) != nullptr);
    REQUIRE(order_book->find_trailing_sell_stop(stored_sell->stop_price) != nullptr);
    REQUIRE(order_book->find_trailing_buy_stop(trailing_buy.stop_price) == nullptr);
    REQUIRE(order_book->best_trailing_buy_stop()->price == stored_buy->stop_price);
    REQUIRE(order_book->best_trailing_sell_stop()->price == stored_sell->stop_price);

    REQUIRE(handler.event_count == 10);
    REQUIRE(handler.events[8].type == EventType::ADD_ORDER);
    REQUIRE(handler.events[9].type == EventType::ADD_ORDER);
    REQUIRE(handler.added_order_count == 4);
    REQUIRE(handler.added_orders[2].stop_price == stored_buy->stop_price);
    REQUIRE(handler.added_orders[2].price == stored_buy->price);
    REQUIRE(handler.added_orders[3].stop_price == stored_sell->stop_price);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Trailing orders retain their supplied trigger without a market reference",
          "[market_manager][orders][trailing_stop]")
{
    matching::MarketManager manager;
    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);

    const matching::Order order =
        matching::Order::TrailingBuyStop(750, RELIANCE.id, 19'000, 10, 100);
    REQUIRE(manager.add_order(order) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(order.id) != nullptr);
    REQUIRE(manager.find_order(order.id)->stop_price == order.stop_price);
    REQUIRE(manager.find_order_book(RELIANCE.id)
                ->find_trailing_buy_stop(order.stop_price) != nullptr);
}

TEST_CASE("Invalid trailing stop-limit adjustment is rejected atomically",
          "[market_manager][orders][trailing_stop]")
{
    RecordingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;

    REQUIRE(manager.add_symbol(RELIANCE) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(RELIANCE.id) == matching::ErrorCode::OK);

    constexpr matching::Price maximum_price =
        std::numeric_limits<matching::Price>::max();
    const matching::Order bid =
        matching::Order::BuyLimit(800, RELIANCE.id, maximum_price, 1);
    REQUIRE(manager.add_order(bid) == matching::ErrorCode::OK);
    matching::detail::MarketManagerTestAccess::update_last_price(
        manager, RELIANCE.id, bid, maximum_price);

    const std::size_t event_count = handler.event_count;
    const std::size_t live_order_count =
        matching::detail::MarketManagerTestAccess::order_pool_live_count(manager);
    const matching::Order overflow = matching::Order::TrailingSellStopLimit(
        801,
        RELIANCE.id,
        maximum_price - 100,
        maximum_price - 50,
        1,
        25);

    REQUIRE(overflow.validate() == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(overflow) ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(manager.find_order(overflow.id) == nullptr);
    REQUIRE(manager.active_order_count() == 1);
    REQUIRE(manager.find_order_book(RELIANCE.id)->trailing_sell_stops().empty());
    REQUIRE(handler.event_count == event_count);
    REQUIRE(matching::detail::MarketManagerTestAccess::order_pool_live_count(manager) ==
            live_order_count);
    REQUIRE(handler.callback_state_valid);
}
