#include "matching/manager/market_manager.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <cstddef>
#include <limits>
#include <utility>
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

class MatchingHandler final : public matching::EventHandler
{
public:
    matching::MarketManager* manager{nullptr};
    std::array<Event, 2'048> events{};
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
        const matching::Order* active =
            (manager != nullptr) ? manager->find_order(order.id) : nullptr;
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               ((active == nullptr) || (active == &order));
        record({EventType::ADD_ORDER,
                order.symbol_id,
                order.id,
                order.price,
                order.quantity});
    }

    void on_update_order(const matching::Order& order) noexcept override
    {
        const matching::Order* active =
            (manager != nullptr) ? manager->find_order(order.id) : nullptr;
        callback_state_valid = callback_state_valid && (manager != nullptr) &&
                               ((active == nullptr) || (active == &order)) &&
                               (order.quantity > 0);
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
        const matching::Order* active =
            (manager != nullptr) ? manager->find_order(order.id) : nullptr;
        callback_state_valid = callback_state_valid && (execution_quantity > 0) &&
                               ((active == nullptr) ||
                                ((active == &order) && (order.quantity > 0)));
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
        const matching::Level* stored =
            level.is_bid() ? order_book.find_bid(level.price)
                           : order_book.find_ask(level.price);
        callback_state_valid = callback_state_valid && (stored == nullptr);
        record({EventType::DELETE_LEVEL,
                order_book.symbol().id,
                0,
                level.price,
                level.total_quantity,
                0,
                level.order_count,
                top});
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
        const matching::Level* stored =
            level.is_bid() ? order_book.find_bid(level.price)
                           : order_book.find_ask(level.price);
        callback_state_valid = callback_state_valid && (stored != nullptr) &&
                               (stored->total_quantity == level.total_quantity) &&
                               (stored->displayed_quantity ==
                                level.displayed_quantity) &&
                               (stored->order_count == level.order_count);
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

const matching::Symbol ALPHA(7, {'A', 'L', 'P', 'H', 'A', ' ', ' ', ' '});
const matching::Symbol BETA(2, {'B', 'E', 'T', 'A', ' ', ' ', ' ', ' '});

void initialize_market(matching::MarketManager& manager, bool second_symbol = false)
{
    REQUIRE(manager.add_symbol(ALPHA) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order_book(ALPHA.id) == matching::ErrorCode::OK);
    if (second_symbol)
    {
        REQUIRE(manager.add_symbol(BETA) == matching::ErrorCode::OK);
        REQUIRE(manager.add_order_book(BETA.id) == matching::ErrorCode::OK);
    }
}

[[nodiscard]] std::pair<std::size_t, std::size_t> book_order_counts(
    const matching::OrderBook& order_book)
{
    std::size_t bids = 0;
    for (const matching::LevelNode& level : order_book.bids())
        bids += level.order_count;

    std::size_t asks = 0;
    for (const matching::LevelNode& level : order_book.asks())
        asks += level.order_count;
    return {bids, asks};
}

[[nodiscard]] std::pair<matching::Quantity, matching::Quantity> book_quantities(
    const matching::OrderBook& order_book)
{
    matching::Quantity bids = 0;
    for (const matching::LevelNode& level : order_book.bids())
        bids += level.total_quantity;

    matching::Quantity asks = 0;
    for (const matching::LevelNode& level : order_book.asks())
        asks += level.total_quantity;
    return {bids, asks};
}

[[nodiscard]] std::pair<matching::Quantity, matching::Quantity>
book_displayed_quantities(const matching::OrderBook& order_book)
{
    matching::Quantity bids = 0;
    for (const matching::LevelNode& level : order_book.bids())
        bids += level.displayed_quantity;

    matching::Quantity asks = 0;
    for (const matching::LevelNode& level : order_book.asks())
        asks += level.displayed_quantity;
    return {bids, asks};
}

[[nodiscard]] std::vector<Event> executions(const MatchingHandler& handler)
{
    std::vector<Event> result;
    for (std::size_t index = 0; index < handler.event_count; ++index)
    {
        if (handler.events[index].type == EventType::EXECUTE_ORDER)
            result.push_back(handler.events[index]);
    }
    return result;
}

[[nodiscard]] std::vector<EventType> order_event_types(
    const MatchingHandler& handler,
    matching::OrderId order_id)
{
    std::vector<EventType> result;
    for (std::size_t index = 0; index < handler.event_count; ++index)
    {
        const Event& event = handler.events[index];
        if ((event.order_id == order_id) &&
            ((event.type == EventType::ADD_ORDER) ||
             (event.type == EventType::UPDATE_ORDER) ||
             (event.type == EventType::DELETE_ORDER) ||
             (event.type == EventType::EXECUTE_ORDER)))
        {
            result.push_back(event.type);
        }
    }
    return result;
}

[[nodiscard]] std::vector<matching::OrderId> fifo_ids(const matching::Level* level)
{
    std::vector<matching::OrderId> result;
    if (level == nullptr)
        return result;

    const auto* node = static_cast<const matching::LevelNode*>(level);
    for (const matching::OrderNode* order = node->orders.front(); order != nullptr;
         order = order->next)
    {
        result.push_back(order->id);
    }
    return result;
}

} // namespace

