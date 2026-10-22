#include "matching/order_book.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <type_traits>
#include <vector>

namespace matching::detail {

class OrderBookTestAccess
{
public:
    static std::optional<LevelUpdate> add_order(OrderBook& book, OrderNode& order) noexcept
    {
        return book.add_order(order);
    }

    static LevelUpdate reduce_order(OrderBook& book,
                                    OrderNode& order,
                                    Quantity reduction) noexcept
    {
        return book.reduce_order(order, reduction);
    }

    static LevelUpdate delete_order(OrderBook& book, OrderNode& order) noexcept
    {
        return book.delete_order(order);
    }

    static const LevelNode* next_level(const OrderBook& book,
                                       const LevelNode& level) noexcept
    {
        return book.next_level(level);
    }

    static bool add_stop_order(OrderBook& book, OrderNode& order) noexcept
    {
        return book.add_stop_order(order);
    }

    static void reduce_stop_order(OrderBook& book,
                                  OrderNode& order,
                                  Quantity reduction) noexcept
    {
        book.reduce_stop_order(order, reduction);
    }

    static void delete_stop_order(OrderBook& book, OrderNode& order) noexcept
    {
        book.delete_stop_order(order);
    }

    static const LevelNode* next_stop_level(const OrderBook& book,
                                            const LevelNode& level) noexcept
    {
        return book.next_stop_level(level);
    }

    static bool add_trailing_stop_order(OrderBook& book, OrderNode& order) noexcept
    {
        return book.add_trailing_stop_order(order);
    }

    static void reduce_trailing_stop_order(OrderBook& book,
                                           OrderNode& order,
                                           Quantity reduction) noexcept
    {
        book.reduce_trailing_stop_order(order, reduction);
    }

    static void delete_trailing_stop_order(OrderBook& book, OrderNode& order) noexcept
    {
        book.delete_trailing_stop_order(order);
    }

    static const LevelNode* next_trailing_stop_level(const OrderBook& book,
                                                     const LevelNode& level) noexcept
    {
        return book.next_trailing_stop_level(level);
    }

    static Price calculate_trailing_stop_price(const OrderBook& book,
                                                const Order& order) noexcept
    {
        return book.calculate_trailing_stop_price(order);
    }

    static Price market_bid_price(const OrderBook& book) noexcept
    {
        return book.market_bid_price();
    }

    static Price market_ask_price(const OrderBook& book) noexcept
    {
        return book.market_ask_price();
    }

    static Price market_trailing_bid_price(const OrderBook& book) noexcept
    {
        return book.market_trailing_bid_price();
    }

    static Price market_trailing_ask_price(const OrderBook& book) noexcept
    {
        return book.market_trailing_ask_price();
    }

    static void update_last_price(OrderBook& book, const Order& order, Price price) noexcept
    {
        book.update_last_price(order, price);
    }

    static void update_matching_price(OrderBook& book,
                                      const Order& order,
                                      Price price) noexcept
    {
        book.update_matching_price(order, price);
    }

    static void reset_matching_price(OrderBook& book) noexcept
    {
        book.reset_matching_price();
    }
};

} // namespace matching::detail

