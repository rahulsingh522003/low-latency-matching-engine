#include "providers/nasdaq/itch/decoder.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace providers::nasdaq::itch {

namespace {

using Cursor = const std::byte*;

[[nodiscard]] char read_character(Cursor& cursor) noexcept
{
    const char value =
        static_cast<char>(std::to_integer<unsigned char>(*cursor));
    ++cursor;
    return value;
}

template <typename Integer, std::size_t Width = sizeof(Integer)>
[[nodiscard]] Integer read_integer(Cursor& cursor) noexcept
{
    static_assert(std::is_unsigned_v<Integer>);
    static_assert(Width <= sizeof(Integer));

    Integer value = 0;
    for (std::size_t index = 0; index < Width; ++index)
    {
        value = static_cast<Integer>(
            (value << 8U) |
            static_cast<Integer>(
                std::to_integer<unsigned char>(cursor[index])));
    }

    cursor += Width;
    return value;
}

template <std::size_t Width>
[[nodiscard]] std::array<char, Width> read_text(Cursor& cursor) noexcept
{
    std::array<char, Width> value;
    std::memcpy(value.data(), cursor, Width);
    cursor += Width;
    return value;
}

template <typename Message>
void read_header(Cursor& cursor, Message& message) noexcept
{
    message.type = read_character(cursor);
    message.stock_locate = read_integer<std::uint16_t>(cursor);
    message.tracking_number = read_integer<std::uint16_t>(cursor);
    message.timestamp = read_integer<std::uint64_t, 6>(cursor);
}

template <typename Message>
[[nodiscard]] bool has_exact_size(std::span<const std::byte> frame) noexcept
{
    return frame.size() == Message::WIRE_SIZE;
}

void assert_fully_consumed(Cursor cursor,
                           std::span<const std::byte> frame) noexcept
{
    assert((cursor == (frame.data() + frame.size())) &&
           "ITCH decoder did not consume the complete message");
    static_cast<void>(cursor);
    static_cast<void>(frame);
}

} // namespace

Decoder::Decoder(MessageHandler& handler) noexcept
    : handler_(handler)
{
}

DecodeStatus Decoder::process(std::span<const std::byte> input) noexcept
{
    message_status_ = DecodeStatus::OK;
    const auto frame_handler = [this](std::span<const std::byte> frame) noexcept {
        message_status_ = decode_message(frame);
        return message_status_ == DecodeStatus::OK;
    };

    return translate_frame_status(
        frame_decoder_.process(input, frame_handler), message_status_);
}

DecodeStatus Decoder::finish() noexcept
{
    return translate_frame_status(frame_decoder_.finish(), message_status_);
}

void Decoder::reset() noexcept
{
    frame_decoder_.reset();
    message_status_ = DecodeStatus::OK;
}

DecodeStatus Decoder::translate_frame_status(FrameStatus status,
                                              DecodeStatus message_status) noexcept
{
    switch (status)
    {
        case FrameStatus::OK:
            return DecodeStatus::OK;
        case FrameStatus::INVALID_MESSAGE_LENGTH:
            return DecodeStatus::INVALID_MESSAGE_LENGTH;
        case FrameStatus::FRAME_REJECTED:
            return (message_status == DecodeStatus::OK)
                       ? DecodeStatus::MESSAGE_REJECTED
                       : message_status;
        case FrameStatus::TRUNCATED_INPUT:
            return DecodeStatus::TRUNCATED_INPUT;
        case FrameStatus::FAILED:
            return DecodeStatus::FAILED;
    }

    assert(false && "Unknown ITCH frame status");
    return DecodeStatus::FAILED;
}

