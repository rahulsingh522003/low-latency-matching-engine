#include "matching/level.h"
#include "matching/memory/object_pool.h"

#include <catch_amalgamated.hpp>

#include <sstream>
#include <type_traits>

namespace {

void append_order(matching::LevelNode& level, matching::OrderNode& order)
{
    REQUIRE(order.level == nullptr);
    REQUIRE(order.price == level.price);
    REQUIRE(order.is_buy() == level.is_bid());
    REQUIRE(order.is_sell() == level.is_ask());
    REQUIRE(order.is_limit());
    REQUIRE(order.quantity > 0);
    if (level.orders.front() != nullptr)
        REQUIRE(order.symbol_id == level.orders.front()->symbol_id);

    level.orders.push_back(order);
    order.level = &level;
    level.total_quantity += order.quantity;
    level.displayed_quantity += order.displayed_quantity();
    ++level.order_count;
}

void reduce_order(matching::LevelNode& level,
                  matching::OrderNode& order,
                  matching::Quantity reduction)
{
    REQUIRE(order.level == &level);
    REQUIRE(reduction > 0);
    REQUIRE(reduction <= order.quantity);
    REQUIRE(reduction <= level.total_quantity);

    const matching::Quantity old_displayed = order.displayed_quantity();
    order.quantity -= reduction;
    level.total_quantity -= reduction;
    level.displayed_quantity -= old_displayed - order.displayed_quantity();

    if (order.quantity == 0)
    {
        REQUIRE(level.order_count > 0);
        --level.order_count;
        level.orders.unlink(order);
        order.level = nullptr;
    }
}

void require_level_invariants(const matching::LevelNode& level)
{
    REQUIRE(level.order_count == level.orders.size());
    REQUIRE(level.orders.empty() == (level.order_count == 0));
    REQUIRE((level.total_quantity == 0) == level.orders.empty());
    REQUIRE(level.displayed_quantity <= level.total_quantity);

    matching::Quantity quantity_sum = 0;
    matching::Quantity displayed_sum = 0;
    const matching::OrderNode* first = level.orders.front();
    for (const matching::OrderNode* order = level.orders.front(); order != nullptr;
         order = order->next)
    {
        REQUIRE(order->level == &level);
        REQUIRE(order->price == level.price);
        REQUIRE(order->is_buy() == level.is_bid());
        REQUIRE(order->is_sell() == level.is_ask());
        REQUIRE(order->is_limit());
        REQUIRE(order->quantity > 0);
        REQUIRE(first != nullptr);
        REQUIRE(order->symbol_id == first->symbol_id);
        quantity_sum += order->quantity;
        displayed_sum += order->displayed_quantity();
    }

    REQUIRE(level.total_quantity == quantity_sum);
    REQUIRE(level.displayed_quantity == displayed_sum);
}

} // namespace

static_assert(std::is_copy_constructible_v<matching::Level>);
static_assert(std::is_copy_assignable_v<matching::Level>);
static_assert(std::is_move_constructible_v<matching::Level>);
static_assert(std::is_move_assignable_v<matching::Level>);
static_assert(!std::is_copy_constructible_v<matching::LevelNode>);
static_assert(!std::is_copy_assignable_v<matching::LevelNode>);
static_assert(!std::is_move_constructible_v<matching::LevelNode>);
static_assert(!std::is_move_assignable_v<matching::LevelNode>);