namespace {

using LevelTree = matching::OrderBook::Levels;
using TestAccess = matching::detail::OrderBookTestAccess;

struct TreeValidation
{
    int height;
    std::size_t node_count;
};

TreeValidation validate_subtree(const matching::LevelNode* level,
                                const matching::LevelNode* expected_parent,
                                std::optional<matching::Price> lower_price,
                                std::optional<matching::Price> upper_price,
                                matching::LevelType expected_type,
                                matching::SymbolId expected_symbol)
{
    if (level == nullptr)
        return {0, 0};

    REQUIRE(level->parent == expected_parent);
    REQUIRE(level->type == expected_type);
    REQUIRE(level->price > 0);
    if (lower_price.has_value())
        REQUIRE(level->price > *lower_price);
    if (upper_price.has_value())
        REQUIRE(level->price < *upper_price);

    std::size_t order_count = 0;
    matching::Quantity total_quantity = 0;
    matching::Quantity displayed_quantity = 0;
    const matching::OrderNode* previous = nullptr;

    for (const matching::OrderNode* order = level->orders.front(); order != nullptr;
         order = order->next)
    {
        REQUIRE(order->previous == previous);
        REQUIRE(order->level == level);
        REQUIRE(order->symbol_id == expected_symbol);
        REQUIRE(order->price == level->price);
        REQUIRE(order->is_limit());
        REQUIRE(order->is_gtc());
        REQUIRE(order->quantity > 0);
        REQUIRE(order->is_buy() == level->is_bid());
        REQUIRE(order->is_sell() == level->is_ask());
        REQUIRE(total_quantity <=
                (std::numeric_limits<matching::Quantity>::max() - order->quantity));

        total_quantity += order->quantity;
        displayed_quantity += order->displayed_quantity();
        ++order_count;
        previous = order;
    }

    REQUIRE(previous == level->orders.back());
    REQUIRE(order_count == level->orders.size());
    REQUIRE(order_count == level->order_count);
    REQUIRE(total_quantity == level->total_quantity);
    REQUIRE(displayed_quantity == level->displayed_quantity);
    REQUIRE(order_count > 0);
    REQUIRE(total_quantity > 0);

    const TreeValidation left = validate_subtree(
        level->left, level, lower_price, level->price, expected_type, expected_symbol);
    const TreeValidation right = validate_subtree(
        level->right, level, level->price, upper_price, expected_type, expected_symbol);
    const int expected_balance = right.height - left.height;

    REQUIRE(expected_balance >= -1);
    REQUIRE(expected_balance <= 1);
    REQUIRE(static_cast<int>(level->balance) == expected_balance);

    return {1 + std::max(left.height, right.height),
            1 + left.node_count + right.node_count};
}

void validate_tree(const LevelTree& tree,
                   matching::LevelType expected_type,
                   matching::SymbolId expected_symbol)
{
    const TreeValidation validation = validate_subtree(
        tree.root(), nullptr, std::nullopt, std::nullopt, expected_type, expected_symbol);

    REQUIRE(validation.node_count == tree.size());
    REQUIRE(tree.empty() == (tree.size() == 0));

    const matching::LevelNode* previous = nullptr;
    for (const matching::LevelNode& level : tree)
    {
        if (previous != nullptr)
            REQUIRE(previous->price < level.price);
        previous = &level;
    }
}

TreeValidation validate_stop_subtree(const matching::LevelNode* level,
                                     const matching::LevelNode* expected_parent,
                                     std::optional<matching::Price> lower_price,
                                     std::optional<matching::Price> upper_price,
                                     matching::LevelType expected_type,
                                     matching::OrderSide expected_side,
                                     bool expected_trailing,
                                     matching::SymbolId expected_symbol)
{
    if (level == nullptr)
        return {0, 0};

    REQUIRE(level->parent == expected_parent);
    REQUIRE(level->type == expected_type);
    REQUIRE(level->price > 0);
    if (lower_price.has_value())
        REQUIRE(level->price > *lower_price);
    if (upper_price.has_value())
        REQUIRE(level->price < *upper_price);

    std::size_t order_count = 0;
    matching::Quantity total_quantity = 0;
    matching::Quantity displayed_quantity = 0;
    const matching::OrderNode* previous = nullptr;

    for (const matching::OrderNode* order = level->orders.front(); order != nullptr;
         order = order->next)
    {
        REQUIRE(order->previous == previous);
        REQUIRE(order->level == level);
        REQUIRE(order->symbol_id == expected_symbol);
        REQUIRE(order->side == expected_side);
        REQUIRE(order->stop_price == level->price);
        if (expected_trailing)
            REQUIRE((order->is_trailing_stop() || order->is_trailing_stop_limit()));
        else
            REQUIRE((order->is_stop() || order->is_stop_limit()));
        REQUIRE(order->quantity > 0);
        REQUIRE(total_quantity <=
                (std::numeric_limits<matching::Quantity>::max() - order->quantity));

        total_quantity += order->quantity;
        displayed_quantity += order->displayed_quantity();
        ++order_count;
        previous = order;
    }

    REQUIRE(previous == level->orders.back());
    REQUIRE(order_count == level->orders.size());
    REQUIRE(order_count == level->order_count);
    REQUIRE(total_quantity == level->total_quantity);
    REQUIRE(displayed_quantity == level->displayed_quantity);
    REQUIRE(order_count > 0);
    REQUIRE(total_quantity > 0);

    const TreeValidation left = validate_stop_subtree(level->left,
                                                      level,
                                                      lower_price,
                                                      level->price,
                                                      expected_type,
                                                      expected_side,
                                                      expected_trailing,
                                                      expected_symbol);
    const TreeValidation right = validate_stop_subtree(level->right,
                                                       level,
                                                       level->price,
                                                       upper_price,
                                                       expected_type,
                                                       expected_side,
                                                       expected_trailing,
                                                       expected_symbol);
    const int expected_balance = right.height - left.height;

    REQUIRE(expected_balance >= -1);
    REQUIRE(expected_balance <= 1);
    REQUIRE(static_cast<int>(level->balance) == expected_balance);

    return {1 + std::max(left.height, right.height),
            1 + left.node_count + right.node_count};
}

void validate_stop_tree(const LevelTree& tree,
                        matching::LevelType expected_type,
                        matching::OrderSide expected_side,
                        bool expected_trailing,
                        matching::SymbolId expected_symbol)
{
    const TreeValidation validation = validate_stop_subtree(tree.root(),
                                                            nullptr,
                                                            std::nullopt,
                                                            std::nullopt,
                                                            expected_type,
                                                            expected_side,
                                                            expected_trailing,
                                                            expected_symbol);

    REQUIRE(validation.node_count == tree.size());
    REQUIRE(tree.empty() == (tree.size() == 0));

    const matching::LevelNode* previous = nullptr;
    for (const matching::LevelNode& level : tree)
    {
        if (previous != nullptr)
            REQUIRE(previous->price < level.price);
        previous = &level;
    }
}

void validate_book(const matching::OrderBook& book)
{
    validate_tree(book.bids(), matching::LevelType::BID, book.symbol().id);
    validate_tree(book.asks(), matching::LevelType::ASK, book.symbol().id);
    validate_stop_tree(book.buy_stops(),
                       matching::LevelType::ASK,
                       matching::OrderSide::BUY,
                       false,
                       book.symbol().id);
    validate_stop_tree(book.sell_stops(),
                       matching::LevelType::BID,
                       matching::OrderSide::SELL,
                       false,
                       book.symbol().id);
    validate_stop_tree(book.trailing_buy_stops(),
                       matching::LevelType::ASK,
                       matching::OrderSide::BUY,
                       true,
                       book.symbol().id);
    validate_stop_tree(book.trailing_sell_stops(),
                       matching::LevelType::BID,
                       matching::OrderSide::SELL,
                       true,
                       book.symbol().id);

    REQUIRE(book.level_count() == (book.bids().size() + book.asks().size() +
                                   book.buy_stops().size() + book.sell_stops().size() +
                                   book.trailing_buy_stops().size() +
                                   book.trailing_sell_stops().size()));
    REQUIRE(book.empty() == (book.level_count() == 0));
    REQUIRE(book.best_bid() == static_cast<const matching::Level*>(book.bids().maximum()));
    REQUIRE(book.best_ask() == static_cast<const matching::Level*>(book.asks().minimum()));
    REQUIRE(book.best_buy_stop() ==
            static_cast<const matching::Level*>(book.buy_stops().minimum()));
    REQUIRE(book.best_sell_stop() ==
            static_cast<const matching::Level*>(book.sell_stops().maximum()));
    REQUIRE(book.best_trailing_buy_stop() ==
            static_cast<const matching::Level*>(book.trailing_buy_stops().minimum()));
    REQUIRE(book.best_trailing_sell_stop() ==
            static_cast<const matching::Level*>(book.trailing_sell_stops().maximum()));

    for (const matching::LevelNode& level : book.bids())
        REQUIRE(book.find_bid(level.price) == static_cast<const matching::Level*>(&level));
    for (const matching::LevelNode& level : book.asks())
        REQUIRE(book.find_ask(level.price) == static_cast<const matching::Level*>(&level));
    for (const matching::LevelNode& level : book.buy_stops())
        REQUIRE(book.find_buy_stop(level.price) ==
                static_cast<const matching::Level*>(&level));
    for (const matching::LevelNode& level : book.sell_stops())
        REQUIRE(book.find_sell_stop(level.price) ==
                static_cast<const matching::Level*>(&level));
    for (const matching::LevelNode& level : book.trailing_buy_stops())
        REQUIRE(book.find_trailing_buy_stop(level.price) ==
                static_cast<const matching::Level*>(&level));
    for (const matching::LevelNode& level : book.trailing_sell_stops())
        REQUIRE(book.find_trailing_sell_stop(level.price) ==
                static_cast<const matching::Level*>(&level));
}

class RestingOrderCleanup
{
public:
    RestingOrderCleanup(matching::OrderBook& book,
                        std::initializer_list<matching::OrderNode*> orders)
        : book_(book),
          orders_(orders)
    {
    }

