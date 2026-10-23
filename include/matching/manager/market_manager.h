#ifndef MATCHING_MARKET_MANAGER_H
#define MATCHING_MARKET_MANAGER_H

#include "matching/book/order_book.h"
#include "matching/containers/hash_map.h"
#include "matching/errors.h"
#include "matching/events/market_handler.h"
#include "matching/fast_hash.h"
#include "matching/memory/object_pool.h"
#include "matching/types.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace matching {

namespace detail {
class MarketManagerTestAccess;
}

class MarketManager
{
    friend class detail::MarketManagerTestAccess;

public:
    using ActiveOrders = containers::HashMap<OrderId, OrderNode*, FastHash>;

    static constexpr std::size_t INITIAL_ORDER_BUCKET_COUNT = 16U * 1024U;

    MarketManager();
    explicit MarketManager(MarketHandler& market_handler);
    MarketManager(const MarketManager&) = delete;
    MarketManager(MarketManager&&) = delete;
    ~MarketManager() noexcept;

    MarketManager& operator=(const MarketManager&) = delete;
    MarketManager& operator=(MarketManager&&) = delete;

    [[nodiscard]] std::size_t symbol_count() const noexcept { return symbol_count_; }
    [[nodiscard]] std::size_t order_book_count() const noexcept
    {
        return order_book_count_;
    }
    [[nodiscard]] std::size_t active_order_count() const noexcept
    {
        return active_orders_.size();
    }
    [[nodiscard]] std::size_t active_order_capacity() const noexcept
    {
        return active_orders_.bucket_count() / 2;
    }

    [[nodiscard]] const Symbol* find_symbol(SymbolId symbol_id) const noexcept;
    [[nodiscard]] const OrderBook* find_order_book(SymbolId symbol_id) const noexcept;
    [[nodiscard]] const Order* find_order(OrderId order_id) const noexcept;

    [[nodiscard]] ErrorCode add_symbol(const Symbol& symbol);
    [[nodiscard]] ErrorCode delete_symbol(SymbolId symbol_id) noexcept;

    [[nodiscard]] ErrorCode add_order_book(SymbolId symbol_id);
    [[nodiscard]] ErrorCode delete_order_book(SymbolId symbol_id) noexcept;

    [[nodiscard]] ErrorCode add_order(const Order& order);
    [[nodiscard]] ErrorCode reduce_order(SymbolId symbol_id,
                                         OrderId order_id,
                                         Quantity reduction) noexcept;
    [[nodiscard]] ErrorCode modify_order(SymbolId symbol_id,
                                         OrderId order_id,
                                         Price new_price,
                                         Quantity new_quantity);
    [[nodiscard]] ErrorCode replace_order(SymbolId symbol_id,
                                          OrderId order_id,
                                          OrderId new_order_id,
                                          Price new_price,
                                          Quantity new_quantity);
    [[nodiscard]] ErrorCode replace_order(SymbolId symbol_id,
                                          OrderId order_id,
                                          const Order& replacement);
    [[nodiscard]] ErrorCode delete_order(SymbolId symbol_id,
                                         OrderId order_id) noexcept;
    [[nodiscard]] ErrorCode execute_order(SymbolId symbol_id,
                                          OrderId order_id,
                                          Quantity execution_quantity) noexcept;
    [[nodiscard]] ErrorCode execute_order(SymbolId symbol_id,
                                          OrderId order_id,
                                          Price execution_price,
                                          Quantity execution_quantity) noexcept;

    [[nodiscard]] bool is_matching_enabled() const noexcept
    {
        return matching_enabled_;
    }
    void enable_matching() noexcept;
    void disable_matching() noexcept { matching_enabled_ = false; }
    void match() noexcept;

private:
    enum class RelocationStatus
    {
        READY,
        QUANTITY_OVERFLOW,
        ALLOCATION_FAILURE
    };

    static MarketHandler default_handler_;

    MarketHandler& market_handler_;

    memory::ObjectPool<LevelNode> level_pool_;
    memory::ObjectPool<Symbol> symbol_pool_;
    memory::ObjectPool<OrderBook> order_book_pool_;
    memory::ObjectPool<OrderNode> order_pool_;

    std::vector<Symbol*> symbols_;
    std::vector<OrderBook*> order_books_;
    ActiveOrders active_orders_;

    std::size_t symbol_count_{0};
    std::size_t order_book_count_{0};
    bool matching_enabled_{false};

