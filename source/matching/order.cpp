#include "matching/order.h"

namespace matching {

ErrorCode Order::validate() const noexcept
{
    if (id == 0)
        return ErrorCode::ORDER_ID_INVALID;

    switch (side)
    {
        case OrderSide::BUY:
        case OrderSide::SELL:
            break;
        default:
            return ErrorCode::ORDER_PARAMETER_INVALID;
    }

    switch (type)
    {
        case OrderType::MARKET:
        case OrderType::LIMIT:
        case OrderType::STOP:
        case OrderType::STOP_LIMIT:
        case OrderType::TRAILING_STOP:
        case OrderType::TRAILING_STOP_LIMIT:
            break;
        default:
            return ErrorCode::ORDER_TYPE_INVALID;
    }

    switch (time_in_force)
    {
        case OrderTimeInForce::GTC:
        case OrderTimeInForce::IOC:
        case OrderTimeInForce::FOK:
        case OrderTimeInForce::AON:
            break;
        default:
            return ErrorCode::ORDER_PARAMETER_INVALID;
    }

    if (quantity == 0)
        return ErrorCode::ORDER_QUANTITY_INVALID;

    if (is_market())
    {
        if ((price != 0) || (stop_price != 0))
            return ErrorCode::ORDER_PARAMETER_INVALID;

        if (!is_ioc() && !is_fok())
            return ErrorCode::ORDER_PARAMETER_INVALID;
        if (is_display_limited())
            return ErrorCode::ORDER_PARAMETER_INVALID;
    }

    if (is_limit())
    {
        if ((price == 0) || (stop_price != 0))
            return ErrorCode::ORDER_PARAMETER_INVALID;
        if (slippage != UNLIMITED_SLIPPAGE)
            return ErrorCode::ORDER_PARAMETER_INVALID;
    }

    if (is_stop())
    {
        if ((price != 0) || (stop_price == 0))
            return ErrorCode::ORDER_PARAMETER_INVALID;
        if (is_aon())
            return ErrorCode::ORDER_PARAMETER_INVALID;
        if (is_display_limited())
            return ErrorCode::ORDER_PARAMETER_INVALID;
    }

    if (is_stop_limit())
    {
        if ((price == 0) || (stop_price == 0))
            return ErrorCode::ORDER_PARAMETER_INVALID;
        if (slippage != UNLIMITED_SLIPPAGE)
            return ErrorCode::ORDER_PARAMETER_INVALID;
    }

    if (is_trailing_stop())
    {
        if ((price != 0) || (stop_price == 0))
            return ErrorCode::ORDER_PARAMETER_INVALID;
        if (is_aon())
            return ErrorCode::ORDER_PARAMETER_INVALID;
        if (is_display_limited())
            return ErrorCode::ORDER_PARAMETER_INVALID;
    }

    if (is_trailing_stop_limit())
    {
        if ((price == 0) || (stop_price == 0))
            return ErrorCode::ORDER_PARAMETER_INVALID;
        if (slippage != UNLIMITED_SLIPPAGE)
            return ErrorCode::ORDER_PARAMETER_INVALID;
    }

    const bool trailing = is_trailing_stop() || is_trailing_stop_limit();
    if (!trailing)
    {
        if ((trailing_distance != 0) || (trailing_step != 0))
            return ErrorCode::ORDER_PARAMETER_INVALID;
        return ErrorCode::OK;
    }

    if (trailing_distance == 0)
        return ErrorCode::ORDER_PARAMETER_INVALID;

    if (trailing_distance > 0)
    {
        if ((trailing_step < 0) || (trailing_step >= trailing_distance))
            return ErrorCode::ORDER_PARAMETER_INVALID;
    }
    else
    {
        constexpr std::int64_t MAX_PERCENTAGE_DISTANCE = -10'000;

        if (trailing_distance < MAX_PERCENTAGE_DISTANCE)
            return ErrorCode::ORDER_PARAMETER_INVALID;

        if ((trailing_step > 0) || (trailing_step <= trailing_distance))
            return ErrorCode::ORDER_PARAMETER_INVALID;
    }

    return ErrorCode::OK;
}

} // namespace matching