    RestingOrderCleanup(const RestingOrderCleanup&) = delete;
    RestingOrderCleanup& operator=(const RestingOrderCleanup&) = delete;

    ~RestingOrderCleanup() noexcept
    {
        for (matching::OrderNode* order : orders_)
        {
            if (order->level != nullptr)
                static_cast<void>(TestAccess::delete_order(book_, *order));
        }
    }

private:
    matching::OrderBook& book_;
    std::vector<matching::OrderNode*> orders_;
};

class StopOrderCleanup
{
public:
    StopOrderCleanup(matching::OrderBook& book,
                     std::initializer_list<matching::OrderNode*> orders)
        : book_(book),
          orders_(orders)
    {
    }

    StopOrderCleanup(const StopOrderCleanup&) = delete;
    StopOrderCleanup& operator=(const StopOrderCleanup&) = delete;

    ~StopOrderCleanup() noexcept
    {
        for (matching::OrderNode* order : orders_)
        {
            if (order->level != nullptr)
                TestAccess::delete_stop_order(book_, *order);
        }
    }

private:
    matching::OrderBook& book_;
    std::vector<matching::OrderNode*> orders_;
};

class TrailingStopOrderCleanup
{
public:
    TrailingStopOrderCleanup(matching::OrderBook& book,
                             std::initializer_list<matching::OrderNode*> orders)
        : book_(book),
          orders_(orders)
    {
    }

    TrailingStopOrderCleanup(const TrailingStopOrderCleanup&) = delete;
    TrailingStopOrderCleanup& operator=(const TrailingStopOrderCleanup&) = delete;

    ~TrailingStopOrderCleanup() noexcept
    {
        for (matching::OrderNode* order : orders_)
        {
            if (order->level != nullptr)
                TestAccess::delete_trailing_stop_order(book_, *order);
        }
    }

private:
    matching::OrderBook& book_;
    std::vector<matching::OrderNode*> orders_;
};

matching::LevelUpdate add_order(matching::OrderBook& book, matching::OrderNode& order)
{
    const std::optional<matching::LevelUpdate> update = TestAccess::add_order(book, order);
    REQUIRE(update.has_value());
    return *update;
}

} // namespace

static_assert(!std::is_default_constructible_v<matching::OrderBook>);
static_assert(!std::is_copy_constructible_v<matching::OrderBook>);
static_assert(!std::is_copy_assignable_v<matching::OrderBook>);
static_assert(!std::is_move_constructible_v<matching::OrderBook>);
static_assert(!std::is_move_assignable_v<matching::OrderBook>);

