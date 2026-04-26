#include "order_book.hpp"

#include <cassert>
#include <iostream>

using lob::Order;
using lob::OrderBook;
using lob::OrderId;
using lob::OrderType;
using lob::Price;
using lob::Quantity;
using lob::Side;
using lob::Trade;

void test_add_resting_limit_orders()
{
    OrderBook book;

    const bool ok1 = book.add_order(Order{1, Side::Buy, OrderType::Limit, 100, 50});
    const bool ok2 = book.add_order(Order{2, Side::Sell, OrderType::Limit, 105, 40});

    assert(ok1);
    assert(ok2);

    assert(book.best_bid().has_value());
    assert(*book.best_bid() == 100);

    assert(book.best_ask().has_value());
    assert(*book.best_ask() == 105);

    assert(book.spread().has_value());
    assert(*book.spread() == 5);

    assert(book.total_quantity_at_price(Side::Buy, 100) == 50);
    assert(book.total_quantity_at_price(Side::Sell, 105) == 40);

    assert(book.trades().empty());
}

void test_reject_invalid_orders()
{
    OrderBook book;

    const bool zero_qty = book.add_order(Order{1, Side::Buy, OrderType::Limit, 100, 0});
    const bool bad_price = book.add_order(Order{2, Side::Buy, OrderType::Limit, 0, 10});

    assert(!zero_qty);
    assert(!bad_price);

    assert(!book.best_bid().has_value());
    assert(!book.best_ask().has_value());
    assert(book.trades().empty());
}

void test_duplicate_active_order_id_rejected()
{
    OrderBook book;

    const bool ok1 = book.add_order(Order{1, Side::Buy, OrderType::Limit, 100, 50});
    const bool ok2 = book.add_order(Order{1, Side::Sell, OrderType::Limit, 105, 30});

    assert(ok1);
    assert(!ok2);

    assert(book.best_bid().has_value());
    assert(*book.best_bid() == 100);
    assert(!book.best_ask().has_value());
}

void test_crossing_buy_limit_matches_resting_ask()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Sell, OrderType::Limit, 101, 40}));
    assert(book.add_order(Order{2, Side::Buy, OrderType::Limit, 103, 40}));

    const auto &trades = book.trades();
    assert(trades.size() == 1);

    assert(trades[0].buy_order_id == 2);
    assert(trades[0].sell_order_id == 1);
    assert(trades[0].price == 101);
    assert(trades[0].quantity == 40);

    assert(!book.best_ask().has_value());
    assert(!book.best_bid().has_value());
}

void test_crossing_sell_limit_matches_resting_bid()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Buy, OrderType::Limit, 100, 25}));
    assert(book.add_order(Order{2, Side::Sell, OrderType::Limit, 99, 25}));

    const auto &trades = book.trades();
    assert(trades.size() == 1);

    assert(trades[0].buy_order_id == 1);
    assert(trades[0].sell_order_id == 2);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 25);

    assert(!book.best_bid().has_value());
    assert(!book.best_ask().has_value());
}

void test_partial_fill_incoming_buy_leaves_resting_ask_quantity()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Sell, OrderType::Limit, 101, 100}));
    assert(book.add_order(Order{2, Side::Buy, OrderType::Limit, 101, 40}));

    const auto &trades = book.trades();
    assert(trades.size() == 1);
    assert(trades[0].price == 101);
    assert(trades[0].quantity == 40);

    assert(book.best_ask().has_value());
    assert(*book.best_ask() == 101);
    assert(book.total_quantity_at_price(Side::Sell, 101) == 60);

    assert(!book.best_bid().has_value());
}

void test_partial_fill_incoming_buy_rests_remaining_quantity()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Sell, OrderType::Limit, 101, 40}));
    assert(book.add_order(Order{2, Side::Buy, OrderType::Limit, 101, 100}));

    const auto &trades = book.trades();
    assert(trades.size() == 1);
    assert(trades[0].price == 101);
    assert(trades[0].quantity == 40);

    assert(book.best_bid().has_value());
    assert(*book.best_bid() == 101);
    assert(book.total_quantity_at_price(Side::Buy, 101) == 60);

    assert(!book.best_ask().has_value());
}

