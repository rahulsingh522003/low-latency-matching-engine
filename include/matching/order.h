#ifndef MATCHING_ORDER_H
#define MATCHING_ORDER_H

#include "containers/intrusive_list.h"
#include "errors.h"
#include "types.h"

#include <cstdint>
#include <limits>

namespace matching {

enum class OrderSide : std::uint8_t
{
    BUY = 0,
    SELL = 1
};

enum class OrderType : std::uint8_t
{
    MARKET = 0,
    LIMIT = 1,
    STOP = 2,
    STOP_LIMIT = 3,
    TRAILING_STOP = 4,
    TRAILING_STOP_LIMIT = 5
};

enum class OrderTimeInForce : std::uint8_t
{
    GTC = 0,  // GTC — Good Till Cancelled
    IOC = 1,  // IOC — Immediate Or Cancel
    FOK = 2,  // FOK — Fill Or Kill
    AON = 3   // AON — All Or None
};

struct Order
{
    static constexpr Price UNLIMITED_SLIPPAGE = std::numeric_limits<Price>::max();
    static constexpr Quantity FULLY_DISPLAYED =
        std::numeric_limits<Quantity>::max();

    OrderId id{0};
    SymbolId symbol_id{0};
    OrderType type{OrderType::MARKET};
    OrderSide side{OrderSide::BUY};
    OrderTimeInForce time_in_force{OrderTimeInForce::GTC};

    Price price{0};
    Price stop_price{0};
    Quantity quantity{0};
    Quantity max_display_quantity{FULLY_DISPLAYED};
    Price slippage{UNLIMITED_SLIPPAGE};
    std::int64_t trailing_distance{0};
    std::int64_t trailing_step{0};

    Order() noexcept = default;
    Order(OrderId order_id,
          SymbolId symbol,
          OrderType order_type,
          OrderSide order_side,
          Price order_price,
          Price order_stop_price,
          Quantity order_quantity,
          OrderTimeInForce tif = OrderTimeInForce::GTC,
          std::int64_t order_trailing_distance = 0,
          std::int64_t order_trailing_step = 0,
          Price order_slippage = UNLIMITED_SLIPPAGE,
          Quantity order_max_display_quantity = FULLY_DISPLAYED) noexcept;

    [[nodiscard]] bool is_market() const noexcept { return type == OrderType::MARKET; }
    [[nodiscard]] bool is_limit() const noexcept { return type == OrderType::LIMIT; }
    [[nodiscard]] bool is_stop() const noexcept { return type == OrderType::STOP; }
    [[nodiscard]] bool is_stop_limit() const noexcept
    {
        return type == OrderType::STOP_LIMIT;
    }
    [[nodiscard]] bool is_trailing_stop() const noexcept
    {
        return type == OrderType::TRAILING_STOP;
    }
    [[nodiscard]] bool is_trailing_stop_limit() const noexcept
    {
        return type == OrderType::TRAILING_STOP_LIMIT;
    }

    [[nodiscard]] bool is_buy() const noexcept { return side == OrderSide::BUY; }
    [[nodiscard]] bool is_sell() const noexcept { return side == OrderSide::SELL; }

    [[nodiscard]] bool is_gtc() const noexcept { return time_in_force == OrderTimeInForce::GTC; }
    [[nodiscard]] bool is_ioc() const noexcept { return time_in_force == OrderTimeInForce::IOC; }
    [[nodiscard]] bool is_fok() const noexcept { return time_in_force == OrderTimeInForce::FOK; }
    [[nodiscard]] bool is_aon() const noexcept { return time_in_force == OrderTimeInForce::AON; }

    [[nodiscard]] Quantity displayed_quantity() const noexcept
    {
        return (quantity < max_display_quantity) ? quantity : max_display_quantity;
    }
    [[nodiscard]] Quantity hidden_quantity() const noexcept
    {
        return quantity - displayed_quantity();
    }
    [[nodiscard]] bool is_hidden() const noexcept
    {
        return max_display_quantity == 0;
    }
    [[nodiscard]] bool is_display_limited() const noexcept
    {
        return max_display_quantity != FULLY_DISPLAYED;
    }

    [[nodiscard]] ErrorCode validate() const noexcept;

    [[nodiscard]] static Order Market(OrderId order_id,
                                      SymbolId symbol,
                                      OrderSide order_side,
                                      Quantity order_quantity,
                                      OrderTimeInForce tif = OrderTimeInForce::IOC,
                                      Price order_slippage = UNLIMITED_SLIPPAGE) noexcept;

    [[nodiscard]] static Order BuyMarket(OrderId order_id,
                                         SymbolId symbol,
                                         Quantity order_quantity,
                                         OrderTimeInForce tif = OrderTimeInForce::IOC,
                                         Price order_slippage = UNLIMITED_SLIPPAGE) noexcept;

    [[nodiscard]] static Order SellMarket(OrderId order_id,
                                          SymbolId symbol,
                                          Quantity order_quantity,
                                          OrderTimeInForce tif = OrderTimeInForce::IOC,
                                          Price order_slippage = UNLIMITED_SLIPPAGE) noexcept;

    [[nodiscard]] static Order Limit(OrderId order_id,
                                     SymbolId symbol,
                                     OrderSide order_side,
                                     Price order_price,
                                     Quantity order_quantity,
                                     OrderTimeInForce tif = OrderTimeInForce::GTC,
                                     Quantity order_max_display_quantity =
                                         FULLY_DISPLAYED) noexcept;

    [[nodiscard]] static Order BuyLimit(OrderId order_id,
                                        SymbolId symbol,
                                        Price order_price,
                                        Quantity order_quantity,
                                        OrderTimeInForce tif = OrderTimeInForce::GTC,
                                        Quantity order_max_display_quantity =
                                            FULLY_DISPLAYED) noexcept;

