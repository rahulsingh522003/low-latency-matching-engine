#include "order_book.hpp"

#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

using lob::BookLevel;
using lob::Order;
using lob::OrderBook;
using lob::OrderType;
using lob::RestingOrderView;
using lob::Side;
using lob::Trade;

std::string side_to_string(Side side) {
    return (side == Side::Buy) ? "Buy" : "Sell";
}

std::string order_type_to_string(OrderType type) {
    return (type == OrderType::Limit) ? "Limit" : "Market";
}

std::string optional_price_to_string(const std::optional<lob::Price>& price) {
    if (price.has_value()) {
        return std::to_string(*price);
    }
    return "none";
}

long long read_integer_line(const std::string& prompt) {
    while (true) {
        std::cout << prompt;

        std::string line;
        if (!std::getline(std::cin >> std::ws, line)) {
            std::cin.clear();
            std::cout << "Input error. Please try again.\n";
            continue;
        }

        std::stringstream ss(line);
        long long value;
        char extra;

        if ((ss >> value) && !(ss >> extra)) {
            return value;
        }

        std::cout << "Invalid input. Please enter a valid integer.\n";
    }
}

Side read_side() {
    while (true) {
        const long long side_input = read_integer_line("Enter side (0 = Buy, 1 = Sell): ");

        if (side_input == 0) {
            return Side::Buy;
        }
        if (side_input == 1) {
            return Side::Sell;
        }

        std::cout << "Invalid side. Please enter 0 or 1.\n";
    }
}

lob::Price read_price() {
    while (true) {
        const long long value = read_integer_line("Enter price: ");

        if (value > 0) {
            return static_cast<lob::Price>(value);
        }

        std::cout << "Invalid price. Please enter a positive integer.\n";
    }
}

lob::Quantity read_quantity() {
    while (true) {
        const long long value = read_integer_line("Enter quantity: ");

        if (value > 0) {
            return static_cast<lob::Quantity>(value);
        }

        std::cout << "Invalid quantity. Please enter a positive integer.\n";
    }
}

lob::OrderId read_order_id(const std::string& prompt) {
    while (true) {
        const long long value = read_integer_line(prompt);

        if (value > 0) {
            return static_cast<lob::OrderId>(value);
        }

        std::cout << "Invalid order id. Please enter a positive integer.\n";
    }
}

void print_trade(const Trade& trade) {
    std::cout << "Trade: "
              << "buy_id=" << trade.buy_order_id
              << " sell_id=" << trade.sell_order_id
              << " price=" << trade.price
              << " qty=" << trade.quantity
              << "\n";
}

void print_trade_history(const OrderBook& book) {
    const auto& trades = book.trades();

    std::cout << "\nTrade History:\n";

    if (trades.empty()) {
        std::cout << "  <empty>\n";
        return;
    }

    std::cout << std::left
              << std::setw(12) << "Buy ID"
              << std::setw(12) << "Sell ID"
              << std::setw(12) << "Price"
              << std::setw(12) << "Quantity"
              << "\n";

    std::cout << std::string(48, '-') << "\n";

    for (const auto& trade : trades) {
        std::cout << std::left
                  << std::setw(12) << trade.buy_order_id
                  << std::setw(12) << trade.sell_order_id
                  << std::setw(12) << trade.price
                  << std::setw(12) << trade.quantity
                  << "\n";
    }
}

void print_levels(const OrderBook& book, Side side, std::size_t depth) {
    const auto levels = book.levels(side, depth);

    std::cout << (side == Side::Buy ? "Bids" : "Asks") << ":\n";

    if (levels.empty()) {
        std::cout << "  <empty>\n";
        return;
    }

    for (const auto& level : levels) {
        std::cout << "  price=" << level.price
                  << " qty=" << level.total_quantity
                  << "\n";
    }
}

