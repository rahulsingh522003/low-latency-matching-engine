#include "order_book.hpp"

#include <iostream>
#include <optional>
#include <string>

using lob::Order;
using lob::OrderBook;
using lob::OrderType;
using lob::Side;
using lob::Trade;

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
    std::cout << "Submitting order: "
              << side_to_string(order.side) << " "
              << order_type_to_string(order.type)
              << " | id=" << order.id
              << " price=" << order.price
              << " qty=" << order.quantity
              << "\n";
}

void print_trade(const Trade &trade)
{
    std::cout << "Trade: "
              << "buy_id=" << trade.buy_order_id
              << " sell_id=" << trade.sell_order_id
              << " price=" << trade.price
              << " qty=" << trade.quantity
              << "\n";
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

    for (const auto &level : levels)
    {
        std::cout << "  price=" << level.price
                  << " qty=" << level.total_quantity
                  << "\n";
    }
}

void print_top_of_book(const OrderBook &book)
{
    std::cout << "Top of Book:\n";
    std::cout << "  Best Bid: " << optional_price_to_string(book.best_bid()) << "\n";
    std::cout << "  Best Ask: " << optional_price_to_string(book.best_ask()) << "\n";

    const auto spread = book.spread();
    std::cout << "  Spread  : "
              << (spread.has_value() ? std::to_string(*spread) : "none")
              << "\n";

    std::cout << "\nBook Depth (top 5):\n";
    print_levels(book, Side::Buy, 5);
    print_levels(book, Side::Sell, 5);
}

void print_new_trades(const OrderBook &book, std::size_t start_index)
{
    const auto &trades = book.trades();
    if (start_index >= trades.size())
    {
        std::cout << "No new trades.\n";
        return;
    }

    for (std::size_t i = start_index; i < trades.size(); ++i)
    {
        print_trade(trades[i]);
    }
}

void submit_and_report(OrderBook &book, const Order &order, std::size_t &trade_cursor)
{
    print_order(order);

    const bool accepted = book.add_order(order);
    std::cout << "Accepted: " << (accepted ? "true" : "false") << "\n";

    if (accepted)
    {
        print_new_trades(book, trade_cursor);
        trade_cursor = book.trades().size();
    }
    else
    {
        std::cout << "Order rejected.\n";
    }

    print_top_of_book(book);
    std::cout << "\n";
}

int main()
{
    OrderBook book;

    std::cout << "=== Limit Order Book Demo ===\n\n";

    std::size_t trade_cursor = 0;

    submit_and_report(book, Order{1, Side::Buy, OrderType::Limit, 100, 50}, trade_cursor);
    submit_and_report(book, Order{2, Side::Buy, OrderType::Limit, 99, 40}, trade_cursor);
    submit_and_report(book, Order{3, Side::Sell, OrderType::Limit, 102, 35}, trade_cursor);
    submit_and_report(book, Order{4, Side::Sell, OrderType::Limit, 101, 20}, trade_cursor);

    std::cout << "=== Crossing buy limit order ===\n";
    submit_and_report(book, Order{5, Side::Buy, OrderType::Limit, 102, 30}, trade_cursor);

    std::cout << "=== Sell market order ===\n";
    submit_and_report(book, Order{6, Side::Sell, OrderType::Market, 0, 70}, trade_cursor);

    std::cout << "=== Demo complete ===\n";
    return 0;
}