TEST_CASE("A price level starts in a valid empty state", "[matching][level]")
{
    const matching::Level bid(matching::LevelType::BID, 18'525);
    const matching::Level ask(matching::LevelType::ASK, 18'530);

    REQUIRE(bid.is_bid());
    REQUIRE_FALSE(bid.is_ask());
    REQUIRE(bid.price == 18'525);
    REQUIRE(bid.total_quantity == 0);
    REQUIRE(bid.displayed_quantity == 0);
    REQUIRE(bid.order_count == 0);

    REQUIRE(ask.is_ask());
    REQUIRE_FALSE(ask.is_bid());
    REQUIRE(ask.price == 18'530);
    REQUIRE(ask.total_quantity == 0);
    REQUIRE(ask.displayed_quantity == 0);
    REQUIRE(ask.order_count == 0);
}

TEST_CASE("LevelNode connects an AVL price index to a FIFO order queue",
          "[matching][level]")
{
    matching::LevelNode lower(matching::LevelType::BID, 18'520);
    matching::LevelNode level(matching::LevelType::BID, 18'525);
    matching::LevelNode higher(matching::LevelType::BID, 18'530);
    matching::containers::IntrusiveAvlTree<matching::LevelNode> levels;

    REQUIRE(levels.insert(level).second);
    REQUIRE(levels.insert(lower).second);
    REQUIRE(levels.insert(higher).second);
    REQUIRE(levels.minimum() == &lower);
    REQUIRE(levels.maximum() == &higher);

    matching::OrderNode first(
        matching::Order::BuyLimit(1, 7, level.price, 40, matching::OrderTimeInForce::GTC));
    matching::OrderNode second(
        matching::Order::BuyLimit(2, 7, level.price, 60, matching::OrderTimeInForce::GTC));

    append_order(level, first);
    append_order(level, second);

    REQUIRE(level.orders.front() == &first);
    REQUIRE(level.orders.back() == &second);
    REQUIRE(first.next == &second);
    REQUIRE(second.previous == &first);
    require_level_invariants(level);

    reduce_order(level, first, 15);
    REQUIRE(level.orders.front() == &first);
    REQUIRE(first.quantity == 25);
    require_level_invariants(level);

    reduce_order(level, first, 25);
    REQUIRE(level.orders.front() == &second);
    REQUIRE(second.previous == nullptr);
    require_level_invariants(level);

    reduce_order(level, second, 60);
    require_level_invariants(level);

    static_cast<void>(levels.erase(lower));
    static_cast<void>(levels.erase(level));
    static_cast<void>(levels.erase(higher));
}

TEST_CASE("The AVL index treats equal prices as one key", "[matching][level]")
{
    matching::LevelNode stored(matching::LevelType::ASK, 18'530);
    matching::LevelNode duplicate(matching::LevelType::ASK, 18'530);
    matching::containers::IntrusiveAvlTree<matching::LevelNode> levels;

    const auto [first, inserted] = levels.insert(stored);
    const auto [existing, duplicate_inserted] = levels.insert(duplicate);

    REQUIRE(inserted);
    REQUIRE(first == &stored);
    REQUIRE_FALSE(duplicate_inserted);
    REQUIRE(existing == &stored);
    REQUIRE(levels.size() == 1);
    REQUIRE(duplicate.parent == nullptr);
    REQUIRE(duplicate.left == nullptr);
    REQUIRE(duplicate.right == nullptr);

    static_cast<void>(levels.erase(stored));
}

TEST_CASE("LevelUpdate owns a scalar snapshot", "[matching][level]")
{
    matching::LevelNode level(matching::LevelType::BID, 18'525);
    matching::OrderNode order(
        matching::Order::BuyLimit(1, 7, level.price, 100, matching::OrderTimeInForce::GTC));
    append_order(level, order);

    const matching::LevelUpdate update(matching::UpdateType::ADD, level, true);

    level.total_quantity = 75;
    REQUIRE(update.type == matching::UpdateType::ADD);
    REQUIRE(update.level.type == matching::LevelType::BID);
    REQUIRE(update.level.price == 18'525);
    REQUIRE(update.level.total_quantity == 100);
    REQUIRE(update.level.displayed_quantity == 100);
    REQUIRE(update.level.order_count == 1);
    REQUIRE(update.top);

    level.total_quantity = order.quantity;
    reduce_order(level, order, order.quantity);
}

TEST_CASE("A Level snapshot survives release and reuse of its pooled LevelNode",
          "[matching][level]")
{
    matching::memory::ObjectPool<matching::LevelNode> pool(4 * sizeof(matching::LevelNode), 1);
    matching::LevelNode* level = pool.create(matching::LevelType::BID, 18'525);
    REQUIRE(level != nullptr);

    matching::OrderNode order(
        matching::Order::BuyLimit(1, 7, level->price, 100, matching::OrderTimeInForce::GTC));
    append_order(*level, order);
    const matching::LevelUpdate update(matching::UpdateType::ADD, *level, true);

    reduce_order(*level, order, order.quantity);
    pool.release(*level);
    REQUIRE(pool.live_count() == 0);

    matching::LevelNode* reused = pool.create(matching::LevelType::ASK, 18'530);
    REQUIRE(reused == level);
    REQUIRE(update.level.type == matching::LevelType::BID);
    REQUIRE(update.level.price == 18'525);
    REQUIRE(update.level.total_quantity == 100);
    REQUIRE(update.level.displayed_quantity == 100);
    REQUIRE(update.level.order_count == 1);

    pool.release(*reused);
}

TEST_CASE("Price level values have deterministic diagnostic output", "[matching][level]")
{
    matching::Level level(matching::LevelType::ASK, 18'530);
    level.total_quantity = 75;
    level.displayed_quantity = 50;
    level.order_count = 2;
    const matching::LevelUpdate update(matching::UpdateType::UPDATE, level, false);
    std::ostringstream stream;

    stream << update;

    REQUIRE(stream.str() ==
            "LevelUpdate(Type=UPDATE; Level=Level(Type=ASK; Price=18530; "
            "TotalQuantity=75; DisplayedQuantity=50; OrderCount=2); Top=false)");
}