void print_orders_table(const OrderBook& book, Side side) {
    const auto orders = book.resting_orders(side);

    std::cout << "\n" << side_to_string(side) << " Orders:\n";

    if (orders.empty()) {
        std::cout << "  <empty>\n";
        return;
    }

    std::cout << std::left
              << std::setw(12) << "Order ID"
              << std::setw(12) << "Price"
              << std::setw(12) << "Quantity"
              << "\n";

    std::cout << std::string(36, '-') << "\n";

    for (const auto& order : orders) {
        std::cout << std::left
                  << std::setw(12) << order.id
                  << std::setw(12) << order.price
                  << std::setw(12) << order.quantity
                  << "\n";
    }
}

void print_top_of_book(const OrderBook& book) {
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

void print_new_trades(const OrderBook& book, std::size_t& trade_cursor) {
    const auto& trades = book.trades();

    if (trade_cursor >= trades.size()) {
        std::cout << "No new trades.\n";
        return;
    }

    for (std::size_t i = trade_cursor; i < trades.size(); ++i) {
        print_trade(trades[i]);
    }

    trade_cursor = trades.size();
}

void add_limit_order(OrderBook& book, std::size_t& trade_cursor, lob::OrderId& next_order_id) {
    std::cout << "\n--- Add Limit Order ---\n";

    const Side side = read_side();
    const lob::Price price = read_price();
    const lob::Quantity qty = read_quantity();
    const lob::OrderId id = next_order_id++;

    const Order order{id, side, OrderType::Limit, price, qty};

    std::cout << "Assigned order id: " << id << "\n";

    const bool accepted = book.add_order(order);
    std::cout << "Accepted: " << (accepted ? "true" : "false") << "\n";

    if (accepted) {
        print_new_trades(book, trade_cursor);
    }

    print_top_of_book(book);
}

void add_market_order(OrderBook& book, std::size_t& trade_cursor, lob::OrderId& next_order_id) {
    std::cout << "\n--- Add Market Order ---\n";

    const Side side = read_side();
    const lob::Quantity qty = read_quantity();
    const lob::OrderId id = next_order_id++;

    const Order order{id, side, OrderType::Market, 0, qty};

    std::cout << "Assigned order id: " << id << "\n";

    const bool accepted = book.add_order(order);
    std::cout << "Accepted: " << (accepted ? "true" : "false") << "\n";

    if (accepted) {
        const std::size_t old_trade_cursor = trade_cursor;
        print_new_trades(book, trade_cursor);

        if (old_trade_cursor == trade_cursor) {
            std::cout << "Order accepted, but no matching liquidity was available.\n";
        }
    }

    print_top_of_book(book);
}

void cancel_order_interactive(OrderBook& book) {
    std::cout << "\n--- Cancel Order ---\n";

    const lob::OrderId id = read_order_id("Enter order id to cancel: ");
    const bool cancelled = book.cancel_order(id);

    std::cout << "Cancel result: " << (cancelled ? "success" : "failed") << "\n";
    print_top_of_book(book);
}

int main() {
    OrderBook book;
    std::size_t trade_cursor = 0;
    lob::OrderId next_order_id = 1;

    std::cout << "=== Interactive Limit Order Book ===\n";

    while (true) {
        std::cout << "\nChoose an action:\n";
        std::cout << "1. Add limit order\n";
        std::cout << "2. Add market order\n";
        std::cout << "3. Cancel order\n";
        std::cout << "4. Show top of book\n";
        std::cout << "5. Show all buy orders\n";
        std::cout << "6. Show all sell orders\n";
        std::cout << "7. Show trade history\n";
        std::cout << "8. Exit\n";

        const long long choice = read_integer_line("Enter choice: ");

        switch (choice) {
            case 1:
                add_limit_order(book, trade_cursor, next_order_id);
                break;
            case 2:
                add_market_order(book, trade_cursor, next_order_id);
                break;
            case 3:
                cancel_order_interactive(book);
                break;
            case 4:
                print_top_of_book(book);
                break;
            case 5:
                print_orders_table(book, Side::Buy);
                break;
            case 6:
                print_orders_table(book, Side::Sell);
                break;
            case 7:
                print_trade_history(book);
                break;
            case 8:
                std::cout << "Exiting interactive demo.\n";
                return 0;
            default:
                std::cout << "Invalid choice. Please try again.\n";
                break;
        }
    }
}