DecodeStatus Decoder::decode_message(std::span<const std::byte> frame) noexcept
{
    assert(!frame.empty() && "ITCH message must contain a type byte");
    const char type =
        static_cast<char>(std::to_integer<unsigned char>(frame.front()));

    switch (type)
    {
        case SystemEventMessage::TYPE:
            return decode_system_event(frame);
        case StockDirectoryMessage::TYPE:
            return decode_stock_directory(frame);
        case StockTradingActionMessage::TYPE:
            return decode_stock_trading_action(frame);
        case RegShoRestrictionMessage::TYPE:
            return decode_reg_sho_restriction(frame);
        case MarketParticipantPositionMessage::TYPE:
            return decode_market_participant_position(frame);
        case MwcbDeclineLevelMessage::TYPE:
            return decode_mwcb_decline_level(frame);
        case MwcbStatusMessage::TYPE:
            return decode_mwcb_status(frame);
        case IpoQuotingPeriodUpdateMessage::TYPE:
            return decode_ipo_quoting_period_update(frame);
        case AddOrderMessage::TYPE:
            return decode_add_order(frame);
        case AddOrderWithAttributionMessage::TYPE:
            return decode_add_order_with_attribution(frame);
        case OrderExecutedMessage::TYPE:
            return decode_order_executed(frame);
        case OrderExecutedWithPriceMessage::TYPE:
            return decode_order_executed_with_price(frame);
        case OrderCancelMessage::TYPE:
            return decode_order_cancel(frame);
        case OrderDeleteMessage::TYPE:
            return decode_order_delete(frame);
        case OrderReplaceMessage::TYPE:
            return decode_order_replace(frame);
        case TradeMessage::TYPE:
            return decode_trade(frame);
        case CrossTradeMessage::TYPE:
            return decode_cross_trade(frame);
        case BrokenTradeMessage::TYPE:
            return decode_broken_trade(frame);
        case NoiiMessage::TYPE:
            return decode_noii(frame);
        case RpiiMessage::TYPE:
            return decode_rpii(frame);
        case LuldAuctionCollarMessage::TYPE:
            return decode_luld_auction_collar(frame);
        default:
            return decode_unknown(frame);
    }
}