TEST_CASE("An incoming limit uses passive price and publishes completed state",
          "[matching]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, ALPHA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(ALPHA.id)->best_bid() == nullptr);
    REQUIRE(manager.find_order_book(ALPHA.id)->best_ask() == nullptr);

    const std::array<EventType, 7> expected{
        EventType::ADD_ORDER,
        EventType::EXECUTE_ORDER,
        EventType::DELETE_ORDER,
        EventType::DELETE_LEVEL,
        EventType::UPDATE_ORDER_BOOK,
        EventType::EXECUTE_ORDER,
        EventType::DELETE_ORDER};
    REQUIRE(handler.event_count == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
        REQUIRE(handler.events[index].type == expected[index]);

    REQUIRE(handler.events[1].order_id == 1);
    REQUIRE(handler.events[1].price == 100);
    REQUIRE(handler.events[1].execution_quantity == 10);
    REQUIRE(handler.events[5].order_id == 2);
    REQUIRE(handler.events[5].price == 100);
    REQUIRE(handler.events[5].execution_quantity == 10);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Matching consumes FIFO before sweeping worse price levels", "[matching]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 100, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(3, ALPHA.id, 105, 30)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(4, ALPHA.id, 110, 40)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(5, ALPHA.id, 105, 35)) ==
            matching::ErrorCode::OK);

    const std::vector<Event> fills = executions(handler);
    REQUIRE(fills.size() == 6);
    REQUIRE(std::vector<matching::OrderId>{fills[0].order_id,
                                            fills[1].order_id,
                                            fills[2].order_id,
                                            fills[3].order_id,
                                            fills[4].order_id,
                                            fills[5].order_id} ==
            std::vector<matching::OrderId>{1, 5, 2, 5, 3, 5});
    REQUIRE(std::vector<matching::Price>{fills[0].price,
                                         fills[1].price,
                                         fills[2].price,
                                         fills[3].price,
                                         fills[4].price,
                                         fills[5].price} ==
            std::vector<matching::Price>{100, 100, 100, 100, 105, 105});
    REQUIRE(fills[0].execution_quantity == 10);
    REQUIRE(fills[2].execution_quantity == 20);
    REQUIRE(fills[4].execution_quantity == 5);

    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(manager.find_order(2) == nullptr);
    REQUIRE(manager.find_order(3)->quantity == 25);
    REQUIRE(manager.find_order(4)->quantity == 40);
    REQUIRE(manager.find_order(5) == nullptr);
    const matching::OrderBook* book = manager.find_order_book(ALPHA.id);
    REQUIRE(book->best_ask()->price == 105);
    REQUIRE(book->find_ask(100) == nullptr);
    REQUIRE(book_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{0, 65});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Buy and sell aggressors have symmetric residual behavior", "[matching]")
{
    SECTION("A buy residual rests only after matching")
    {
        matching::MarketManager manager;
        initialize_market(manager);
        manager.enable_matching();

        REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::BuyLimit(2, ALPHA.id, 100, 15)) ==
                matching::ErrorCode::OK);

        REQUIRE(manager.find_order(1) == nullptr);
        REQUIRE(manager.find_order(2)->quantity == 5);
        REQUIRE(manager.find_order_book(ALPHA.id)->best_ask() == nullptr);
        REQUIRE(manager.find_order_book(ALPHA.id)->best_bid()->price == 100);
    }

    SECTION("A sell aggressor executes best bids first")
    {
        MatchingHandler handler;
        matching::MarketManager manager(handler);
        handler.manager = &manager;
        initialize_market(manager);
        manager.enable_matching();

        REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 100, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::BuyLimit(2, ALPHA.id, 95, 20)) ==
                matching::ErrorCode::OK);
        handler.reset();
        REQUIRE(manager.add_order(matching::Order::SellLimit(3, ALPHA.id, 95, 15)) ==
                matching::ErrorCode::OK);

        const std::vector<Event> fills = executions(handler);
        REQUIRE(fills.size() == 4);
        REQUIRE(fills[0].order_id == 1);
        REQUIRE(fills[0].price == 100);
        REQUIRE(fills[2].order_id == 2);
        REQUIRE(fills[2].price == 95);
        REQUIRE(manager.find_order(2)->quantity == 15);
        REQUIRE(manager.find_order(3) == nullptr);
        REQUIRE(handler.callback_state_valid);
    }
}

