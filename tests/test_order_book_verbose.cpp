#include "order_book.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using lob::Order;
using lob::OrderBook;
using lob::OrderType;
using lob::Side;
using lob::Trade;

struct TestStats
{
    int passed = 0;
    int failed = 0;
};

std::string side_to_string(Side side)
{
    return (side == Side::Buy) ? "Buy" : "Sell";
}

std::string order_type_to_string(OrderType type)
{
    return (type == OrderType::Limit) ? "Limit" : "Market";
}

std::string optional_price_to_string(const std::optional<lob::Price> &price)
{
    if (price.has_value())
    {
        return std::to_string(*price);
    }
    return "none";
}

void print_order(const Order &order)
{
    std::cout << "  Add "
              << side_to_string(order.side) << " "
              << order_type_to_string(order.type)
              << ": id=" << order.id
              << " price=" << order.price
              << " qty=" << order.quantity
              << "\n";
}

void print_trade(const Trade &trade, std::size_t index)
{
    std::cout << "  Trade[" << index << "]"
              << " buy_id=" << trade.buy_order_id
              << " sell_id=" << trade.sell_order_id
              << " price=" << trade.price
              << " qty=" << trade.quantity
              << "\n";
}

bool check(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cout << "    [FAIL] " << message << "\n";
        return false;
    }
    std::cout << "    [PASS] " << message << "\n";
    return true;
}