    [[nodiscard]] static Order SellLimit(OrderId order_id,
                                         SymbolId symbol,
                                         Price order_price,
                                         Quantity order_quantity,
                                         OrderTimeInForce tif = OrderTimeInForce::GTC,
                                         Quantity order_max_display_quantity =
                                             FULLY_DISPLAYED) noexcept;

    [[nodiscard]] static Order Stop(OrderId order_id,
                                    SymbolId symbol,
                                    OrderSide order_side,
                                    Price order_stop_price,
                                    Quantity order_quantity,
                                    OrderTimeInForce tif = OrderTimeInForce::GTC,
                                    Price order_slippage = UNLIMITED_SLIPPAGE) noexcept;

    [[nodiscard]] static Order BuyStop(OrderId order_id,
                                       SymbolId symbol,
                                       Price order_stop_price,
                                       Quantity order_quantity,
                                       OrderTimeInForce tif = OrderTimeInForce::GTC,
                                       Price order_slippage = UNLIMITED_SLIPPAGE) noexcept;

    [[nodiscard]] static Order SellStop(OrderId order_id,
                                        SymbolId symbol,
                                        Price order_stop_price,
                                        Quantity order_quantity,
                                        OrderTimeInForce tif = OrderTimeInForce::GTC,
                                        Price order_slippage = UNLIMITED_SLIPPAGE) noexcept;

    [[nodiscard]] static Order StopLimit(OrderId order_id,
                                         SymbolId symbol,
                                         OrderSide order_side,
                                         Price order_stop_price,
                                         Price order_price,
                                         Quantity order_quantity,
                                         OrderTimeInForce tif = OrderTimeInForce::GTC,
                                         Quantity order_max_display_quantity =
                                             FULLY_DISPLAYED) noexcept;

    [[nodiscard]] static Order BuyStopLimit(
        OrderId order_id,
        SymbolId symbol,
        Price order_stop_price,
        Price order_price,
        Quantity order_quantity,
        OrderTimeInForce tif = OrderTimeInForce::GTC,
        Quantity order_max_display_quantity = FULLY_DISPLAYED) noexcept;

    [[nodiscard]] static Order SellStopLimit(
        OrderId order_id,
        SymbolId symbol,
        Price order_stop_price,
        Price order_price,
        Quantity order_quantity,
        OrderTimeInForce tif = OrderTimeInForce::GTC,
        Quantity order_max_display_quantity = FULLY_DISPLAYED) noexcept;

    [[nodiscard]] static Order TrailingStop(
        OrderId order_id,
        SymbolId symbol,
        OrderSide order_side,
        Price order_stop_price,
        Quantity order_quantity,
        std::int64_t order_trailing_distance,
        std::int64_t order_trailing_step = 0,
        OrderTimeInForce tif = OrderTimeInForce::GTC,
        Price order_slippage = UNLIMITED_SLIPPAGE) noexcept;

    [[nodiscard]] static Order TrailingBuyStop(
        OrderId order_id,
        SymbolId symbol,
        Price order_stop_price,
        Quantity order_quantity,
        std::int64_t order_trailing_distance,
        std::int64_t order_trailing_step = 0,
        OrderTimeInForce tif = OrderTimeInForce::GTC,
        Price order_slippage = UNLIMITED_SLIPPAGE) noexcept;

    [[nodiscard]] static Order TrailingSellStop(
        OrderId order_id,
        SymbolId symbol,
        Price order_stop_price,
        Quantity order_quantity,
        std::int64_t order_trailing_distance,
        std::int64_t order_trailing_step = 0,
        OrderTimeInForce tif = OrderTimeInForce::GTC,
        Price order_slippage = UNLIMITED_SLIPPAGE) noexcept;

    [[nodiscard]] static Order TrailingStopLimit(
        OrderId order_id,
        SymbolId symbol,
        OrderSide order_side,
        Price order_stop_price,
        Price order_price,
        Quantity order_quantity,
        std::int64_t order_trailing_distance,
        std::int64_t order_trailing_step = 0,
        OrderTimeInForce tif = OrderTimeInForce::GTC,
        Quantity order_max_display_quantity = FULLY_DISPLAYED) noexcept;

    [[nodiscard]] static Order TrailingBuyStopLimit(
        OrderId order_id,
        SymbolId symbol,
        Price order_stop_price,
        Price order_price,
        Quantity order_quantity,
        std::int64_t order_trailing_distance,
        std::int64_t order_trailing_step = 0,
        OrderTimeInForce tif = OrderTimeInForce::GTC,
        Quantity order_max_display_quantity = FULLY_DISPLAYED) noexcept;

    [[nodiscard]] static Order TrailingSellStopLimit(
        OrderId order_id,
        SymbolId symbol,
        Price order_stop_price,
        Price order_price,
        Quantity order_quantity,
        std::int64_t order_trailing_distance,
        std::int64_t order_trailing_step = 0,
        OrderTimeInForce tif = OrderTimeInForce::GTC,
        Quantity order_max_display_quantity = FULLY_DISPLAYED) noexcept;
};

struct LevelNode;

struct OrderNode : public Order, public containers::IntrusiveList<OrderNode>::Node
{
    LevelNode* level{nullptr};

    explicit OrderNode(const Order& order) noexcept;
    OrderNode(const OrderNode&) = delete;
    OrderNode(OrderNode&&) = delete;

    OrderNode& operator=(const OrderNode&) = delete;
    OrderNode& operator=(OrderNode&&) = delete;
};

} // namespace matching

#include "order.inl"

#endif // MATCHING_ORDER_H