TEST_CASE("Market orders honor the arrival best and slippage bound", "[matching]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    for (matching::OrderId id = 1; id <= 3; ++id)
    {
        REQUIRE(manager.add_order(
                    matching::Order::BuyLimit(id, ALPHA.id, 10, id * 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(
                    matching::Order::BuyLimit(id + 3, ALPHA.id, 20, id * 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(
                    matching::Order::BuyLimit(id + 6, ALPHA.id, 30, id * 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(
                    matching::Order::SellLimit(id + 9, ALPHA.id, 40, (4 - id) * 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(
                    matching::Order::SellLimit(id + 12, ALPHA.id, 50, (4 - id) * 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(
                    matching::Order::SellLimit(id + 15, ALPHA.id, 60, (4 - id) * 10)) ==
                matching::ErrorCode::OK);
    }

    const matching::OrderBook* book = manager.find_order_book(ALPHA.id);
    REQUIRE(book_order_counts(*book) == std::pair<std::size_t, std::size_t>{9, 9});
    REQUIRE(book_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{180, 180});

    REQUIRE(manager.add_order(matching::Order::SellMarket(19, ALPHA.id, 15)) ==
            matching::ErrorCode::OK);
    REQUIRE(book_order_counts(*book) == std::pair<std::size_t, std::size_t>{8, 9});
    REQUIRE(book_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{165, 180});

    REQUIRE(manager.add_order(matching::Order::SellMarket(
                20, ALPHA.id, 100, matching::OrderTimeInForce::IOC, 0)) ==
            matching::ErrorCode::OK);
    REQUIRE(book_order_counts(*book) == std::pair<std::size_t, std::size_t>{6, 9});
    REQUIRE(book_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{120, 180});

    REQUIRE(manager.add_order(matching::Order::BuyMarket(
                21, ALPHA.id, 160, matching::OrderTimeInForce::IOC, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(book_order_counts(*book) == std::pair<std::size_t, std::size_t>{6, 2});
    REQUIRE(book_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{120, 20});

    REQUIRE(manager.add_order(matching::Order::SellMarket(22, ALPHA.id, 1'000)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyMarket(23, ALPHA.id, 1'000)) ==
            matching::ErrorCode::OK);
    REQUIRE(book_order_counts(*book) == std::pair<std::size_t, std::size_t>{0, 0});
    REQUIRE(book_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{0, 0});
    REQUIRE(manager.active_order_count() == 0);
}

TEST_CASE("Empty and bounded market residuals are cancelled", "[matching]")
{
    SECTION("An empty market has only accepted and cancelled lifecycle events")
    {
        MatchingHandler handler;
        matching::MarketManager manager(handler);
        handler.manager = &manager;
        initialize_market(manager);
        manager.enable_matching();
        handler.reset();

        REQUIRE(manager.add_order(matching::Order::BuyMarket(1, ALPHA.id, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.active_order_count() == 0);
        REQUIRE(handler.event_count == 2);
        REQUIRE(handler.events[0].type == EventType::ADD_ORDER);
        REQUIRE(handler.events[1].type == EventType::DELETE_ORDER);
        REQUIRE(handler.events[1].quantity == 10);
        REQUIRE(handler.callback_state_valid);
    }

    SECTION("A saturated buy bound cannot overflow")
    {
        matching::MarketManager manager;
        initialize_market(manager);
        manager.enable_matching();
        constexpr matching::Price high =
            std::numeric_limits<matching::Price>::max() - 2;
        REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, high, 7)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::BuyMarket(
                    2, ALPHA.id, 7, matching::OrderTimeInForce::IOC, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.active_order_count() == 0);
    }

    SECTION("A saturated sell bound cannot underflow")
    {
        matching::MarketManager manager;
        initialize_market(manager);
        manager.enable_matching();
        REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 2, 7)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::SellMarket(
                    2, ALPHA.id, 7, matching::OrderTimeInForce::IOC, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.active_order_count() == 0);
    }
}

TEST_CASE("IOC executes eligible liquidity and never rests its residual", "[matching]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 110, 20)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                3, ALPHA.id, 105, 50, matching::OrderTimeInForce::IOC)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(manager.find_order(2)->quantity == 20);
    REQUIRE(manager.find_order(3) == nullptr);
    REQUIRE(manager.find_order_book(ALPHA.id)->best_bid() == nullptr);
    REQUIRE(manager.find_order_book(ALPHA.id)->best_ask()->price == 110);

    const std::vector<Event> fills = executions(handler);
    REQUIRE(fills.size() == 2);
    REQUIRE(fills[0].order_id == 1);
    REQUIRE(fills[1].order_id == 3);
    REQUIRE(fills[1].quantity == 40);
    REQUIRE(handler.events[handler.event_count - 1].type == EventType::DELETE_ORDER);
    REQUIRE(handler.events[handler.event_count - 1].order_id == 3);
    REQUIRE(handler.events[handler.event_count - 1].quantity == 40);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("FOK pre-check either executes the full order or changes no depth", "[matching]")
{
    SECTION("Complete eligible liquidity executes across levels")
    {
        matching::MarketManager manager;
        initialize_market(manager);
        manager.enable_matching();
        REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 105, 20)) ==
                matching::ErrorCode::OK);

        REQUIRE(manager.add_order(matching::Order::BuyLimit(
                    3, ALPHA.id, 105, 25, matching::OrderTimeInForce::FOK)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.find_order(1) == nullptr);
        REQUIRE(manager.find_order(2)->quantity == 5);
        REQUIRE(manager.find_order(3) == nullptr);
    }

    SECTION("Insufficient quantity emits no partial execution or depth event")
    {
        MatchingHandler handler;
        matching::MarketManager manager(handler);
        handler.manager = &manager;
        initialize_market(manager);
        manager.enable_matching();
        REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 105, 20)) ==
                matching::ErrorCode::OK);
        handler.reset();

        REQUIRE(manager.add_order(matching::Order::BuyLimit(
                    3, ALPHA.id, 105, 31, matching::OrderTimeInForce::FOK)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.find_order(1)->quantity == 10);
        REQUIRE(manager.find_order(2)->quantity == 20);
        REQUIRE(manager.find_order(3) == nullptr);
        REQUIRE(handler.event_count == 2);
        REQUIRE(handler.events[0].type == EventType::ADD_ORDER);
        REQUIRE(handler.events[1].type == EventType::DELETE_ORDER);
        REQUIRE(executions(handler).empty());
        REQUIRE(handler.callback_state_valid);
    }

    SECTION("Liquidity outside the limit does not satisfy FOK")
    {
        matching::MarketManager manager;
        initialize_market(manager);
        manager.enable_matching();
        REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 110, 20)) ==
                matching::ErrorCode::OK);

        REQUIRE(manager.add_order(matching::Order::BuyLimit(
                    3, ALPHA.id, 105, 20, matching::OrderTimeInForce::FOK)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.find_order(1)->quantity == 10);
        REQUIRE(manager.find_order(2)->quantity == 20);
        REQUIRE(manager.find_order(3) == nullptr);
    }

    SECTION("Market FOK uses the same slippage bound during pre-check")
    {
        matching::MarketManager manager;
        initialize_market(manager);
        manager.enable_matching();
        REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 110, 20)) ==
                matching::ErrorCode::OK);

        REQUIRE(manager.add_order(matching::Order::BuyMarket(
                    3, ALPHA.id, 20, matching::OrderTimeInForce::FOK, 5)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.find_order(1)->quantity == 10);
        REQUIRE(manager.find_order(2)->quantity == 20);
        REQUIRE(manager.find_order(3) == nullptr);
    }
}

TEST_CASE("Duplicate aggressor IDs are rejected before any execution", "[matching]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();
    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 110, 10)) ==
            matching::ErrorCode::ORDER_DUPLICATE);
    REQUIRE(handler.event_count == 0);
    REQUIRE(manager.find_order(1)->is_sell());
    REQUIRE(manager.find_order(1)->quantity == 10);
    REQUIRE(manager.find_order_book(ALPHA.id)->best_ask()->price == 100);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Manual and automatic controls deterministically clear stored crosses",
          "[matching]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager, true);

    REQUIRE_FALSE(manager.is_matching_enabled());
    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 110, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 100, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, BETA.id, 210, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(4, BETA.id, 200, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 4);
    handler.reset();

    manager.match();
    REQUIRE_FALSE(manager.is_matching_enabled());
    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(manager.find_order(2)->quantity == 10);
    REQUIRE(manager.find_order(3)->quantity == 10);
    REQUIRE(manager.find_order(4) == nullptr);

    const std::vector<Event> fills = executions(handler);
    REQUIRE(fills.size() == 4);
    REQUIRE(fills[0].order_id == 4);
    REQUIRE(fills[0].price == 200);
    REQUIRE(fills[1].order_id == 3);
    REQUIRE(fills[1].price == 200);
    REQUIRE(fills[2].order_id == 2);
    REQUIRE(fills[2].price == 100);
    REQUIRE(fills[3].order_id == 1);
    REQUIRE(fills[3].price == 100);

    manager.enable_matching();
    REQUIRE(manager.is_matching_enabled());
    manager.disable_matching();
    REQUIRE_FALSE(manager.is_matching_enabled());

    REQUIRE(manager.add_order(matching::Order::BuyLimit(5, ALPHA.id, 105, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(5) != nullptr);
    REQUIRE(manager.find_order(2) != nullptr);
    manager.enable_matching();
    REQUIRE(manager.find_order(5) == nullptr);
    REQUIRE(manager.find_order(2)->quantity == 5);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Explicit matching activates dormant stops without enabling automatic mode",
          "[matching][stop]")
{
    matching::MarketManager manager;
    initialize_market(manager);

    REQUIRE_FALSE(manager.is_matching_enabled());
    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellStop(2, ALPHA.id, 110, 10)) ==
            matching::ErrorCode::OK);

    const matching::OrderBook* book = manager.find_order_book(ALPHA.id);
    REQUIRE(manager.active_order_count() == 2);
    REQUIRE(book->best_sell_stop() != nullptr);

    manager.match();

    REQUIRE_FALSE(manager.is_matching_enabled());
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(book->empty());
}

TEST_CASE("Automatic matching follows price-changing modify and replace", "[matching]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 100, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 120, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.modify_order(ALPHA.id, 2, 90, 10) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1)->quantity == 10);
    REQUIRE(manager.find_order(2) == nullptr);

    REQUIRE(manager.add_order(matching::Order::SellLimit(3, ALPHA.id, 110, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.replace_order(ALPHA.id, 1, 4, 120, 10) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(manager.find_order(3) == nullptr);
    REQUIRE(manager.find_order(4) == nullptr);
    REQUIRE(manager.active_order_count() == 0);
}

TEST_CASE("Orders never match across instrument boundaries", "[matching]")
{
    matching::MarketManager manager;
    initialize_market(manager, true);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 90, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, BETA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 2);
    REQUIRE(manager.find_order(1) != nullptr);
    REQUIRE(manager.find_order(2) != nullptr);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, ALPHA.id, 90, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(manager.find_order(3) == nullptr);
    REQUIRE(manager.find_order(2) != nullptr);

    REQUIRE(manager.add_order(matching::Order::SellLimit(4, BETA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 0);
}

TEST_CASE("Non-crossing limits preserve FIFO while matching is enabled", "[matching]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 90, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, ALPHA.id, 90, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(3, ALPHA.id, 100, 30)) ==
            matching::ErrorCode::OK);

    const matching::OrderBook* book = manager.find_order_book(ALPHA.id);
    REQUIRE(book->best_bid()->price == 90);
    REQUIRE(book->best_ask()->price == 100);
    REQUIRE(fifo_ids(book->find_bid(90)) == std::vector<matching::OrderId>{1, 2});
    REQUIRE(manager.active_order_count() == 3);
}

TEST_CASE("An incoming all-or-none order fills completely across price levels",
          "[matching]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                1, ALPHA.id, 20, 30, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, ALPHA.id, 20, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                3, ALPHA.id, 30, 30, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(4, ALPHA.id, 30, 10)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.add_order(matching::Order::SellLimit(
                5, ALPHA.id, 20, 80, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(ALPHA.id)->empty());

    const std::vector<Event> fills = executions(handler);
    REQUIRE(fills.size() == 8);
    REQUIRE(std::vector<matching::OrderId>{fills[0].order_id,
                                            fills[1].order_id,
                                            fills[2].order_id,
                                            fills[3].order_id,
                                            fills[4].order_id,
                                            fills[5].order_id,
                                            fills[6].order_id,
                                            fills[7].order_id} ==
            std::vector<matching::OrderId>{3, 5, 4, 5, 1, 5, 2, 5});
    REQUIRE(std::vector<matching::Price>{fills[0].price,
                                         fills[1].price,
                                         fills[2].price,
                                         fills[3].price,
                                         fills[4].price,
                                         fills[5].price,
                                         fills[6].price,
                                         fills[7].price} ==
            std::vector<matching::Price>{30, 30, 30, 30, 20, 20, 20, 20});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("An unfilled all-or-none order rests without partial mutation",
          "[matching]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                1, ALPHA.id, 20, 30, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, ALPHA.id, 20, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                3, ALPHA.id, 30, 30, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(4, ALPHA.id, 30, 10)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.add_order(matching::Order::SellLimit(
                5, ALPHA.id, 20, 100, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(executions(handler).empty());
    REQUIRE(manager.find_order(1)->quantity == 30);
    REQUIRE(manager.find_order(2)->quantity == 10);
    REQUIRE(manager.find_order(3)->quantity == 30);
    REQUIRE(manager.find_order(4)->quantity == 10);
    REQUIRE(manager.find_order(5)->quantity == 100);
    REQUIRE(book_quantities(*manager.find_order_book(ALPHA.id)) ==
            std::pair<matching::Quantity, matching::Quantity>{80, 100});
    REQUIRE(handler.callback_state_valid);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                6, ALPHA.id, 20, 20, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(ALPHA.id)->empty());
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("A resting all-or-none order blocks later FIFO liquidity",
          "[matching]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellLimit(
                1, ALPHA.id, 100, 50, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 100, 30)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, ALPHA.id, 100, 40)) ==
            matching::ErrorCode::OK);
    REQUIRE(executions(handler).empty());
    REQUIRE(fifo_ids(manager.find_order_book(ALPHA.id)->find_ask(100)) ==
            std::vector<matching::OrderId>{1, 2});
    REQUIRE(manager.find_order(1)->quantity == 50);
    REQUIRE(manager.find_order(2)->quantity == 30);
    REQUIRE(manager.find_order(3)->quantity == 40);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(4, ALPHA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(manager.find_order(2)->quantity == 30);
    REQUIRE(manager.find_order(3) == nullptr);
    REQUIRE(manager.find_order(4) == nullptr);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Fill-or-kill respects indivisible resting liquidity", "[matching]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(
                2, ALPHA.id, 100, 50, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(3, ALPHA.id, 100, 40)) ==
            matching::ErrorCode::OK);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                4, ALPHA.id, 100, 40, matching::OrderTimeInForce::FOK)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1)->quantity == 10);
    REQUIRE(manager.find_order(2)->quantity == 50);
    REQUIRE(manager.find_order(3)->quantity == 40);
    REQUIRE(manager.find_order(4) == nullptr);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                5, ALPHA.id, 100, 100, matching::OrderTimeInForce::FOK)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(ALPHA.id)->empty());
}