DecodeStatus Decoder::decode_system_event(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<SystemEventMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    SystemEventMessage message;
    read_header(cursor, message);
    message.event_code = read_character(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_stock_directory(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<StockDirectoryMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    StockDirectoryMessage message;
    read_header(cursor, message);
    message.stock = read_text<8>(cursor);
    message.market_category = read_character(cursor);
    message.financial_status_indicator = read_character(cursor);
    message.round_lot_size = read_integer<std::uint32_t>(cursor);
    message.round_lots_only = read_character(cursor);
    message.issue_classification = read_character(cursor);
    message.issue_sub_type = read_text<2>(cursor);
    message.authenticity = read_character(cursor);
    message.short_sale_threshold_indicator = read_character(cursor);
    message.ipo_flag = read_character(cursor);
    message.luld_reference_price_tier = read_character(cursor);
    message.etp_flag = read_character(cursor);
    message.etp_leverage_factor = read_integer<std::uint32_t>(cursor);
    message.inverse_indicator = read_character(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_stock_trading_action(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<StockTradingActionMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    StockTradingActionMessage message;
    read_header(cursor, message);
    message.stock = read_text<8>(cursor);
    message.trading_state = read_character(cursor);
    message.reserved = read_character(cursor);
    message.reason = read_text<4>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_reg_sho_restriction(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<RegShoRestrictionMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    RegShoRestrictionMessage message;
    read_header(cursor, message);
    message.stock = read_text<8>(cursor);
    message.action = read_character(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_market_participant_position(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<MarketParticipantPositionMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    MarketParticipantPositionMessage message;
    read_header(cursor, message);
    message.participant_id = read_text<4>(cursor);
    message.stock = read_text<8>(cursor);
    message.primary_market_maker = read_character(cursor);
    message.market_maker_mode = read_character(cursor);
    message.market_participant_state = read_character(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_mwcb_decline_level(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<MwcbDeclineLevelMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    MwcbDeclineLevelMessage message;
    read_header(cursor, message);
    message.level_1 = read_integer<std::uint64_t>(cursor);
    message.level_2 = read_integer<std::uint64_t>(cursor);
    message.level_3 = read_integer<std::uint64_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_mwcb_status(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<MwcbStatusMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    MwcbStatusMessage message;
    read_header(cursor, message);
    message.breached_level = read_character(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_ipo_quoting_period_update(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<IpoQuotingPeriodUpdateMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    IpoQuotingPeriodUpdateMessage message;
    read_header(cursor, message);
    message.stock = read_text<8>(cursor);
    message.release_time = read_integer<std::uint32_t>(cursor);
    message.release_qualifier = read_character(cursor);
    message.price = read_integer<std::uint32_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_add_order(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<AddOrderMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    AddOrderMessage message;
    read_header(cursor, message);
    message.order_reference_number = read_integer<std::uint64_t>(cursor);
    message.buy_sell_indicator = read_character(cursor);
    message.shares = read_integer<std::uint32_t>(cursor);
    message.stock = read_text<8>(cursor);
    message.price = read_integer<std::uint32_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_add_order_with_attribution(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<AddOrderWithAttributionMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    AddOrderWithAttributionMessage message;
    read_header(cursor, message);
    message.order_reference_number = read_integer<std::uint64_t>(cursor);
    message.buy_sell_indicator = read_character(cursor);
    message.shares = read_integer<std::uint32_t>(cursor);
    message.stock = read_text<8>(cursor);
    message.price = read_integer<std::uint32_t>(cursor);
    message.attribution = read_text<4>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_order_executed(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<OrderExecutedMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    OrderExecutedMessage message;
    read_header(cursor, message);
    message.order_reference_number = read_integer<std::uint64_t>(cursor);
    message.executed_shares = read_integer<std::uint32_t>(cursor);
    message.match_number = read_integer<std::uint64_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_order_executed_with_price(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<OrderExecutedWithPriceMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    OrderExecutedWithPriceMessage message;
    read_header(cursor, message);
    message.order_reference_number = read_integer<std::uint64_t>(cursor);
    message.executed_shares = read_integer<std::uint32_t>(cursor);
    message.match_number = read_integer<std::uint64_t>(cursor);
    message.printable = read_character(cursor);
    message.execution_price = read_integer<std::uint32_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_order_cancel(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<OrderCancelMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    OrderCancelMessage message;
    read_header(cursor, message);
    message.order_reference_number = read_integer<std::uint64_t>(cursor);
    message.cancelled_shares = read_integer<std::uint32_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_order_delete(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<OrderDeleteMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    OrderDeleteMessage message;
    read_header(cursor, message);
    message.order_reference_number = read_integer<std::uint64_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_order_replace(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<OrderReplaceMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    OrderReplaceMessage message;
    read_header(cursor, message);
    message.original_order_reference_number =
        read_integer<std::uint64_t>(cursor);
    message.new_order_reference_number = read_integer<std::uint64_t>(cursor);
    message.shares = read_integer<std::uint32_t>(cursor);
    message.price = read_integer<std::uint32_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_trade(std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<TradeMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    TradeMessage message;
    read_header(cursor, message);
    message.order_reference_number = read_integer<std::uint64_t>(cursor);
    message.buy_sell_indicator = read_character(cursor);
    message.shares = read_integer<std::uint32_t>(cursor);
    message.stock = read_text<8>(cursor);
    message.price = read_integer<std::uint32_t>(cursor);
    message.match_number = read_integer<std::uint64_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_cross_trade(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<CrossTradeMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    CrossTradeMessage message;
    read_header(cursor, message);
    message.shares = read_integer<std::uint64_t>(cursor);
    message.stock = read_text<8>(cursor);
    message.cross_price = read_integer<std::uint32_t>(cursor);
    message.match_number = read_integer<std::uint64_t>(cursor);
    message.cross_type = read_character(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_broken_trade(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<BrokenTradeMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    BrokenTradeMessage message;
    read_header(cursor, message);
    message.match_number = read_integer<std::uint64_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_noii(std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<NoiiMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    NoiiMessage message;
    read_header(cursor, message);
    message.paired_shares = read_integer<std::uint64_t>(cursor);
    message.imbalance_shares = read_integer<std::uint64_t>(cursor);
    message.imbalance_direction = read_character(cursor);
    message.stock = read_text<8>(cursor);
    message.far_price = read_integer<std::uint32_t>(cursor);
    message.near_price = read_integer<std::uint32_t>(cursor);
    message.current_reference_price = read_integer<std::uint32_t>(cursor);
    message.cross_type = read_character(cursor);
    message.price_variation_indicator = read_character(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_rpii(std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<RpiiMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    RpiiMessage message;
    read_header(cursor, message);
    message.stock = read_text<8>(cursor);
    message.interest_flag = read_character(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_luld_auction_collar(
    std::span<const std::byte> frame) noexcept
{
    if (!has_exact_size<LuldAuctionCollarMessage>(frame))
        return DecodeStatus::INVALID_MESSAGE_SIZE;

    Cursor cursor = frame.data();
    LuldAuctionCollarMessage message;
    read_header(cursor, message);
    message.stock = read_text<8>(cursor);
    message.auction_collar_reference_price =
        read_integer<std::uint32_t>(cursor);
    message.upper_auction_collar_price = read_integer<std::uint32_t>(cursor);
    message.lower_auction_collar_price = read_integer<std::uint32_t>(cursor);
    message.auction_collar_extension = read_integer<std::uint32_t>(cursor);
    assert_fully_consumed(cursor, frame);
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

DecodeStatus Decoder::decode_unknown(
    std::span<const std::byte> frame) noexcept
{
    const UnknownMessage message{
        static_cast<char>(std::to_integer<unsigned char>(frame.front()))};
    return handler_.on_message(message) ? DecodeStatus::OK
                                        : DecodeStatus::MESSAGE_REJECTED;
}

} // namespace providers::nasdaq::itch