bool test_crossing_buy_limit_matches_resting_ask()
{
    std::cout << "\n[TEST] Crossing buy limit matches resting ask\n";
    std::cout << "Input:\n";

    OrderBook book;

    Order sell{1, Side::Sell, OrderType::Limit, 101, 40};
    Order buy{2, Side::Buy, OrderType::Limit, 103, 40};

    print_order(sell);
    print_order(buy);

    book.add_order(sell);
    book.add_order(buy);

    std::cout << "Expected:\n";
    std::cout << "  1 trade\n";
    std::cout << "  Trade price = 101\n";
    std::cout << "  Trade qty   = 40\n";
    std::cout << "  Best bid    = none\n";
    std::cout << "  Best ask    = none\n";

    std::cout << "Actual:\n";
    std::cout << "  Number of trades = " << book.trades().size() << "\n";
    for (std::size_t i = 0; i < book.trades().size(); ++i)
    {
        print_trade(book.trades()[i], i);
    }
    std::cout << "  Best bid = " << optional_price_to_string(book.best_bid()) << "\n";
    std::cout << "  Best ask = " << optional_price_to_string(book.best_ask()) << "\n";

    bool ok = true;
    ok &= check(book.trades().size() == 1, "Exactly one trade should be generated");
    ok &= check(book.trades()[0].buy_order_id == 2, "Buyer should be incoming order id=2");
    ok &= check(book.trades()[0].sell_order_id == 1, "Seller should be resting order id=1");
    ok &= check(book.trades()[0].price == 101, "Trade price should be 101");
    ok &= check(book.trades()[0].quantity == 40, "Trade quantity should be 40");
    ok &= check(!book.best_bid().has_value(), "Best bid should be empty");
    ok &= check(!book.best_ask().has_value(), "Best ask should be empty");

    std::cout << "Result: " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

bool test_fifo_with_same_price_level()
{
    std::cout << "\n[TEST] FIFO at same price level\n";
    std::cout << "Input:\n";

    OrderBook book;

    Order sell1{1, Side::Sell, OrderType::Limit, 101, 30};
    Order sell2{2, Side::Sell, OrderType::Limit, 101, 50};
    Order buy{3, Side::Buy, OrderType::Limit, 101, 60};

    print_order(sell1);
    print_order(sell2);
    print_order(buy);

    book.add_order(sell1);
    book.add_order(sell2);
    book.add_order(buy);

    std::cout << "Expected:\n";
    std::cout << "  2 trades\n";
    std::cout << "  First fills order 1 for 30\n";
    std::cout << "  Second fills order 2 for 30\n";
    std::cout << "  Remaining ask qty at 101 = 20\n";

    std::cout << "Actual:\n";
    std::cout << "  Number of trades = " << book.trades().size() << "\n";
    for (std::size_t i = 0; i < book.trades().size(); ++i)
    {
        print_trade(book.trades()[i], i);
    }
    std::cout << "  Remaining ask qty at 101 = "
              << book.total_quantity_at_price(Side::Sell, 101) << "\n";

    bool ok = true;
    ok &= check(book.trades().size() == 2, "Exactly two trades should be generated");
    ok &= check(book.trades()[0].sell_order_id == 1, "First trade should hit order 1 first");
    ok &= check(book.trades()[0].quantity == 30, "First trade quantity should be 30");
    ok &= check(book.trades()[1].sell_order_id == 2, "Second trade should hit order 2 second");
    ok &= check(book.trades()[1].quantity == 30, "Second trade quantity should be 30");
    ok &= check(book.total_quantity_at_price(Side::Sell, 101) == 20,
                "Remaining ask quantity at 101 should be 20");

    std::cout << "Result: " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

bool test_cancel_existing_order()
{
    std::cout << "\n[TEST] Cancel existing order\n";
    std::cout << "Input:\n";

    OrderBook book;

    Order buy1{1, Side::Buy, OrderType::Limit, 100, 50};
    Order buy2{2, Side::Buy, OrderType::Limit, 99, 20};

    print_order(buy1);
    print_order(buy2);
    std::cout << "  Cancel order id=1\n";

    book.add_order(buy1);
    book.add_order(buy2);
    const bool cancel_ok = book.cancel_order(1);

    std::cout << "Expected:\n";
    std::cout << "  Cancel succeeds\n";
    std::cout << "  Best bid = 99\n";
    std::cout << "  Qty at 100 = 0\n";
    std::cout << "  Qty at 99 = 20\n";

    std::cout << "Actual:\n";
    std::cout << "  Cancel returned = " << (cancel_ok ? "true" : "false") << "\n";
    std::cout << "  Best bid = " << optional_price_to_string(book.best_bid()) << "\n";
    std::cout << "  Qty at 100 = " << book.total_quantity_at_price(Side::Buy, 100) << "\n";
    std::cout << "  Qty at 99 = " << book.total_quantity_at_price(Side::Buy, 99) << "\n";

    bool ok = true;
    ok &= check(cancel_ok, "Cancel should succeed");
    ok &= check(book.best_bid().has_value() && *book.best_bid() == 99,
                "Best bid should become 99");
    ok &= check(book.total_quantity_at_price(Side::Buy, 100) == 0,
                "Quantity at 100 should become 0");
    ok &= check(book.total_quantity_at_price(Side::Buy, 99) == 20,
                "Quantity at 99 should remain 20");

    std::cout << "Result: " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

void run_test(bool (*test_fn)(), const std::string &name, TestStats &stats)
{
    const bool ok = test_fn();
    if (ok)
    {
        ++stats.passed;
    }
    else
    {
        ++stats.failed;
        std::cout << "[SUMMARY] Test failed: " << name << "\n";
    }
}

void print_levels(const OrderBook &book, Side side, std::size_t depth)
{
    const auto levels = book.levels(side, depth);

    std::cout << (side == Side::Buy ? "Bids" : "Asks") << ":\n";

    if (levels.empty())
    {
        std::cout << "  <empty>\n";
        return;
    }

    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        std::cout << "  Level[" << i << "]"
                  << " price=" << levels[i].price
                  << " qty=" << levels[i].total_quantity
                  << "\n";
    }
}

bool test_levels_are_sorted_and_aggregated_verbose()
{
    std::cout << "\n[TEST] Levels API returns sorted aggregated depth\n";
    std::cout << "Input:\n";

    OrderBook book;

    Order b1{1, Side::Buy, OrderType::Limit, 100, 40};
    Order b2{2, Side::Buy, OrderType::Limit, 100, 60};
    Order b3{3, Side::Buy, OrderType::Limit, 99, 25};
    Order a1{4, Side::Sell, OrderType::Limit, 101, 20};
    Order a2{5, Side::Sell, OrderType::Limit, 101, 30};
    Order a3{6, Side::Sell, OrderType::Limit, 102, 15};

    print_order(b1);
    print_order(b2);
    print_order(b3);
    print_order(a1);
    print_order(a2);
    print_order(a3);

    book.add_order(b1);
    book.add_order(b2);
    book.add_order(b3);
    book.add_order(a1);
    book.add_order(a2);
    book.add_order(a3);

    std::cout << "Expected:\n";
    std::cout << "  Bid levels:\n";
    std::cout << "    100 -> 100\n";
    std::cout << "    99  -> 25\n";
    std::cout << "  Ask levels:\n";
    std::cout << "    101 -> 50\n";
    std::cout << "    102 -> 15\n";

    const auto bid_levels = book.levels(Side::Buy, 5);
    const auto ask_levels = book.levels(Side::Sell, 5);

    std::cout << "Actual:\n";
    print_levels(book, Side::Buy, 5);
    print_levels(book, Side::Sell, 5);

    bool ok = true;
    ok &= check(bid_levels.size() == 2, "Bid side should return 2 levels");
    ok &= check(bid_levels[0].price == 100 && bid_levels[0].total_quantity == 100,
                "Best bid level should be 100 -> 100");
    ok &= check(bid_levels[1].price == 99 && bid_levels[1].total_quantity == 25,
                "Second bid level should be 99 -> 25");

    ok &= check(ask_levels.size() == 2, "Ask side should return 2 levels");
    ok &= check(ask_levels[0].price == 101 && ask_levels[0].total_quantity == 50,
                "Best ask level should be 101 -> 50");
    ok &= check(ask_levels[1].price == 102 && ask_levels[1].total_quantity == 15,
                "Second ask level should be 102 -> 15");

    std::cout << "Result: " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

bool test_resting_orders_verbose() {
    std::cout << "\n[TEST] Resting orders API returns order IDs, prices, and quantities in book order\n";
    std::cout << "Input:\n";

    OrderBook book;

    Order b1{1, Side::Buy, OrderType::Limit, 100, 40};
    Order b2{2, Side::Buy, OrderType::Limit, 100, 20};
    Order b3{3, Side::Buy, OrderType::Limit, 99, 10};

    print_order(b1);
    print_order(b2);
    print_order(b3);

    book.add_order(b1);
    book.add_order(b2);
    book.add_order(b3);

    const auto orders = book.resting_orders(Side::Buy);

    std::cout << "Expected:\n";
    std::cout << "  Order[0] -> id=1 price=100 qty=40\n";
    std::cout << "  Order[1] -> id=2 price=100 qty=20\n";
    std::cout << "  Order[2] -> id=3 price=99 qty=10\n";

    std::cout << "Actual:\n";
    for (std::size_t i = 0; i < orders.size(); ++i) {
        std::cout << "  Order[" << i << "] -> id=" << orders[i].id
                  << " price=" << orders[i].price
                  << " qty=" << orders[i].quantity
                  << "\n";
    }

    bool ok = true;
    ok &= check(orders.size() == 3, "Buy side should return 3 resting orders");
    ok &= check(orders[0].id == 1 && orders[0].price == 100 && orders[0].quantity == 40,
                "First order should be id=1 price=100 qty=40");
    ok &= check(orders[1].id == 2 && orders[1].price == 100 && orders[1].quantity == 20,
                "Second order should be id=2 price=100 qty=20");
    ok &= check(orders[2].id == 3 && orders[2].price == 99 && orders[2].quantity == 10,
                "Third order should be id=3 price=99 qty=10");

    std::cout << "Result: " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

int main()
{
    TestStats stats;

    run_test(test_crossing_buy_limit_matches_resting_ask,
             "Crossing buy limit matches resting ask", stats);

    run_test(test_fifo_with_same_price_level,
             "FIFO at same price level", stats);

    run_test(test_cancel_existing_order,
             "Cancel existing order", stats);

    run_test(test_levels_are_sorted_and_aggregated_verbose,
             "Levels API returns sorted aggregated depth", stats);

    run_test(test_resting_orders_verbose,
             "Resting orders API returns order IDs, prices, and quantities", stats);

    std::cout << "\n========================================\n";
    std::cout << "Tests passed: " << stats.passed << "\n";
    std::cout << "Tests failed: " << stats.failed << "\n";
    std::cout << "========================================\n";

    return (stats.failed == 0) ? 0 : 1;
}