TEST_CASE("Stored all-or-none chains can span both FIFO sides", "[matching]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                1, ALPHA.id, 100, 20, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(
                2, ALPHA.id, 100, 10, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(3, ALPHA.id, 100, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(
                4, ALPHA.id, 100, 15, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(5, ALPHA.id, 100, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                6, ALPHA.id, 100, 20, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);

    REQUIRE(book_order_counts(*manager.find_order_book(ALPHA.id)) ==
            std::pair<std::size_t, std::size_t>{3, 3});
    REQUIRE(book_quantities(*manager.find_order_book(ALPHA.id)) ==
            std::pair<matching::Quantity, matching::Quantity>{45, 30});

    REQUIRE(manager.add_order(matching::Order::SellLimit(7, ALPHA.id, 100, 15)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(ALPHA.id)->empty());
}

TEST_CASE("A stored chain never crosses through an ineligible price level",
          "[matching]")
{
    matching::MarketManager manager;
    initialize_market(manager);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                1, ALPHA.id, 100, 20, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 90, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(3, ALPHA.id, 110, 10)) ==
            matching::ErrorCode::OK);

    manager.enable_matching();
    REQUIRE(manager.find_order(1)->quantity == 20);
    REQUIRE(manager.find_order(2)->quantity == 10);
    REQUIRE(manager.find_order(3)->quantity == 10);

    REQUIRE(manager.add_order(matching::Order::SellLimit(4, ALPHA.id, 95, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(manager.find_order(2) == nullptr);
    REQUIRE(manager.find_order(4) == nullptr);
    REQUIRE(manager.find_order(3)->quantity == 10);
    REQUIRE(manager.find_order_book(ALPHA.id)->best_bid() == nullptr);
    REQUIRE(manager.find_order_book(ALPHA.id)->best_ask()->price == 110);
}

TEST_CASE("Manual execution cannot partially fill an all-or-none order",
          "[matching]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                1, ALPHA.id, 100, 20, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);

    REQUIRE(manager.execute_order(ALPHA.id, 1, 100, 10) ==
            matching::ErrorCode::ORDER_QUANTITY_INVALID);
    REQUIRE(manager.find_order(1)->quantity == 20);
    REQUIRE(manager.execute_order(ALPHA.id, 1, 100, 20) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1) == nullptr);
}

TEST_CASE("Lifecycle changes recheck an all-or-none blocked cross", "[matching]")
{
    SECTION("Reducing the blocker can make the quantities exact")
    {
        matching::MarketManager manager;
        initialize_market(manager);
        REQUIRE(manager.add_order(matching::Order::BuyLimit(
                    1, ALPHA.id, 100, 50, matching::OrderTimeInForce::AON)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 100, 40)) ==
                matching::ErrorCode::OK);

        manager.enable_matching();
        REQUIRE(manager.active_order_count() == 2);

        REQUIRE(manager.reduce_order(ALPHA.id, 1, 10) == matching::ErrorCode::OK);
        REQUIRE(manager.active_order_count() == 0);
        REQUIRE(manager.find_order_book(ALPHA.id)->empty());
    }

    SECTION("Cancelling the blocker exposes the next FIFO order")
    {
        matching::MarketManager manager;
        initialize_market(manager);
        REQUIRE(manager.add_order(matching::Order::SellLimit(
                    1, ALPHA.id, 100, 50, matching::OrderTimeInForce::AON)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 100, 20)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::BuyLimit(3, ALPHA.id, 100, 20)) ==
                matching::ErrorCode::OK);

        manager.enable_matching();
        REQUIRE(manager.active_order_count() == 3);

        REQUIRE(manager.delete_order(ALPHA.id, 1) == matching::ErrorCode::OK);
        REQUIRE(manager.active_order_count() == 0);
        REQUIRE(manager.find_order_book(ALPHA.id)->empty());
    }
}

TEST_CASE("Display-limited orders expose less depth without limiting execution",
          "[matching]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                1, ALPHA.id, 10, 10, matching::OrderTimeInForce::GTC, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                2, ALPHA.id, 20, 20, matching::OrderTimeInForce::GTC, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(
                3, ALPHA.id, 30, 30, matching::OrderTimeInForce::GTC, 15)) ==
            matching::ErrorCode::OK);

    const matching::OrderBook* book = manager.find_order_book(ALPHA.id);
    REQUIRE(book_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{60, 0});
    REQUIRE(book_displayed_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{30, 0});

    REQUIRE(manager.add_order(matching::Order::SellMarket(4, ALPHA.id, 55)) ==
            matching::ErrorCode::OK);
    REQUIRE(book_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{5, 0});
    REQUIRE(book_displayed_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{5, 0});
    REQUIRE(manager.find_order(1)->quantity == 5);
    REQUIRE(manager.find_order(2) == nullptr);
    REQUIRE(manager.find_order(3) == nullptr);
    REQUIRE(manager.find_order(1)->displayed_quantity() == 5);
}

