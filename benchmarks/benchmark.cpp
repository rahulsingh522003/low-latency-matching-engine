#include "order_book.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

using lob::Order;
using lob::OrderBook;
using lob::OrderType;
using lob::Side;

int main()
{
    OrderBook book;

    const std::size_t num_orders = 1'000'000;

    std::mt19937 rng(42);

    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> type_dist(0, 9); // ~70% limit, 30% market
    std::uniform_int_distribution<int> price_dist(95, 105);
    std::uniform_int_distribution<int> qty_dist(1, 100);

    std::vector<Order> orders;
    orders.reserve(num_orders);

    for (std::size_t i = 0; i < num_orders; ++i)
    {
        const Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        const OrderType type = (type_dist(rng) < 7) ? OrderType::Limit : OrderType::Market;
        const lob::Price price = (type == OrderType::Limit) ? price_dist(rng) : 0;
        const lob::Quantity qty = qty_dist(rng);

        orders.push_back(Order{
            static_cast<lob::OrderId>(i + 1),
            side,
            type,
            price,
            qty});
    }

    std::cout << "Running benchmark with " << num_orders << " orders...\n";

    const auto start = std::chrono::high_resolution_clock::now();

    for (const auto &order : orders)
    {
        book.add_order(order);
    }

    const auto end = std::chrono::high_resolution_clock::now();

    const double seconds =
        std::chrono::duration<double>(end - start).count();
    const double throughput = static_cast<double>(num_orders) / seconds;

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Time taken: " << seconds << " seconds\n";

    std::cout << std::fixed << std::setprecision(0);
    std::cout << "Throughput: " << throughput << " orders/sec\n";

    return 0;
}