TEST_CASE("A regular order book starts empty", "[order_book]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    const matching::OrderBook order_book(level_pool, symbol);

    REQUIRE(order_book.symbol() == symbol);
    REQUIRE(order_book.empty());
    REQUIRE(order_book.level_count() == 0);
    REQUIRE(order_book.bids().empty());
    REQUIRE(order_book.asks().empty());
    REQUIRE(order_book.buy_stops().empty());
    REQUIRE(order_book.sell_stops().empty());
    REQUIRE(order_book.trailing_buy_stops().empty());
    REQUIRE(order_book.trailing_sell_stops().empty());
    REQUIRE(order_book.best_bid() == nullptr);
    REQUIRE(order_book.best_ask() == nullptr);
    REQUIRE(order_book.best_buy_stop() == nullptr);
    REQUIRE(order_book.best_sell_stop() == nullptr);
    REQUIRE(order_book.best_trailing_buy_stop() == nullptr);
    REQUIRE(order_book.best_trailing_sell_stop() == nullptr);
    REQUIRE(order_book.find_bid(18'525) == nullptr);
    REQUIRE(order_book.find_ask(18'530) == nullptr);
    REQUIRE(order_book.find_buy_stop(18'600) == nullptr);
    REQUIRE(order_book.find_sell_stop(18'400) == nullptr);
    REQUIRE(order_book.find_trailing_buy_stop(18'600) == nullptr);
    REQUIRE(order_book.find_trailing_sell_stop(18'400) == nullptr);
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}

TEST_CASE("The first resting order creates and later removes its price level",
          "[order_book]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode order(matching::Order::BuyLimit(1, symbol.id, 18'525, 100));
    RestingOrderCleanup cleanup(order_book, {&order});

    const matching::LevelUpdate addition = add_order(order_book, order);

    REQUIRE(addition.type == matching::UpdateType::ADD);
    REQUIRE(addition.level.type == matching::LevelType::BID);
    REQUIRE(addition.level.price == 18'525);
    REQUIRE(addition.level.total_quantity == 100);
    REQUIRE(addition.level.order_count == 1);
    REQUIRE(addition.top);
    REQUIRE(order.level != nullptr);
    REQUIRE(order_book.best_bid() == order.level);
    REQUIRE(order_book.find_bid(order.price) == order.level);
    REQUIRE(level_pool.live_count() == 1);
    validate_book(order_book);

    const matching::LevelUpdate deletion = TestAccess::delete_order(order_book, order);

    REQUIRE(deletion.type == matching::UpdateType::DELETE);
    REQUIRE(deletion.level.type == matching::LevelType::BID);
    REQUIRE(deletion.level.price == 18'525);
    REQUIRE(deletion.level.total_quantity == 0);
    REQUIRE(deletion.level.order_count == 0);
    REQUIRE(deletion.top);
    REQUIRE(order.quantity == 100);
    REQUIRE(order.level == nullptr);
    REQUIRE(order.previous == nullptr);
    REQUIRE(order.next == nullptr);
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}

TEST_CASE("Bid and ask levels maintain price ordering and cached best prices",
          "[order_book]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode bid_middle(matching::Order::BuyLimit(1, symbol.id, 18'520, 10));
    matching::OrderNode bid_worse(matching::Order::BuyLimit(2, symbol.id, 18'510, 20));
    matching::OrderNode bid_better(matching::Order::BuyLimit(3, symbol.id, 18'525, 30));
    matching::OrderNode ask_middle(matching::Order::SellLimit(4, symbol.id, 18'535, 40));
    matching::OrderNode ask_worse(matching::Order::SellLimit(5, symbol.id, 18'540, 50));
    matching::OrderNode ask_better(matching::Order::SellLimit(6, symbol.id, 18'530, 60));
    RestingOrderCleanup cleanup(order_book,
                                {&bid_middle,
                                 &bid_worse,
                                 &bid_better,
                                 &ask_middle,
                                 &ask_worse,
                                 &ask_better});

    REQUIRE(add_order(order_book, bid_middle).top);
    validate_book(order_book);
    REQUIRE_FALSE(add_order(order_book, bid_worse).top);
    validate_book(order_book);
    REQUIRE(add_order(order_book, bid_better).top);
    validate_book(order_book);
    REQUIRE(add_order(order_book, ask_middle).top);
    validate_book(order_book);
    REQUIRE_FALSE(add_order(order_book, ask_worse).top);
    validate_book(order_book);
    REQUIRE(add_order(order_book, ask_better).top);
    validate_book(order_book);

    REQUIRE(order_book.level_count() == 6);
    REQUIRE(order_book.best_bid()->price == 18'525);
    REQUIRE(order_book.best_ask()->price == 18'530);
    REQUIRE(level_pool.live_count() == 6);

    std::vector<matching::Price> bid_prices;
    for (auto iterator = order_book.bids().rbegin(); iterator != order_book.bids().rend();
         ++iterator)
    {
        bid_prices.push_back(iterator->price);
    }

    std::vector<matching::Price> ask_prices;
    for (const matching::LevelNode& level : order_book.asks())
        ask_prices.push_back(level.price);

    REQUIRE(bid_prices == std::vector<matching::Price>{18'525, 18'520, 18'510});
    REQUIRE(ask_prices == std::vector<matching::Price>{18'530, 18'535, 18'540});
    REQUIRE(TestAccess::next_level(order_book, *bid_better.level) == bid_middle.level);
    REQUIRE(TestAccess::next_level(order_book, *bid_middle.level) == bid_worse.level);
    REQUIRE(TestAccess::next_level(order_book, *bid_worse.level) == nullptr);
    REQUIRE(TestAccess::next_level(order_book, *ask_better.level) == ask_middle.level);
    REQUIRE(TestAccess::next_level(order_book, *ask_middle.level) == ask_worse.level);
    REQUIRE(TestAccess::next_level(order_book, *ask_worse.level) == nullptr);

    const matching::LevelUpdate removed_worse_bid =
        TestAccess::delete_order(order_book, bid_worse);
    REQUIRE(removed_worse_bid.type == matching::UpdateType::DELETE);
    REQUIRE_FALSE(removed_worse_bid.top);
    REQUIRE(order_book.best_bid()->price == 18'525);
    validate_book(order_book);

    const matching::LevelUpdate removed_worse_ask =
        TestAccess::delete_order(order_book, ask_worse);
    REQUIRE(removed_worse_ask.type == matching::UpdateType::DELETE);
    REQUIRE_FALSE(removed_worse_ask.top);
    REQUIRE(order_book.best_ask()->price == 18'530);
    validate_book(order_book);

    const matching::LevelUpdate removed_bid =
        TestAccess::delete_order(order_book, bid_better);
    REQUIRE(removed_bid.type == matching::UpdateType::DELETE);
    REQUIRE(removed_bid.top);
    REQUIRE(order_book.best_bid()->price == 18'520);
    validate_book(order_book);

    const matching::LevelUpdate removed_ask =
        TestAccess::delete_order(order_book, ask_better);
    REQUIRE(removed_ask.type == matching::UpdateType::DELETE);
    REQUIRE(removed_ask.top);
    REQUIRE(order_book.best_ask()->price == 18'535);
    validate_book(order_book);

    static_cast<void>(TestAccess::delete_order(order_book, bid_middle));
    static_cast<void>(TestAccess::delete_order(order_book, ask_middle));
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}

TEST_CASE("Orders at one price preserve FIFO through reduction and middle deletion",
          "[order_book]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode first(matching::Order::BuyLimit(1, symbol.id, 18'525, 10));
    matching::OrderNode middle(matching::Order::BuyLimit(2, symbol.id, 18'525, 20));
    matching::OrderNode last(matching::Order::BuyLimit(3, symbol.id, 18'525, 30));
    RestingOrderCleanup cleanup(order_book, {&first, &middle, &last});

    REQUIRE(add_order(order_book, first).type == matching::UpdateType::ADD);
    REQUIRE(add_order(order_book, middle).type == matching::UpdateType::UPDATE);
    REQUIRE(add_order(order_book, last).type == matching::UpdateType::UPDATE);
    REQUIRE(level_pool.live_count() == 1);
    REQUIRE(first.level == middle.level);
    REQUIRE(middle.level == last.level);

    matching::LevelNode* level = first.level;
    REQUIRE(level->orders.front() == &first);
    REQUIRE(level->orders.back() == &last);
    REQUIRE(level->total_quantity == 60);
    REQUIRE(level->order_count == 3);
    validate_book(order_book);

    const matching::LevelUpdate middle_deletion =
        TestAccess::delete_order(order_book, middle);
    REQUIRE(middle_deletion.type == matching::UpdateType::UPDATE);
    REQUIRE(middle_deletion.level.total_quantity == 40);
    REQUIRE(middle_deletion.level.order_count == 2);
    REQUIRE(first.next == &last);
    REQUIRE(last.previous == &first);
    REQUIRE(middle.level == nullptr);
    validate_book(order_book);

    const matching::LevelUpdate partial_reduction =
        TestAccess::reduce_order(order_book, first, 4);
    REQUIRE(partial_reduction.type == matching::UpdateType::UPDATE);
    REQUIRE(partial_reduction.level.total_quantity == 36);
    REQUIRE(partial_reduction.level.order_count == 2);
    REQUIRE(first.quantity == 6);
    REQUIRE(level->orders.front() == &first);
    validate_book(order_book);

    const matching::LevelUpdate full_reduction =
        TestAccess::reduce_order(order_book, first, 6);
    REQUIRE(full_reduction.type == matching::UpdateType::UPDATE);
    REQUIRE(full_reduction.level.total_quantity == 30);
    REQUIRE(full_reduction.level.order_count == 1);
    REQUIRE(first.quantity == 0);
    REQUIRE(first.level == nullptr);
    REQUIRE(level->orders.front() == &last);
    REQUIRE(last.previous == nullptr);
    validate_book(order_book);

    const matching::LevelUpdate last_deletion = TestAccess::delete_order(order_book, last);
    REQUIRE(last_deletion.type == matching::UpdateType::DELETE);
    REQUIRE(last_deletion.level.total_quantity == 0);
    REQUIRE(last_deletion.level.order_count == 0);
    REQUIRE(last_deletion.top);
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}

TEST_CASE("Level-pool exhaustion leaves the order book unchanged", "[order_book]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool(
        sizeof(matching::LevelNode), 1);
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode first(matching::Order::BuyLimit(1, symbol.id, 18'525, 10));
    matching::OrderNode second(matching::Order::BuyLimit(2, symbol.id, 18'520, 20));
    RestingOrderCleanup cleanup(order_book, {&first, &second});

    REQUIRE(add_order(order_book, first).type == matching::UpdateType::ADD);
    const matching::Level* original_best = order_book.best_bid();
    const std::optional<matching::LevelUpdate> failed =
        TestAccess::add_order(order_book, second);

    REQUIRE_FALSE(failed.has_value());
    REQUIRE(second.level == nullptr);
    REQUIRE(second.previous == nullptr);
    REQUIRE(second.next == nullptr);
    REQUIRE(order_book.level_count() == 1);
    REQUIRE(order_book.best_bid() == original_best);
    REQUIRE(order_book.find_bid(second.price) == nullptr);
    REQUIRE(level_pool.live_count() == 1);
    validate_book(order_book);

    static_cast<void>(TestAccess::delete_order(order_book, first));
    REQUIRE(level_pool.live_count() == 0);

    REQUIRE(add_order(order_book, second).type == matching::UpdateType::ADD);
    REQUIRE(order_book.best_bid()->price == second.price);
    REQUIRE(level_pool.live_count() == 1);
    validate_book(order_book);

    static_cast<void>(TestAccess::delete_order(order_book, second));
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}

TEST_CASE("Order books have deterministic diagnostic output", "[order_book]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    const matching::OrderBook order_book(level_pool, symbol);
    std::ostringstream stream;

    stream << order_book;

    REQUIRE(stream.str() ==
            "OrderBook(Symbol=Symbol(Id=7; Name=\"RELIANCE\"); BidLevels=0; AskLevels=0; "
            "BuyStopLevels=0; SellStopLevels=0; TrailingBuyStopLevels=0; "
            "TrailingSellStopLevels=0)");
}

TEST_CASE("Plain stop levels maintain trigger ordering and cached candidates",
          "[order_book][stop]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);

    matching::OrderNode buy_middle(matching::Order::BuyStop(101, symbol.id, 18'700, 10));
    matching::OrderNode buy_first(matching::Order::BuyStop(102, symbol.id, 18'600, 20));
    matching::OrderNode buy_last(matching::Order::BuyStop(103, symbol.id, 18'800, 30));
    matching::OrderNode sell_middle(matching::Order::SellStop(104, symbol.id, 18'300, 40));
    matching::OrderNode sell_first(matching::Order::SellStop(105, symbol.id, 18'400, 50));
    matching::OrderNode sell_last(matching::Order::SellStop(106, symbol.id, 18'200, 60));
    StopOrderCleanup cleanup(order_book,
                             {&buy_middle,
                              &buy_first,
                              &buy_last,
                              &sell_middle,
                              &sell_first,
                              &sell_last});

    REQUIRE(TestAccess::add_stop_order(order_book, buy_middle));
    REQUIRE(TestAccess::add_stop_order(order_book, buy_first));
    REQUIRE(TestAccess::add_stop_order(order_book, buy_last));
    REQUIRE(TestAccess::add_stop_order(order_book, sell_middle));
    REQUIRE(TestAccess::add_stop_order(order_book, sell_first));
    REQUIRE(TestAccess::add_stop_order(order_book, sell_last));
    validate_book(order_book);

    REQUIRE(order_book.level_count() == 6);
    REQUIRE(order_book.best_bid() == nullptr);
    REQUIRE(order_book.best_ask() == nullptr);
    REQUIRE(order_book.best_buy_stop()->price == 18'600);
    REQUIRE(order_book.best_sell_stop()->price == 18'400);
    REQUIRE(buy_first.level->type == matching::LevelType::ASK);
    REQUIRE(sell_first.level->type == matching::LevelType::BID);

    std::vector<matching::Price> buy_stop_prices;
    for (const matching::LevelNode& level : order_book.buy_stops())
        buy_stop_prices.push_back(level.price);

    std::vector<matching::Price> sell_stop_prices;
    for (const matching::LevelNode& level : order_book.sell_stops())
        sell_stop_prices.push_back(level.price);

    REQUIRE(buy_stop_prices == std::vector<matching::Price>{18'600, 18'700, 18'800});
    REQUIRE(sell_stop_prices == std::vector<matching::Price>{18'200, 18'300, 18'400});
    REQUIRE(TestAccess::next_stop_level(order_book, *buy_first.level) == buy_middle.level);
    REQUIRE(TestAccess::next_stop_level(order_book, *buy_middle.level) == buy_last.level);
    REQUIRE(TestAccess::next_stop_level(order_book, *buy_last.level) == nullptr);
    REQUIRE(TestAccess::next_stop_level(order_book, *sell_first.level) == sell_middle.level);
    REQUIRE(TestAccess::next_stop_level(order_book, *sell_middle.level) == sell_last.level);
    REQUIRE(TestAccess::next_stop_level(order_book, *sell_last.level) == nullptr);

    TestAccess::delete_stop_order(order_book, buy_first);
    REQUIRE(order_book.best_buy_stop()->price == 18'700);
    validate_book(order_book);

    TestAccess::delete_stop_order(order_book, sell_first);
    REQUIRE(order_book.best_sell_stop()->price == 18'300);
    validate_book(order_book);

    TestAccess::delete_stop_order(order_book, buy_last);
    TestAccess::delete_stop_order(order_book, sell_last);
    REQUIRE(order_book.best_buy_stop()->price == 18'700);
    REQUIRE(order_book.best_sell_stop()->price == 18'300);
    validate_book(order_book);

    TestAccess::delete_stop_order(order_book, buy_middle);
    TestAccess::delete_stop_order(order_book, sell_middle);
    REQUIRE(order_book.empty());
    REQUIRE(order_book.best_buy_stop() == nullptr);
    REQUIRE(order_book.best_sell_stop() == nullptr);
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}

TEST_CASE("Orders at one stop trigger preserve FIFO and quantity aggregates",
          "[order_book][stop]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);

    matching::OrderNode first(matching::Order::BuyStop(201, symbol.id, 18'600, 10));
    matching::OrderNode middle(
        matching::Order::BuyStopLimit(202, symbol.id, 18'600, 18'610, 20));
    matching::OrderNode last(matching::Order::BuyStop(203, symbol.id, 18'600, 30));
    StopOrderCleanup cleanup(order_book, {&first, &middle, &last});

    REQUIRE(TestAccess::add_stop_order(order_book, first));
    REQUIRE(TestAccess::add_stop_order(order_book, middle));
    REQUIRE(TestAccess::add_stop_order(order_book, last));
    REQUIRE(level_pool.live_count() == 1);
    REQUIRE(first.level == middle.level);
    REQUIRE(middle.level == last.level);

    matching::LevelNode* level = first.level;
    REQUIRE(level->orders.front() == &first);
    REQUIRE(level->orders.back() == &last);
    REQUIRE(level->total_quantity == 60);
    REQUIRE(level->order_count == 3);
    validate_book(order_book);

    TestAccess::delete_stop_order(order_book, middle);
    REQUIRE(middle.quantity == 20);
    REQUIRE(middle.level == nullptr);
    REQUIRE(first.next == &last);
    REQUIRE(last.previous == &first);
    REQUIRE(level->total_quantity == 40);
    REQUIRE(level->order_count == 2);
    validate_book(order_book);

    TestAccess::reduce_stop_order(order_book, first, 4);
    REQUIRE(first.quantity == 6);
    REQUIRE(first.level == level);
    REQUIRE(level->orders.front() == &first);
    REQUIRE(level->total_quantity == 36);
    REQUIRE(level->order_count == 2);
    validate_book(order_book);

    TestAccess::reduce_stop_order(order_book, first, 6);
    REQUIRE(first.quantity == 0);
    REQUIRE(first.level == nullptr);
    REQUIRE(level->orders.front() == &last);
    REQUIRE(last.previous == nullptr);
    REQUIRE(level->total_quantity == 30);
    REQUIRE(level->order_count == 1);
    validate_book(order_book);

    TestAccess::delete_stop_order(order_book, last);
    REQUIRE(last.quantity == 30);
    REQUIRE(last.level == nullptr);
    REQUIRE(order_book.find_buy_stop(18'600) == nullptr);
    REQUIRE(order_book.best_buy_stop() == nullptr);
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}

TEST_CASE("Stop-level pool exhaustion leaves indexes unchanged", "[order_book][stop]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool(
        sizeof(matching::LevelNode), 1);
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode first(matching::Order::BuyStop(301, symbol.id, 18'600, 10));
    matching::OrderNode second(matching::Order::BuyStop(302, symbol.id, 18'700, 20));
    StopOrderCleanup cleanup(order_book, {&first, &second});

    REQUIRE(TestAccess::add_stop_order(order_book, first));
    const matching::Level* original_candidate = order_book.best_buy_stop();
    REQUIRE_FALSE(TestAccess::add_stop_order(order_book, second));

    REQUIRE(second.level == nullptr);
    REQUIRE(second.previous == nullptr);
    REQUIRE(second.next == nullptr);
    REQUIRE(order_book.level_count() == 1);
    REQUIRE(order_book.best_buy_stop() == original_candidate);
    REQUIRE(order_book.find_buy_stop(second.stop_price) == nullptr);
    REQUIRE(level_pool.live_count() == 1);
    validate_book(order_book);

    TestAccess::delete_stop_order(order_book, first);
    REQUIRE(level_pool.live_count() == 0);

    REQUIRE(TestAccess::add_stop_order(order_book, second));
    REQUIRE(order_book.best_buy_stop()->price == second.stop_price);
    REQUIRE(level_pool.live_count() == 1);
    validate_book(order_book);

    TestAccess::delete_stop_order(order_book, second);
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}

TEST_CASE("Trailing-stop indexes maintain side-specific trigger ordering",
          "[order_book][trailing_stop]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);

    matching::OrderNode buy_middle(
        matching::Order::TrailingBuyStop(401, symbol.id, 18'700, 10, 100));
    matching::OrderNode buy_first(
        matching::Order::TrailingBuyStop(402, symbol.id, 18'600, 20, 100));
    matching::OrderNode buy_last(
        matching::Order::TrailingBuyStop(403, symbol.id, 18'800, 30, 100));
    matching::OrderNode sell_middle(
        matching::Order::TrailingSellStop(404, symbol.id, 18'300, 40, 100));
    matching::OrderNode sell_first(
        matching::Order::TrailingSellStop(405, symbol.id, 18'400, 50, 100));
    matching::OrderNode sell_last(
        matching::Order::TrailingSellStop(406, symbol.id, 18'200, 60, 100));
    TrailingStopOrderCleanup cleanup(order_book,
                                     {&buy_middle,
                                      &buy_first,
                                      &buy_last,
                                      &sell_middle,
                                      &sell_first,
                                      &sell_last});

    REQUIRE(TestAccess::add_trailing_stop_order(order_book, buy_middle));
    REQUIRE(TestAccess::add_trailing_stop_order(order_book, buy_first));
    REQUIRE(TestAccess::add_trailing_stop_order(order_book, buy_last));
    REQUIRE(TestAccess::add_trailing_stop_order(order_book, sell_middle));
    REQUIRE(TestAccess::add_trailing_stop_order(order_book, sell_first));
    REQUIRE(TestAccess::add_trailing_stop_order(order_book, sell_last));
    validate_book(order_book);

    REQUIRE(order_book.level_count() == 6);
    REQUIRE(order_book.best_trailing_buy_stop()->price == 18'600);
    REQUIRE(order_book.best_trailing_sell_stop()->price == 18'400);
    REQUIRE(buy_first.level->type == matching::LevelType::ASK);
    REQUIRE(sell_first.level->type == matching::LevelType::BID);

    std::vector<matching::Price> buy_prices;
    for (const matching::LevelNode& level : order_book.trailing_buy_stops())
        buy_prices.push_back(level.price);

    std::vector<matching::Price> sell_prices;
    for (const matching::LevelNode& level : order_book.trailing_sell_stops())
        sell_prices.push_back(level.price);

    REQUIRE(buy_prices == std::vector<matching::Price>{18'600, 18'700, 18'800});
    REQUIRE(sell_prices == std::vector<matching::Price>{18'200, 18'300, 18'400});
    REQUIRE(TestAccess::next_trailing_stop_level(order_book, *buy_first.level) ==
            buy_middle.level);
    REQUIRE(TestAccess::next_trailing_stop_level(order_book, *buy_middle.level) ==
            buy_last.level);
    REQUIRE(TestAccess::next_trailing_stop_level(order_book, *buy_last.level) == nullptr);
    REQUIRE(TestAccess::next_trailing_stop_level(order_book, *sell_first.level) ==
            sell_middle.level);
    REQUIRE(TestAccess::next_trailing_stop_level(order_book, *sell_middle.level) ==
            sell_last.level);
    REQUIRE(TestAccess::next_trailing_stop_level(order_book, *sell_last.level) == nullptr);

    TestAccess::delete_trailing_stop_order(order_book, buy_first);
    TestAccess::delete_trailing_stop_order(order_book, sell_first);
    REQUIRE(order_book.best_trailing_buy_stop()->price == 18'700);
    REQUIRE(order_book.best_trailing_sell_stop()->price == 18'300);
    validate_book(order_book);

    TestAccess::delete_trailing_stop_order(order_book, buy_middle);
    TestAccess::delete_trailing_stop_order(order_book, buy_last);
    TestAccess::delete_trailing_stop_order(order_book, sell_middle);
    TestAccess::delete_trailing_stop_order(order_book, sell_last);
    REQUIRE(order_book.empty());
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}

TEST_CASE("Trailing repricing loses FIFO priority at the destination trigger",
          "[order_book][trailing_stop]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode waiting(
        matching::Order::TrailingBuyStop(501, symbol.id, 18'500, 20, 100));
    matching::OrderNode repriced(
        matching::Order::TrailingBuyStop(502, symbol.id, 18'600, 30, 100));
    TrailingStopOrderCleanup cleanup(order_book, {&waiting, &repriced});

    REQUIRE(TestAccess::add_trailing_stop_order(order_book, waiting));
    REQUIRE(TestAccess::add_trailing_stop_order(order_book, repriced));
    REQUIRE(level_pool.live_count() == 2);

    TestAccess::delete_trailing_stop_order(order_book, repriced);
    repriced.stop_price = waiting.stop_price;
    REQUIRE(TestAccess::add_trailing_stop_order(order_book, repriced));

    matching::LevelNode* level = waiting.level;
    REQUIRE(repriced.level == level);
    REQUIRE(level->orders.front() == &waiting);
    REQUIRE(level->orders.back() == &repriced);
    REQUIRE(waiting.next == &repriced);
    REQUIRE(repriced.previous == &waiting);
    REQUIRE(level->total_quantity == 50);
    REQUIRE(level->order_count == 2);
    REQUIRE(level_pool.live_count() == 1);
    validate_book(order_book);

    TestAccess::reduce_trailing_stop_order(order_book, repriced, 10);
    REQUIRE(repriced.quantity == 20);
    REQUIRE(level->total_quantity == 40);
    REQUIRE(level->order_count == 2);

    TestAccess::reduce_trailing_stop_order(order_book, repriced, 20);
    REQUIRE(repriced.quantity == 0);
    REQUIRE(repriced.level == nullptr);
    REQUIRE(level->orders.front() == &waiting);
    REQUIRE(level->orders.back() == &waiting);
    REQUIRE(level->total_quantity == 20);
    REQUIRE(level->order_count == 1);
    validate_book(order_book);

    TestAccess::delete_trailing_stop_order(order_book, waiting);
    REQUIRE(order_book.empty());
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}

TEST_CASE("Display aggregates consume hidden reserve before visible leaves",
          "[order_book][display_quantity]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode order(matching::Order::BuyLimit(
        551, symbol.id, 18'500, 100, matching::OrderTimeInForce::GTC, 10));

    const std::optional<matching::LevelUpdate> addition =
        TestAccess::add_order(order_book, order);
    REQUIRE(addition.has_value());
    REQUIRE(addition->level.total_quantity == 100);
    REQUIRE(addition->level.displayed_quantity == 10);
    REQUIRE(addition->level.hidden_quantity() == 90);

    const matching::LevelUpdate hidden_reduction =
        TestAccess::reduce_order(order_book, order, 20);
    REQUIRE(order.quantity == 80);
    REQUIRE(hidden_reduction.level.total_quantity == 80);
    REQUIRE(hidden_reduction.level.displayed_quantity == 10);
    REQUIRE(hidden_reduction.level.hidden_quantity() == 70);

    const matching::LevelUpdate visible_reduction =
        TestAccess::reduce_order(order_book, order, 75);
    REQUIRE(order.quantity == 5);
    REQUIRE(visible_reduction.level.total_quantity == 5);
    REQUIRE(visible_reduction.level.displayed_quantity == 5);
    REQUIRE(visible_reduction.level.hidden_quantity() == 0);

    const matching::LevelUpdate deletion =
        TestAccess::delete_order(order_book, order);
    REQUIRE(deletion.type == matching::UpdateType::DELETE);
    REQUIRE(deletion.level.total_quantity == 0);
    REQUIRE(deletion.level.displayed_quantity == 0);
    REQUIRE(order_book.empty());
    REQUIRE(level_pool.live_count() == 0);
}

TEST_CASE("Market reference prices follow side-specific rules",
          "[order_book][trailing_stop]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode bid(matching::Order::BuyLimit(601, symbol.id, 18'500, 10));
    matching::OrderNode ask(matching::Order::SellLimit(602, symbol.id, 18'550, 10));
    RestingOrderCleanup cleanup(order_book, {&bid, &ask});

    static_cast<void>(add_order(order_book, bid));
    static_cast<void>(add_order(order_book, ask));

    REQUIRE(TestAccess::market_bid_price(order_book) == 18'500);
    REQUIRE(TestAccess::market_ask_price(order_book) == 18'550);
    REQUIRE(TestAccess::market_trailing_bid_price(order_book) == 0);
    REQUIRE(TestAccess::market_trailing_ask_price(order_book) ==
            std::numeric_limits<matching::Price>::max());

    TestAccess::update_matching_price(order_book, bid, 18'525);
    TestAccess::update_matching_price(order_book, ask, 18'530);
    REQUIRE(TestAccess::market_bid_price(order_book) == 18'525);
    REQUIRE(TestAccess::market_ask_price(order_book) == 18'530);

    TestAccess::update_last_price(order_book, bid, 18'510);
    TestAccess::update_last_price(order_book, ask, 18'540);
    REQUIRE(TestAccess::market_trailing_bid_price(order_book) == 18'500);
    REQUIRE(TestAccess::market_trailing_ask_price(order_book) == 18'550);

    TestAccess::reset_matching_price(order_book);
    REQUIRE(TestAccess::market_bid_price(order_book) == 18'500);
    REQUIRE(TestAccess::market_ask_price(order_book) == 18'550);
    validate_book(order_book);
}

TEST_CASE("Trailing price calculation moves only in the favorable direction",
          "[order_book][trailing_stop]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode bid(matching::Order::BuyLimit(701, symbol.id, 18'800, 10));
    matching::OrderNode ask(matching::Order::SellLimit(702, symbol.id, 18'200, 10));
    RestingOrderCleanup cleanup(order_book, {&bid, &ask});
    static_cast<void>(add_order(order_book, bid));
    static_cast<void>(add_order(order_book, ask));

    TestAccess::update_last_price(order_book, bid, 18'700);
    TestAccess::update_last_price(order_book, ask, 18'300);

    matching::Order sell =
        matching::Order::TrailingSellStop(703, symbol.id, 18'400, 10, 100, 25);
    matching::Order buy =
        matching::Order::TrailingBuyStop(704, symbol.id, 18'600, 10, 100, 25);

    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, sell) == 18'600);
    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, buy) == 18'400);

    sell.stop_price = 18'600;
    buy.stop_price = 18'400;
    TestAccess::update_last_price(order_book, bid, 18'650);
    TestAccess::update_last_price(order_book, ask, 18'450);
    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, sell) == 18'600);
    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, buy) == 18'400);

    sell.stop_price = 18'550;
    buy.stop_price = 18'450;
    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, sell) == 18'550);
    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, buy) == 18'450);
}

TEST_CASE("Percentage trailing calculations handle empty and boundary markets",
          "[order_book][trailing_stop]")
{
    matching::memory::ObjectPool<matching::LevelNode> empty_pool;
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    const matching::OrderBook empty_book(empty_pool, symbol);
    const matching::Order empty_buy =
        matching::Order::TrailingBuyStop(801, symbol.id, 21'000, 10, -100, -50);
    const matching::Order empty_sell =
        matching::Order::TrailingSellStop(802, symbol.id, 19'000, 10, -100, -50);

    REQUIRE(TestAccess::calculate_trailing_stop_price(empty_book, empty_buy) == 21'000);
    REQUIRE(TestAccess::calculate_trailing_stop_price(empty_book, empty_sell) == 19'000);

    matching::memory::ObjectPool<matching::LevelNode> level_pool;
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode bid(matching::Order::BuyLimit(803, symbol.id, 20'000, 10));
    matching::OrderNode ask(matching::Order::SellLimit(804, symbol.id, 20'000, 10));
    RestingOrderCleanup cleanup(order_book, {&bid, &ask});
    static_cast<void>(add_order(order_book, bid));
    static_cast<void>(add_order(order_book, ask));
    TestAccess::update_last_price(order_book, bid, 20'000);
    TestAccess::update_last_price(order_book, ask, 20'000);

    matching::Order sell =
        matching::Order::TrailingSellStop(805, symbol.id, 19'000, 10, -100, -50);
    matching::Order buy =
        matching::Order::TrailingBuyStop(806, symbol.id, 21'000, 10, -100, -50);
    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, sell) == 19'800);
    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, buy) == 20'200);

    sell.stop_price = 19'750;
    buy.stop_price = 20'250;
    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, sell) == 19'750);
    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, buy) == 20'250);

    TestAccess::update_last_price(
        order_book, ask, std::numeric_limits<matching::Price>::max() - 100);
    matching::Order maximum_percentage = matching::Order::TrailingBuyStop(
        807,
        symbol.id,
        std::numeric_limits<matching::Price>::max() - 1,
        10,
        -10'000,
        0);
    REQUIRE(TestAccess::calculate_trailing_stop_price(order_book, maximum_percentage) ==
            std::numeric_limits<matching::Price>::max() - 1);
}

TEST_CASE("Trailing-stop pool exhaustion leaves indexes unchanged",
          "[order_book][trailing_stop]")
{
    matching::memory::ObjectPool<matching::LevelNode> level_pool(
        sizeof(matching::LevelNode), 1);
    const matching::Symbol symbol(7, {'R', 'E', 'L', 'I', 'A', 'N', 'C', 'E'});
    matching::OrderBook order_book(level_pool, symbol);
    matching::OrderNode first(
        matching::Order::TrailingSellStop(901, symbol.id, 18'400, 10, 100));
    matching::OrderNode second(
        matching::Order::TrailingSellStop(902, symbol.id, 18'300, 20, 100));
    TrailingStopOrderCleanup cleanup(order_book, {&first, &second});

    REQUIRE(TestAccess::add_trailing_stop_order(order_book, first));
    const matching::Level* original_candidate = order_book.best_trailing_sell_stop();
    REQUIRE_FALSE(TestAccess::add_trailing_stop_order(order_book, second));
    REQUIRE(second.level == nullptr);
    REQUIRE(second.previous == nullptr);
    REQUIRE(second.next == nullptr);
    REQUIRE(order_book.best_trailing_sell_stop() == original_candidate);
    REQUIRE(order_book.find_trailing_sell_stop(second.stop_price) == nullptr);
    REQUIRE(level_pool.live_count() == 1);
    validate_book(order_book);

    TestAccess::delete_trailing_stop_order(order_book, first);
    REQUIRE(TestAccess::add_trailing_stop_order(order_book, second));
    REQUIRE(order_book.best_trailing_sell_stop()->price == second.stop_price);
    REQUIRE(level_pool.live_count() == 1);

    TestAccess::delete_trailing_stop_order(order_book, second);
    REQUIRE(order_book.empty());
    REQUIRE(level_pool.live_count() == 0);
    validate_book(order_book);
}