TEST_CASE("Hidden reserve is consumed before displayed leaves without losing FIFO",
          "[matching]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellLimit(
                1, ALPHA.id, 100, 100, matching::OrderTimeInForce::GTC, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 100, 20)) ==
            matching::ErrorCode::OK);

    const matching::OrderBook* book = manager.find_order_book(ALPHA.id);
    REQUIRE(book->find_ask(100)->displayed_quantity == 30);
    REQUIRE(fifo_ids(book->find_ask(100)) ==
            std::vector<matching::OrderId>{1, 2});

    REQUIRE(manager.add_order(matching::Order::BuyMarket(3, ALPHA.id, 50)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(1)->quantity == 50);
    REQUIRE(manager.find_order(1)->displayed_quantity() == 10);
    REQUIRE(book->find_ask(100)->total_quantity == 70);
    REQUIRE(book->find_ask(100)->displayed_quantity == 30);
    REQUIRE(fifo_ids(book->find_ask(100)) ==
            std::vector<matching::OrderId>{1, 2});
}

TEST_CASE("Triggered market stops sweep liquidity in trigger and FIFO order",
          "[matching][stop]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);

    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 40)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyStop(2, ALPHA.id, 90, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyStop(3, ALPHA.id, 90, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyStop(4, ALPHA.id, 95, 10)) ==
            matching::ErrorCode::OK);
    handler.reset();

    manager.enable_matching();

    REQUIRE(manager.find_order(2) == nullptr);
    REQUIRE(manager.find_order(3) == nullptr);
    REQUIRE(manager.find_order(4) == nullptr);
    REQUIRE(manager.find_order(1)->quantity == 10);
    REQUIRE(manager.find_order_book(ALPHA.id)->best_buy_stop() == nullptr);

    const std::vector<Event> fills = executions(handler);
    REQUIRE(fills.size() == 6);
    REQUIRE(std::vector<matching::OrderId>{fills[0].order_id,
                                            fills[1].order_id,
                                            fills[2].order_id,
                                            fills[3].order_id,
                                            fills[4].order_id,
                                            fills[5].order_id} ==
            std::vector<matching::OrderId>{1, 2, 1, 3, 1, 4});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Stops remain dormant without a market reference", "[matching][stop]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellStop(1, ALPHA.id, 100, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyStopLimit(
                2, ALPHA.id, 100, 105, 20)) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::TrailingBuyStop(3, ALPHA.id, 120, 5, 10)) ==
            matching::ErrorCode::OK);

    const matching::OrderBook* book = manager.find_order_book(ALPHA.id);
    REQUIRE(manager.active_order_count() == 3);
    REQUIRE(book->best_sell_stop()->price == 100);
    REQUIRE(book->best_buy_stop()->price == 100);
    REQUIRE(book->best_trailing_buy_stop()->price == 120);
    REQUIRE(book->bids().empty());
    REQUIRE(book->asks().empty());
}

