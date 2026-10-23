#include "matching/level.h"
#include "matching/manager/market_manager.h"
#include "matching/memory/object_pool.h"
#include "matching/order.h"
#include "matching/book/order_book.h"
#include "matching/symbol.h"

#include <cstddef>
#include <iostream>
#include <string_view>

namespace {

template <typename T>
void report_type(std::string_view name)
{
    matching::memory::ObjectPool<T> pool;
    std::cout << name << ',' << sizeof(T) << ',' << alignof(T) << ','
              << pool.page_size() << ',' << pool.slots_per_page() << ','
              << pool.capacity() << '\n';
}

} // namespace

int main()
{
    std::cout << "type,sizeof_bytes,alignof_bytes,default_page_bytes,"
                 "slots_per_page,initial_capacity\n";
    report_type<matching::OrderNode>("OrderNode");
    report_type<matching::LevelNode>("LevelNode");
    report_type<matching::Symbol>("Symbol");
    report_type<matching::OrderBook>("OrderBook");
    std::cout << "Order," << sizeof(matching::Order) << ','
              << alignof(matching::Order) << ",0,0,0\n";
    std::cout << "Level," << sizeof(matching::Level) << ','
              << alignof(matching::Level) << ",0,0,0\n";
    std::cout << "MarketManager," << sizeof(matching::MarketManager) << ','
              << alignof(matching::MarketManager) << ",0,0,0\n";
    return 0;
}