void test_fifo_with_same_price_level()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Sell, OrderType::Limit, 101, 30}));
    assert(book.add_order(Order{2, Side::Sell, OrderType::Limit, 101, 50}));
    assert(book.add_order(Order{3, Side::Buy, OrderType::Limit, 101, 60}));

    const auto &trades = book.trades();
    assert(trades.size() == 2);

    assert(trades[0].buy_order_id == 3);
    assert(trades[0].sell_order_id == 1);
    assert(trades[0].price == 101);
    assert(trades[0].quantity == 30);

    assert(trades[1].buy_order_id == 3);
    assert(trades[1].sell_order_id == 2);
    assert(trades[1].price == 101);
    assert(trades[1].quantity == 30);

    assert(book.best_ask().has_value());
    assert(*book.best_ask() == 101);
    assert(book.total_quantity_at_price(Side::Sell, 101) == 20);

    assert(!book.best_bid().has_value());
}

void test_buy_market_order_sweeps_multiple_ask_levels()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Sell, OrderType::Limit, 101, 20}));
    assert(book.add_order(Order{2, Side::Sell, OrderType::Limit, 102, 30}));
    assert(book.add_order(Order{3, Side::Buy, OrderType::Market, 0, 40}));

    const auto &trades = book.trades();
    assert(trades.size() == 2);

    assert(trades[0].buy_order_id == 3);
    assert(trades[0].sell_order_id == 1);
    assert(trades[0].price == 101);
    assert(trades[0].quantity == 20);

    assert(trades[1].buy_order_id == 3);
    assert(trades[1].sell_order_id == 2);
    assert(trades[1].price == 102);
    assert(trades[1].quantity == 20);

    assert(book.best_ask().has_value());
    assert(*book.best_ask() == 102);
    assert(book.total_quantity_at_price(Side::Sell, 102) == 10);

    assert(!book.best_bid().has_value());
}

void test_sell_market_order_does_not_rest_when_unfilled()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Buy, OrderType::Limit, 100, 25}));
    assert(book.add_order(Order{2, Side::Sell, OrderType::Market, 0, 40}));

    const auto &trades = book.trades();
    assert(trades.size() == 1);

    assert(trades[0].buy_order_id == 1);
    assert(trades[0].sell_order_id == 2);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 25);

    assert(!book.best_bid().has_value());
    assert(!book.best_ask().has_value());
}

void test_cancel_existing_order()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Buy, OrderType::Limit, 100, 50}));
    assert(book.add_order(Order{2, Side::Buy, OrderType::Limit, 99, 20}));

    assert(book.cancel_order(1));

    assert(book.best_bid().has_value());
    assert(*book.best_bid() == 99);
    assert(book.total_quantity_at_price(Side::Buy, 100) == 0);
    assert(book.total_quantity_at_price(Side::Buy, 99) == 20);
}

void test_cancel_unknown_order_fails()
{
    OrderBook book;

    assert(!book.cancel_order(999));
}

void test_cancel_removes_empty_price_level()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Sell, OrderType::Limit, 105, 10}));
    assert(book.best_ask().has_value());
    assert(*book.best_ask() == 105);

    assert(book.cancel_order(1));

    assert(!book.best_ask().has_value());
    assert(book.total_quantity_at_price(Side::Sell, 105) == 0);
}

void test_levels_empty_book()
{
    OrderBook book;

    const auto bid_levels = book.levels(Side::Buy, 5);
    const auto ask_levels = book.levels(Side::Sell, 5);

    assert(bid_levels.empty());
    assert(ask_levels.empty());
}

void test_levels_zero_depth()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Buy, OrderType::Limit, 100, 50}));
    assert(book.add_order(Order{2, Side::Sell, OrderType::Limit, 105, 40}));

    const auto bid_levels = book.levels(Side::Buy, 0);
    const auto ask_levels = book.levels(Side::Sell, 0);

    assert(bid_levels.empty());
    assert(ask_levels.empty());
}

void test_bid_levels_are_sorted_and_aggregated()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Buy, OrderType::Limit, 100, 40}));
    assert(book.add_order(Order{2, Side::Buy, OrderType::Limit, 100, 60}));
    assert(book.add_order(Order{3, Side::Buy, OrderType::Limit, 99, 25}));
    assert(book.add_order(Order{4, Side::Buy, OrderType::Limit, 98, 10}));

    const auto bid_levels = book.levels(Side::Buy, 3);

    assert(bid_levels.size() == 3);

    assert(bid_levels[0].price == 100);
    assert(bid_levels[0].total_quantity == 100);

    assert(bid_levels[1].price == 99);
    assert(bid_levels[1].total_quantity == 25);

    assert(bid_levels[2].price == 98);
    assert(bid_levels[2].total_quantity == 10);
}