TEST_CASE("Activated market stops retain their slippage boundary",
          "[matching][stop]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 110, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyStop(
                3, ALPHA.id, 100, 10, matching::OrderTimeInForce::GTC, 0)) ==
            matching::ErrorCode::OK);

    REQUIRE(manager.find_order(3) == nullptr);
    REQUIRE(manager.find_order(1) == nullptr);
    REQUIRE(manager.find_order(2)->quantity == 5);
    REQUIRE(manager.find_order_book(ALPHA.id)->best_ask()->price == 110);
}

TEST_CASE("A newly eligible sell stop converts to IOC market and consumes bids",
          "[matching][stop]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 10, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, ALPHA.id, 20, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, ALPHA.id, 30, 30)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.add_order(matching::Order::SellStop(4, ALPHA.id, 40, 60)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(ALPHA.id)->empty());

    const std::vector<Event> fills = executions(handler);
    REQUIRE(fills.size() == 6);
    REQUIRE(std::vector<matching::OrderId>{fills[0].order_id,
                                            fills[1].order_id,
                                            fills[2].order_id,
                                            fills[3].order_id,
                                            fills[4].order_id,
                                            fills[5].order_id} ==
            std::vector<matching::OrderId>{3, 4, 2, 4, 1, 4});
    REQUIRE(order_event_types(handler, 4) ==
            std::vector<EventType>{EventType::ADD_ORDER,
                                   EventType::UPDATE_ORDER,
                                   EventType::EXECUTE_ORDER,
                                   EventType::EXECUTE_ORDER,
                                   EventType::EXECUTE_ORDER,
                                   EventType::DELETE_ORDER});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("FOK market stops either fill completely or leave depth unchanged",
          "[matching][stop]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 5)) ==
            matching::ErrorCode::OK);
    handler.reset();
    REQUIRE(manager.add_order(matching::Order::BuyStop(
                2, ALPHA.id, 100, 10, matching::OrderTimeInForce::FOK)) ==
            matching::ErrorCode::OK);

    REQUIRE(manager.find_order(2) == nullptr);
    REQUIRE(manager.find_order(1)->quantity == 5);
    REQUIRE(executions(handler).empty());
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("Triggered stop-limit orders match and rest only their residual",
          "[matching][stop_limit]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 30, 10)) ==
            matching::ErrorCode::OK);
    handler.reset();
    REQUIRE(manager.add_order(matching::Order::SellStopLimit(
                2, ALPHA.id, 40, 20, 25, matching::OrderTimeInForce::GTC, 5)) ==
            matching::ErrorCode::OK);

    const matching::Order* residual = manager.find_order(2);
    REQUIRE(residual != nullptr);
    REQUIRE(residual->is_limit());
    REQUIRE(residual->stop_price == 0);
    REQUIRE(residual->quantity == 15);
    REQUIRE(residual->displayed_quantity() == 5);
    REQUIRE(manager.find_order(1) == nullptr);

    const matching::Level* ask = manager.find_order_book(ALPHA.id)->find_ask(20);
    REQUIRE(ask != nullptr);
    REQUIRE(ask->total_quantity == 15);
    REQUIRE(ask->displayed_quantity == 5);
    REQUIRE(ask->order_count == 1);
    REQUIRE(order_event_types(handler, 2) ==
            std::vector<EventType>{EventType::ADD_ORDER,
                                   EventType::UPDATE_ORDER,
                                   EventType::EXECUTE_ORDER});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("IOC stop-limit residual is cancelled after activation",
          "[matching][stop_limit]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 30, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellStopLimit(
                2, ALPHA.id, 40, 20, 25, matching::OrderTimeInForce::IOC)) ==
            matching::ErrorCode::OK);

    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(ALPHA.id)->bids().empty());
    REQUIRE(manager.find_order_book(ALPHA.id)->asks().empty());
    REQUIRE(manager.find_order_book(ALPHA.id)->sell_stops().empty());
}