    [[nodiscard]] OrderBook* find_order_book_mutable(SymbolId symbol_id) noexcept;
    [[nodiscard]] ErrorCode resolve_active_order(SymbolId symbol_id,
                                                 OrderId order_id,
                                                 OrderBook*& order_book,
                                                 OrderNode*& order) noexcept;
    [[nodiscard]] static bool is_supported_passive_order(const Order& order) noexcept;
    [[nodiscard]] static LevelNode* find_order_level(OrderBook& order_book,
                                                     const Order& order) noexcept;
    [[nodiscard]] static const Level* find_order_level(const OrderBook& order_book,
                                                       const Order& order) noexcept;
    [[nodiscard]] static bool can_store_quantity(const OrderBook& order_book,
                                                 const Order& order) noexcept;
    [[nodiscard]] RelocationStatus prepare_new_order_level(
        OrderBook& order_book,
        const Order& order,
        LevelNode*& destination_level) noexcept;
    [[nodiscard]] RelocationStatus prepare_relocation(
        OrderBook& order_book,
        const OrderNode& current_order,
        const Order& replacement,
        LevelNode*& destination_level) noexcept;
    [[nodiscard]] static bool add_to_order_book(
        OrderBook& order_book,
        OrderNode& order,
        std::optional<LevelUpdate>& level_update) noexcept;
    [[nodiscard]] static bool add_to_order_book_at_level(
        OrderBook& order_book,
        OrderNode& order,
        LevelNode* level,
        std::optional<LevelUpdate>& level_update) noexcept;
    static void reduce_in_order_book(OrderBook& order_book,
                                     OrderNode& order,
                                     Quantity reduction,
                                     std::optional<LevelUpdate>& level_update) noexcept;
    static void delete_from_order_book(
        OrderBook& order_book,
        OrderNode& order,
        std::optional<LevelUpdate>& level_update) noexcept;
    [[nodiscard]] static bool prepare_trailing_order(
        OrderBook& order_book,
        Order& order,
        const OrderNode* excluded_order = nullptr) noexcept;
    [[nodiscard]] static bool is_stop_triggered(const OrderBook& order_book,
                                                const Order& order) noexcept;
    [[nodiscard]] ErrorCode add_triggered_order(OrderBook& order_book,
                                                const Order& order);
    [[nodiscard]] ErrorCode replace_resolved_order(OrderBook& order_book,
                                                   OrderNode& order,
                                                   Order replacement);
    [[nodiscard]] ErrorCode execute_resolved_order(OrderBook& order_book,
                                                   OrderNode& order,
                                                   Price execution_price,
                                                   Quantity execution_quantity) noexcept;
    [[nodiscard]] static bool resolve_execution_bound(const OrderBook& order_book,
                                                       const Order& order,
                                                       Price& bound) noexcept;
    [[nodiscard]] static bool is_price_eligible(const Order& order,
                                                Price level_price,
                                                Price bound) noexcept;
    [[nodiscard]] static Quantity calculate_executable_quantity(
        const OrderBook& order_book,
        const Order& order,
        Price bound) noexcept;
    [[nodiscard]] static Quantity calculate_crossed_chain(
        const OrderBook& order_book) noexcept;
    [[nodiscard]] bool match_incoming(OrderBook& order_book,
                                      Order& incoming,
                                      Price bound,
                                      bool registered_incoming = false) noexcept;
    void process_if_enabled(OrderBook& order_book) noexcept;
    void match_order_book(OrderBook& order_book) noexcept;
    [[nodiscard]] bool activate_advanced_orders(OrderBook& order_book) noexcept;
    [[nodiscard]] bool activate_stop_level(OrderBook& order_book,
                                           LevelNode* level,
                                           Price market_price) noexcept;
    [[nodiscard]] bool activate_market_stop(OrderBook& order_book,
                                            OrderNode& order) noexcept;
    [[nodiscard]] bool activate_limit_stop(OrderBook& order_book,
                                           OrderNode& order) noexcept;
    [[nodiscard]] bool reprice_trailing_orders(OrderBook& order_book,
                                               OrderSide side) noexcept;
    void execute_crossed_chain(OrderBook& order_book,
                               Quantity execution_quantity) noexcept;
    void execute_incoming_trade(OrderBook& order_book,
                                Order& incoming,
                                OrderNode& passive,
                                Price execution_price,
                                Quantity execution_quantity,
                                bool registered_incoming) noexcept;
    void execute_crossed_trade(OrderBook& order_book,
                               OrderNode& bid,
                               OrderNode& ask,
                               Price execution_price,
                               Quantity execution_quantity) noexcept;
    void publish_execution_result(const OrderBook& order_book,
                                  OrderNode& order,
                                  Price execution_price,
                                  Quantity execution_quantity,
                                  const std::optional<LevelUpdate>& level_update) noexcept;
    void publish_level_update(const OrderBook& order_book,
                              const LevelUpdate& update) noexcept;
};

} // namespace matching

#include "market_manager.inl"

#endif // MATCHING_MARKET_MANAGER_H