void test_ask_levels_are_sorted_and_aggregated()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Sell, OrderType::Limit, 101, 20}));
    assert(book.add_order(Order{2, Side::Sell, OrderType::Limit, 101, 30}));
    assert(book.add_order(Order{3, Side::Sell, OrderType::Limit, 102, 40}));
    assert(book.add_order(Order{4, Side::Sell, OrderType::Limit, 103, 15}));

    const auto ask_levels = book.levels(Side::Sell, 3);

    assert(ask_levels.size() == 3);

    assert(ask_levels[0].price == 101);
    assert(ask_levels[0].total_quantity == 50);

    assert(ask_levels[1].price == 102);
    assert(ask_levels[1].total_quantity == 40);

    assert(ask_levels[2].price == 103);
    assert(ask_levels[2].total_quantity == 15);
}

void test_levels_respect_requested_depth()
{
    OrderBook book;

    assert(book.add_order(Order{1, Side::Buy, OrderType::Limit, 100, 10}));
    assert(book.add_order(Order{2, Side::Buy, OrderType::Limit, 99, 20}));
    assert(book.add_order(Order{3, Side::Buy, OrderType::Limit, 98, 30}));

    const auto bid_levels = book.levels(Side::Buy, 2);

    assert(bid_levels.size() == 2);
    assert(bid_levels[0].price == 100);
    assert(bid_levels[1].price == 99);
}

void test_resting_orders_buy_side() {
    OrderBook book;

    assert(book.add_order(Order{1, Side::Buy, OrderType::Limit, 100, 40}));
    assert(book.add_order(Order{2, Side::Buy, OrderType::Limit, 100, 20}));
    assert(book.add_order(Order{3, Side::Buy, OrderType::Limit, 99, 10}));

    const auto orders = book.resting_orders(Side::Buy);

    assert(orders.size() == 3);

    assert(orders[0].id == 1);
    assert(orders[0].price == 100);
    assert(orders[0].quantity == 40);

    assert(orders[1].id == 2);
    assert(orders[1].price == 100);
    assert(orders[1].quantity == 20);

    assert(orders[2].id == 3);
    assert(orders[2].price == 99);
    assert(orders[2].quantity == 10);
}

void test_resting_orders_sell_side() {
    OrderBook book;

    assert(book.add_order(Order{1, Side::Sell, OrderType::Limit, 101, 15}));
    assert(book.add_order(Order{2, Side::Sell, OrderType::Limit, 101, 25}));
    assert(book.add_order(Order{3, Side::Sell, OrderType::Limit, 102, 30}));

    const auto orders = book.resting_orders(Side::Sell);

    assert(orders.size() == 3);

    assert(orders[0].id == 1);
    assert(orders[0].price == 101);
    assert(orders[0].quantity == 15);

    assert(orders[1].id == 2);
    assert(orders[1].price == 101);
    assert(orders[1].quantity == 25);

    assert(orders[2].id == 3);
    assert(orders[2].price == 102);
    assert(orders[2].quantity == 30);
}

void test_resting_orders_after_partial_fill() {
    OrderBook book;

    assert(book.add_order(Order{1, Side::Sell, OrderType::Limit, 101, 40}));
    assert(book.add_order(Order{2, Side::Sell, OrderType::Limit, 101, 20}));
    assert(book.add_order(Order{3, Side::Buy, OrderType::Limit, 101, 50}));

    const auto orders = book.resting_orders(Side::Sell);

    assert(orders.size() == 1);
    assert(orders[0].id == 2);
    assert(orders[0].price == 101);
    assert(orders[0].quantity == 10);
}

int main()
{
    test_add_resting_limit_orders();
    test_reject_invalid_orders();
    test_duplicate_active_order_id_rejected();
    test_crossing_buy_limit_matches_resting_ask();
    test_crossing_sell_limit_matches_resting_bid();
    test_partial_fill_incoming_buy_leaves_resting_ask_quantity();
    test_partial_fill_incoming_buy_rests_remaining_quantity();
    test_fifo_with_same_price_level();
    test_buy_market_order_sweeps_multiple_ask_levels();
    test_sell_market_order_does_not_rest_when_unfilled();
    test_cancel_existing_order();
    test_cancel_unknown_order_fails();
    test_cancel_removes_empty_price_level();
    test_levels_empty_book();
    test_levels_zero_depth();
    test_bid_levels_are_sorted_and_aggregated();
    test_ask_levels_are_sorted_and_aggregated();
    test_levels_respect_requested_depth();
    test_resting_orders_buy_side();
    test_resting_orders_sell_side();
    test_resting_orders_after_partial_fill();

    std::cout << "All tests passed.\n";
    return 0;
}