TEST_CASE("FOK stop-limit activation performs a read-only full-fill check",
          "[matching][stop_limit]")
{
    SECTION("Insufficient eligible quantity changes no depth")
    {
        MatchingHandler handler;
        matching::MarketManager manager(handler);
        handler.manager = &manager;
        initialize_market(manager);
        manager.enable_matching();

        REQUIRE(manager.add_order(
                    matching::Order::SellLimit(1, ALPHA.id, 100, 5)) ==
                matching::ErrorCode::OK);
        handler.reset();
        REQUIRE(manager.add_order(matching::Order::BuyStopLimit(
                    2,
                    ALPHA.id,
                    100,
                    100,
                    10,
                    matching::OrderTimeInForce::FOK)) ==
                matching::ErrorCode::OK);

        REQUIRE(manager.find_order(2) == nullptr);
        REQUIRE(manager.find_order(1)->quantity == 5);
        REQUIRE(executions(handler).empty());
        REQUIRE(handler.callback_state_valid);
    }

    SECTION("Exact eligible quantity executes completely")
    {
        matching::MarketManager manager;
        initialize_market(manager);
        manager.enable_matching();

        REQUIRE(manager.add_order(
                    matching::Order::SellLimit(1, ALPHA.id, 100, 10)) ==
                matching::ErrorCode::OK);
        REQUIRE(manager.add_order(matching::Order::BuyStopLimit(
                    2,
                    ALPHA.id,
                    100,
                    100,
                    10,
                    matching::OrderTimeInForce::FOK)) ==
                matching::ErrorCode::OK);

        REQUIRE(manager.active_order_count() == 0);
        REQUIRE(manager.find_order_book(ALPHA.id)->empty());
    }
}

TEST_CASE("Trailing orders reprice favorably and retain stop-limit offset",
          "[matching][trailing_stop]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 100, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(2, ALPHA.id, 200, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellMarket(3, ALPHA.id, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyMarket(4, ALPHA.id, 10)) ==
            matching::ErrorCode::OK);

    REQUIRE(manager.add_order(
                matching::Order::TrailingBuyStop(5, ALPHA.id, 1'000, 10, 10, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::TrailingSellStopLimit(
                6, ALPHA.id, 1, 11, 10, -1'000, -500)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(5)->stop_price == 210);
    REQUIRE(manager.find_order(6)->stop_price == 90);
    REQUIRE(manager.find_order(6)->price == 100);

    REQUIRE(manager.modify_order(ALPHA.id, 1, 103, 20) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(6)->stop_price == 90);
    REQUIRE(manager.find_order(6)->price == 100);

    REQUIRE(manager.modify_order(ALPHA.id, 1, 120, 20) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(6)->stop_price == 108);
    REQUIRE(manager.find_order(6)->price == 118);

    REQUIRE(manager.modify_order(ALPHA.id, 2, 197, 20) == matching::ErrorCode::OK);
    REQUIRE(manager.modify_order(ALPHA.id, 2, 180, 20) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(5)->stop_price == 210);

    REQUIRE(manager.modify_order(ALPHA.id, 2, 197, 20) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyMarket(7, ALPHA.id, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(5)->stop_price == 210);

    REQUIRE(manager.modify_order(ALPHA.id, 2, 180, 20) == matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyMarket(7, ALPHA.id, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.find_order(5)->stop_price == 190);
}

TEST_CASE("An unfavorable move activates a trailing stop through the normal pipeline",
          "[matching][trailing_stop]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);

    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 100, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::TrailingBuyStop(2, ALPHA.id, 110, 10, 10)) ==
            matching::ErrorCode::OK);
    manager.enable_matching();
    REQUIRE(manager.find_order(2) != nullptr);

    handler.reset();
    REQUIRE(manager.modify_order(ALPHA.id, 1, 110, 20) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(2) == nullptr);
    REQUIRE(manager.find_order(1)->quantity == 10);
    REQUIRE(manager.find_order_book(ALPHA.id)->trailing_buy_stops().empty());
    REQUIRE(executions(handler).size() == 2);
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("A quote exposed by matching can trigger a waiting stop", "[matching][stop]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::SellLimit(1, ALPHA.id, 30, 30)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyStop(2, ALPHA.id, 40, 40)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(3, ALPHA.id, 60, 60)) ==
            matching::ErrorCode::OK);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(4, ALPHA.id, 40, 40)) ==
            matching::ErrorCode::OK);

    const matching::OrderBook* book = manager.find_order_book(ALPHA.id);
    REQUIRE(manager.find_order(2) == nullptr);
    REQUIRE(book_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{10, 20});
    REQUIRE(book->best_bid()->price == 40);
    REQUIRE(book->best_ask()->price == 60);
}

