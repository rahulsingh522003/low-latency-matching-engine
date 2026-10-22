#include "providers/nasdaq/itch/market_adapter.h"

#include "matching/order.h"
#include "matching/symbol.h"

#include <new>

namespace providers::nasdaq::itch {

MarketAdapter::MarketAdapter(matching::MarketManager& market,
                             PriceConversionPolicy price_policy) noexcept
    : market_(market),
      price_policy_(price_policy)
{
}

bool MarketAdapter::begin_message(char message_type) noexcept
{
    ++statistics_.messages;
    ++statistics_.message_type_counts[
        static_cast<unsigned char>(message_type)];

    if (status_ != MarketAdapterStatus::OK)
        return false;
    if (market_.is_matching_enabled())
        return fail(MarketAdapterStatus::MATCHING_MUST_BE_DISABLED);
    return true;
}

bool MarketAdapter::ignore_message(char message_type) noexcept
{
    if (!begin_message(message_type))
        return false;
    ++statistics_.ignored_messages;
    return true;
}

bool MarketAdapter::apply(matching::ErrorCode result) noexcept
{
    if (result == matching::ErrorCode::OK)
        return true;

    engine_error_ = result;
    return fail(MarketAdapterStatus::ENGINE_REJECTED);
}

bool MarketAdapter::fail(MarketAdapterStatus status) noexcept
{
    if (status_ == MarketAdapterStatus::OK)
        status_ = status;
    return false;
}

bool MarketAdapter::convert_price(std::uint32_t itch_price,
                                  matching::Price& cent_price) noexcept
{
    if (itch_price == 0)
        return fail(MarketAdapterStatus::PRICE_NOT_REPRESENTABLE);

    const std::uint32_t whole_cents = itch_price / ITCH_UNITS_PER_CENT;
    const std::uint32_t sub_cent_units = itch_price % ITCH_UNITS_PER_CENT;

    if (sub_cent_units == 0)
    {
        ++statistics_.exact_cent_prices;
        cent_price = whole_cents;
        return true;
    }

    if (price_policy_ == PriceConversionPolicy::REQUIRE_EXACT_CENT)
        return fail(MarketAdapterStatus::PRICE_NOT_REPRESENTABLE);

    ++statistics_.rounded_prices;
    cent_price = whole_cents + ((sub_cent_units >= 50) ? 1U : 0U);
    if (cent_price == 0)
    {
        // The core has no zero-price limit level. Positive prices below half a
        // cent are aggregated into its minimum representable one-cent level.
        cent_price = 1;
        ++statistics_.minimum_cent_prices;
    }
    return true;
}

bool MarketAdapter::add_order(matching::SymbolId symbol_id,
                              matching::OrderId order_id,
                              char side,
                              std::uint32_t shares,
                              std::uint32_t itch_price) noexcept
{
    matching::OrderSide order_side;
    if (side == 'B')
        order_side = matching::OrderSide::BUY;
    else if (side == 'S')
        order_side = matching::OrderSide::SELL;
    else
        return fail(MarketAdapterStatus::INVALID_ORDER_SIDE);

    matching::Price cent_price = 0;
    if (!convert_price(itch_price, cent_price))
        return false;

    try
    {
        return apply(market_.add_order(matching::Order::Limit(
            order_id, symbol_id, order_side, cent_price, shares)));
    }
    catch (const std::bad_alloc&)
    {
        return fail(MarketAdapterStatus::ALLOCATION_FAILURE);
    }
    catch (...)
    {
        return fail(MarketAdapterStatus::INTERNAL_FAILURE);
    }
}

bool MarketAdapter::on_message(const SystemEventMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const StockDirectoryMessage& message) noexcept
{
    if (!begin_message(message.type))
        return false;
    ++statistics_.symbol_directory_messages;

    const matching::SymbolId symbol_id = message.stock_locate;
    const matching::Symbol* existing_symbol = market_.find_symbol(symbol_id);
    if (existing_symbol != nullptr)
    {
        if ((existing_symbol->name == message.stock) &&
            (market_.find_order_book(symbol_id) != nullptr))
        {
            ++statistics_.repeated_directory_messages;
            return true;
        }

        engine_error_ = matching::ErrorCode::SYMBOL_DUPLICATE;
        return fail(MarketAdapterStatus::ENGINE_REJECTED);
    }

    try
    {
        if (!apply(market_.add_symbol(matching::Symbol(symbol_id, message.stock))))
            return false;

        const matching::ErrorCode book_result = market_.add_order_book(symbol_id);
        if (book_result == matching::ErrorCode::OK)
            return true;

        const matching::ErrorCode rollback_result = market_.delete_symbol(symbol_id);
        if (rollback_result != matching::ErrorCode::OK)
            return fail(MarketAdapterStatus::INTERNAL_FAILURE);
        return apply(book_result);
    }
    catch (const std::bad_alloc&)
    {
        // add_symbol() and add_order_book() are individually exception-safe.
        // If the symbol was committed but book construction failed, remove it.
        if ((market_.find_symbol(symbol_id) != nullptr) &&
            (market_.find_order_book(symbol_id) == nullptr))
        {
            static_cast<void>(market_.delete_symbol(symbol_id));
        }
        return fail(MarketAdapterStatus::ALLOCATION_FAILURE);
    }
    catch (...)
    {
        return fail(MarketAdapterStatus::INTERNAL_FAILURE);
    }
}

bool MarketAdapter::on_message(const StockTradingActionMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const RegShoRestrictionMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(
    const MarketParticipantPositionMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const MwcbDeclineLevelMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const MwcbStatusMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(
    const IpoQuotingPeriodUpdateMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const AddOrderMessage& message) noexcept
{
    if (!begin_message(message.type))
        return false;
    ++statistics_.add_order_messages;
    return add_order(message.stock_locate,
                     message.order_reference_number,
                     message.buy_sell_indicator,
                     message.shares,
                     message.price);
}

bool MarketAdapter::on_message(
    const AddOrderWithAttributionMessage& message) noexcept
{
    if (!begin_message(message.type))
        return false;
    ++statistics_.add_order_messages;
    return add_order(message.stock_locate,
                     message.order_reference_number,
                     message.buy_sell_indicator,
                     message.shares,
                     message.price);
}

bool MarketAdapter::on_message(const OrderExecutedMessage& message) noexcept
{
    if (!begin_message(message.type))
        return false;
    ++statistics_.execute_order_messages;
    return apply(market_.execute_order(message.stock_locate,
                                       message.order_reference_number,
                                       message.executed_shares));
}

bool MarketAdapter::on_message(
    const OrderExecutedWithPriceMessage& message) noexcept
{
    if (!begin_message(message.type))
        return false;
    ++statistics_.execute_order_messages;

    matching::Price cent_price = 0;
    if (!convert_price(message.execution_price, cent_price))
        return false;
    return apply(market_.execute_order(message.stock_locate,
                                       message.order_reference_number,
                                       cent_price,
                                       message.executed_shares));
}

bool MarketAdapter::on_message(const OrderCancelMessage& message) noexcept
{
    if (!begin_message(message.type))
        return false;
    ++statistics_.cancel_order_messages;
    return apply(market_.reduce_order(message.stock_locate,
                                      message.order_reference_number,
                                      message.cancelled_shares));
}

bool MarketAdapter::on_message(const OrderDeleteMessage& message) noexcept
{
    if (!begin_message(message.type))
        return false;
    ++statistics_.delete_order_messages;
    return apply(market_.delete_order(message.stock_locate,
                                      message.order_reference_number));
}

bool MarketAdapter::on_message(const OrderReplaceMessage& message) noexcept
{
    if (!begin_message(message.type))
        return false;
    ++statistics_.replace_order_messages;

    matching::Price cent_price = 0;
    if (!convert_price(message.price, cent_price))
        return false;

    try
    {
        return apply(market_.replace_order(
            message.stock_locate,
            message.original_order_reference_number,
            message.new_order_reference_number,
            cent_price,
            message.shares));
    }
    catch (const std::bad_alloc&)
    {
        return fail(MarketAdapterStatus::ALLOCATION_FAILURE);
    }
    catch (...)
    {
        return fail(MarketAdapterStatus::INTERNAL_FAILURE);
    }
}

bool MarketAdapter::on_message(const TradeMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const CrossTradeMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const BrokenTradeMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const NoiiMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const RpiiMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const LuldAuctionCollarMessage& message) noexcept
{
    return ignore_message(message.type);
}

bool MarketAdapter::on_message(const UnknownMessage& message) noexcept
{
    if (!begin_message(message.type))
        return false;
    ++statistics_.unknown_messages;
    return true;
}

} // namespace providers::nasdaq::itch