TEST_CASE("Stop-limit activation covers full fills, residuals, and later triggers",
          "[matching][stop_limit]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 10, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, ALPHA.id, 20, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, ALPHA.id, 30, 30)) ==
            matching::ErrorCode::OK);

    REQUIRE(manager.add_order(
                matching::Order::SellStopLimit(4, ALPHA.id, 40, 20, 40)) ==
            matching::ErrorCode::OK);
    REQUIRE(book_quantities(*manager.find_order_book(ALPHA.id)) ==
            std::pair<matching::Quantity, matching::Quantity>{20, 0});

    REQUIRE(manager.add_order(
                matching::Order::SellStopLimit(5, ALPHA.id, 30, 10, 30)) ==
            matching::ErrorCode::OK);
    REQUIRE(book_quantities(*manager.find_order_book(ALPHA.id)) ==
            std::pair<matching::Quantity, matching::Quantity>{0, 10});
    REQUIRE(manager.find_order(5)->is_limit());
    REQUIRE(manager.find_order(5)->quantity == 10);

    REQUIRE(manager.add_order(
                matching::Order::BuyStopLimit(6, ALPHA.id, 20, 10, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellLimit(7, ALPHA.id, 20, 20)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(8, ALPHA.id, 20, 30)) ==
            matching::ErrorCode::OK);

    const matching::OrderBook* book = manager.find_order_book(ALPHA.id);
    REQUIRE(manager.find_order(6) != nullptr);
    REQUIRE(manager.find_order(6)->is_limit());
    REQUIRE(manager.find_order(6)->stop_price == 0);
    REQUIRE(manager.find_order(6)->quantity == 10);
    REQUIRE(book_quantities(*book) ==
            std::pair<matching::Quantity, matching::Quantity>{10, 0});
    REQUIRE(book->buy_stops().empty());
}

TEST_CASE("Triggered stop orders cascade as each lower bid is exposed",
          "[matching][stop]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 100, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(2, ALPHA.id, 90, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, ALPHA.id, 80, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellStop(4, ALPHA.id, 95, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellStop(5, ALPHA.id, 85, 5)) ==
            matching::ErrorCode::OK);
    handler.reset();

    REQUIRE(manager.add_order(matching::Order::SellMarket(6, ALPHA.id, 5)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(ALPHA.id)->empty());

    const std::vector<Event> fills = executions(handler);
    REQUIRE(fills.size() == 6);
    REQUIRE(std::vector<matching::OrderId>{fills[0].order_id,
                                            fills[1].order_id,
                                            fills[2].order_id,
                                            fills[3].order_id,
                                            fills[4].order_id,
                                            fills[5].order_id} ==
            std::vector<matching::OrderId>{1, 6, 2, 4, 3, 5});
    REQUIRE(handler.callback_state_valid);
}

TEST_CASE("A triggered AON stop-limit waits until an exact chain exists",
          "[matching][stop_limit]")
{
    matching::MarketManager manager;
    initialize_market(manager);
    manager.enable_matching();

    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 30, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::SellStopLimit(
                2, ALPHA.id, 40, 20, 20, matching::OrderTimeInForce::AON)) ==
            matching::ErrorCode::OK);

    REQUIRE(manager.find_order(2) != nullptr);
    REQUIRE(manager.find_order(2)->is_limit());
    REQUIRE(manager.find_order(2)->is_aon());
    REQUIRE(manager.find_order(2)->quantity == 20);
    REQUIRE(manager.find_order(1)->quantity == 10);

    REQUIRE(manager.add_order(matching::Order::BuyLimit(3, ALPHA.id, 20, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.active_order_count() == 0);
    REQUIRE(manager.find_order_book(ALPHA.id)->empty());
}

TEST_CASE("Trailing orders merging at one trigger use deterministic traversal FIFO",
          "[matching][trailing_stop]")
{
    MatchingHandler handler;
    matching::MarketManager manager(handler);
    handler.manager = &manager;
    initialize_market(manager);

    REQUIRE(manager.add_order(
                matching::Order::TrailingSellStop(2, ALPHA.id, 70, 5, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(
                matching::Order::TrailingSellStop(3, ALPHA.id, 80, 5, 10)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.add_order(matching::Order::BuyLimit(1, ALPHA.id, 100, 30)) ==
            matching::ErrorCode::OK);
    REQUIRE(manager.execute_order(ALPHA.id, 1, 100, 1) == matching::ErrorCode::OK);

    manager.enable_matching();
    const matching::OrderBook* book = manager.find_order_book(ALPHA.id);
    REQUIRE(manager.find_order(2)->stop_price == 90);
    REQUIRE(manager.find_order(3)->stop_price == 90);
    REQUIRE(fifo_ids(book->find_trailing_sell_stop(90)) ==
            std::vector<matching::OrderId>{3, 2});

    REQUIRE(manager.modify_order(ALPHA.id, 1, 110, 29) == matching::ErrorCode::OK);
    REQUIRE(manager.execute_order(ALPHA.id, 1, 110, 1) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(2)->stop_price == 100);
    REQUIRE(manager.find_order(3)->stop_price == 100);
    REQUIRE(fifo_ids(book->find_trailing_sell_stop(100)) ==
            std::vector<matching::OrderId>{3, 2});

    REQUIRE(manager.modify_order(ALPHA.id, 1, 120, 28) == matching::ErrorCode::OK);
    REQUIRE(manager.execute_order(ALPHA.id, 1, 120, 1) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(2)->stop_price == 110);
    REQUIRE(manager.find_order(3)->stop_price == 110);
    REQUIRE(fifo_ids(book->find_trailing_sell_stop(110)) ==
            std::vector<matching::OrderId>{3, 2});

    handler.reset();
    REQUIRE(manager.modify_order(ALPHA.id, 1, 110, 27) == matching::ErrorCode::OK);
    REQUIRE(manager.find_order(2) == nullptr);
    REQUIRE(manager.find_order(3) == nullptr);
    REQUIRE(manager.find_order(1)->quantity == 17);
    const std::vector<Event> fills = executions(handler);
    REQUIRE(fills.size() == 4);
    REQUIRE(std::vector<matching::OrderId>{fills[0].order_id,
                                            fills[1].order_id,
                                            fills[2].order_id,
                                            fills[3].order_id} ==
            std::vector<matching::OrderId>{1, 3, 1, 2});
    REQUIRE(handler.callback_state_valid);
}
