#include "providers/nasdaq/itch/decoder.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace itch = providers::nasdaq::itch;

constexpr std::uint16_t STOCK_LOCATE = 0x1234U;
constexpr std::uint16_t TRACKING_NUMBER = 0x5678U;
constexpr std::uint64_t TIMESTAMP = 0x010203040506ULL;
constexpr itch::StockName STOCK{'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};

[[nodiscard]] constexpr std::byte as_byte(std::uint8_t value) noexcept
{
    return static_cast<std::byte>(value);
}

template <std::size_t Width, typename Integer>
void append_integer(std::vector<std::byte>& output, Integer value)
{
    const std::uint64_t widened = static_cast<std::uint64_t>(value);
    for (std::size_t index = 0; index < Width; ++index)
    {
        const std::size_t shift = (Width - index - 1) * 8;
        output.push_back(
            as_byte(static_cast<std::uint8_t>(widened >> shift)));
    }
}

template <std::size_t Width>
void append_text(std::vector<std::byte>& output,
                 const std::array<char, Width>& text)
{
    for (const char character : text)
        output.push_back(as_byte(static_cast<std::uint8_t>(character)));
}

void append_header(std::vector<std::byte>& output, char type)
{
    output.push_back(as_byte(static_cast<std::uint8_t>(type)));
    append_integer<2>(output, STOCK_LOCATE);
    append_integer<2>(output, TRACKING_NUMBER);
    append_integer<6>(output, TIMESTAMP);
}

[[nodiscard]] std::vector<std::byte> frame_message(
    std::span<const std::byte> message)
{
    std::vector<std::byte> result;
    result.reserve(message.size() + itch::FrameDecoder::LENGTH_PREFIX_SIZE);
    append_integer<2>(result, message.size());
    result.insert(result.end(), message.begin(), message.end());
    return result;
}

struct Fixture
{
    char type;
    std::vector<std::byte> message;
};

[[nodiscard]] std::vector<Fixture> make_known_fixtures()
{
    std::vector<Fixture> fixtures;
    fixtures.reserve(21);

    auto add = [&fixtures](char type, std::vector<std::byte> message) {
        fixtures.push_back(Fixture{type, std::move(message)});
    };

    {
        std::vector<std::byte> value;
        append_header(value, itch::SystemEventMessage::TYPE);
        value.push_back(as_byte('O'));
        add(itch::SystemEventMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::StockDirectoryMessage::TYPE);
        append_text(value, STOCK);
        value.push_back(as_byte('Q'));
        value.push_back(as_byte('N'));
        append_integer<4>(value, 0x11223344U);
        value.push_back(as_byte('Y'));
        value.push_back(as_byte('A'));
        append_text(value, itch::IssueSubType{'B', 'C'});
        value.push_back(as_byte('P'));
        value.push_back(as_byte('Y'));
        value.push_back(as_byte('N'));
        value.push_back(as_byte('1'));
        value.push_back(as_byte('Y'));
        append_integer<4>(value, 0x55667788U);
        value.push_back(as_byte('N'));
        add(itch::StockDirectoryMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::StockTradingActionMessage::TYPE);
        append_text(value, STOCK);
        value.push_back(as_byte('H'));
        value.push_back(as_byte(' '));
        append_text(value, itch::TradingReason{'T', '1', '2', '3'});
        add(itch::StockTradingActionMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::RegShoRestrictionMessage::TYPE);
        append_text(value, STOCK);
        value.push_back(as_byte('1'));
        add(itch::RegShoRestrictionMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::MarketParticipantPositionMessage::TYPE);
        append_text(value, itch::MarketParticipantId{'M', 'P', 'I', 'D'});
        append_text(value, STOCK);
        value.push_back(as_byte('Y'));
        value.push_back(as_byte('N'));
        value.push_back(as_byte('A'));
        add(itch::MarketParticipantPositionMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::MwcbDeclineLevelMessage::TYPE);
        append_integer<8>(value, 0x0102030405060708ULL);
        append_integer<8>(value, 0x1112131415161718ULL);
        append_integer<8>(value, 0x2122232425262728ULL);
        add(itch::MwcbDeclineLevelMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::MwcbStatusMessage::TYPE);
        value.push_back(as_byte('2'));
        add(itch::MwcbStatusMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::IpoQuotingPeriodUpdateMessage::TYPE);
        append_text(value, STOCK);
        append_integer<4>(value, 0x11223344U);
        value.push_back(as_byte('A'));
        append_integer<4>(value, 0x55667788U);
        add(itch::IpoQuotingPeriodUpdateMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::AddOrderMessage::TYPE);
        append_integer<8>(value, 0x0102030405060708ULL);
        value.push_back(as_byte('B'));
        append_integer<4>(value, 0x11223344U);
        append_text(value, STOCK);
        append_integer<4>(value, 0x55667788U);
        add(itch::AddOrderMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::AddOrderWithAttributionMessage::TYPE);
        append_integer<8>(value, 0x1112131415161718ULL);
        value.push_back(as_byte('S'));
        append_integer<4>(value, 0x22334455U);
        append_text(value, STOCK);
        append_integer<4>(value, 0x66778899U);
        append_text(value, itch::MarketParticipantId{'A', 'T', 'T', 'R'});
        add(itch::AddOrderWithAttributionMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::OrderExecutedMessage::TYPE);
        append_integer<8>(value, 0x0102030405060708ULL);
        append_integer<4>(value, 0x11223344U);
        append_integer<8>(value, 0x1112131415161718ULL);
        add(itch::OrderExecutedMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::OrderExecutedWithPriceMessage::TYPE);
        append_integer<8>(value, 0x2122232425262728ULL);
        append_integer<4>(value, 0x33445566U);
        append_integer<8>(value, 0x3132333435363738ULL);
        value.push_back(as_byte('Y'));
        append_integer<4>(value, 0x778899AAU);
        add(itch::OrderExecutedWithPriceMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::OrderCancelMessage::TYPE);
        append_integer<8>(value, 0x4142434445464748ULL);
        append_integer<4>(value, 0x44556677U);
        add(itch::OrderCancelMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::OrderDeleteMessage::TYPE);
        append_integer<8>(value, 0x5152535455565758ULL);
        add(itch::OrderDeleteMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::OrderReplaceMessage::TYPE);
        append_integer<8>(value, 0x6162636465666768ULL);
        append_integer<8>(value, 0x7172737475767778ULL);
        append_integer<4>(value, 0x55667788U);
        append_integer<4>(value, 0x99AABBCCU);
        add(itch::OrderReplaceMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::TradeMessage::TYPE);
        append_integer<8>(value, 0x0102030405060708ULL);
        value.push_back(as_byte('S'));
        append_integer<4>(value, 0x11223344U);
        append_text(value, STOCK);
        append_integer<4>(value, 0x55667788U);
        append_integer<8>(value, 0x1112131415161718ULL);
        add(itch::TradeMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::CrossTradeMessage::TYPE);
        append_integer<8>(value, 0x2122232425262728ULL);
        append_text(value, STOCK);
        append_integer<4>(value, 0x33445566U);
        append_integer<8>(value, 0x3132333435363738ULL);
        value.push_back(as_byte('O'));
        add(itch::CrossTradeMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::BrokenTradeMessage::TYPE);
        append_integer<8>(value, 0x4142434445464748ULL);
        add(itch::BrokenTradeMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::NoiiMessage::TYPE);
        append_integer<8>(value, 0x5152535455565758ULL);
        append_integer<8>(value, 0x6162636465666768ULL);
        value.push_back(as_byte('B'));
        append_text(value, STOCK);
        append_integer<4>(value, 0x11223344U);
        append_integer<4>(value, 0x55667788U);
        append_integer<4>(value, 0x99AABBCCU);
        value.push_back(as_byte('C'));
        value.push_back(as_byte('A'));
        add(itch::NoiiMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::RpiiMessage::TYPE);
        append_text(value, STOCK);
        value.push_back(as_byte('B'));
        add(itch::RpiiMessage::TYPE, std::move(value));
    }
    {
        std::vector<std::byte> value;
        append_header(value, itch::LuldAuctionCollarMessage::TYPE);
        append_text(value, STOCK);
        append_integer<4>(value, 0x11223344U);
        append_integer<4>(value, 0x55667788U);
        append_integer<4>(value, 0x99AABBCCU);
        append_integer<4>(value, 0xDDEEFF00U);
        add(itch::LuldAuctionCollarMessage::TYPE, std::move(value));
    }

    return fixtures;
}

class CapturingHandler final : public itch::MessageHandler
{
public:
    std::vector<char> types;
    std::optional<itch::SystemEventMessage> system_event;
    std::optional<itch::StockDirectoryMessage> stock_directory;
    std::optional<itch::StockTradingActionMessage> trading_action;
    std::optional<itch::RegShoRestrictionMessage> reg_sho;
    std::optional<itch::MarketParticipantPositionMessage> participant;
    std::optional<itch::MwcbDeclineLevelMessage> mwcb_decline;
    std::optional<itch::MwcbStatusMessage> mwcb_status;
    std::optional<itch::IpoQuotingPeriodUpdateMessage> ipo;
    std::optional<itch::AddOrderMessage> add;
    std::optional<itch::AddOrderWithAttributionMessage> attributed_add;
    std::optional<itch::OrderExecutedMessage> executed;
    std::optional<itch::OrderExecutedWithPriceMessage> executed_with_price;
    std::optional<itch::OrderCancelMessage> cancel;
    std::optional<itch::OrderDeleteMessage> deleted;
    std::optional<itch::OrderReplaceMessage> replace;
    std::optional<itch::TradeMessage> trade;
    std::optional<itch::CrossTradeMessage> cross_trade;
    std::optional<itch::BrokenTradeMessage> broken_trade;
    std::optional<itch::NoiiMessage> noii;
    std::optional<itch::RpiiMessage> rpii;
    std::optional<itch::LuldAuctionCollarMessage> luld;

protected:
#define CAPTURE_MESSAGE(MessageType, member)                                      \
    bool on_message(const itch::MessageType& message) noexcept override           \
    {                                                                              \
        member = message;                                                          \
        types.push_back(message.type);                                             \
        return true;                                                               \
    }

    CAPTURE_MESSAGE(SystemEventMessage, system_event)
    CAPTURE_MESSAGE(StockDirectoryMessage, stock_directory)
    CAPTURE_MESSAGE(StockTradingActionMessage, trading_action)
    CAPTURE_MESSAGE(RegShoRestrictionMessage, reg_sho)
    CAPTURE_MESSAGE(MarketParticipantPositionMessage, participant)
    CAPTURE_MESSAGE(MwcbDeclineLevelMessage, mwcb_decline)
    CAPTURE_MESSAGE(MwcbStatusMessage, mwcb_status)
    CAPTURE_MESSAGE(IpoQuotingPeriodUpdateMessage, ipo)
    CAPTURE_MESSAGE(AddOrderMessage, add)
    CAPTURE_MESSAGE(AddOrderWithAttributionMessage, attributed_add)
    CAPTURE_MESSAGE(OrderExecutedMessage, executed)
    CAPTURE_MESSAGE(OrderExecutedWithPriceMessage, executed_with_price)
    CAPTURE_MESSAGE(OrderCancelMessage, cancel)
    CAPTURE_MESSAGE(OrderDeleteMessage, deleted)
    CAPTURE_MESSAGE(OrderReplaceMessage, replace)
    CAPTURE_MESSAGE(TradeMessage, trade)
    CAPTURE_MESSAGE(CrossTradeMessage, cross_trade)
    CAPTURE_MESSAGE(BrokenTradeMessage, broken_trade)
    CAPTURE_MESSAGE(NoiiMessage, noii)
    CAPTURE_MESSAGE(RpiiMessage, rpii)
    CAPTURE_MESSAGE(LuldAuctionCollarMessage, luld)

#undef CAPTURE_MESSAGE
};

template <typename Message>
void require_header(const Message& message, char type)
{
    REQUIRE(message.type == type);
    REQUIRE(message.stock_locate == STOCK_LOCATE);
    REQUIRE(message.tracking_number == TRACKING_NUMBER);
    REQUIRE(message.timestamp == TIMESTAMP);
}

[[nodiscard]] std::vector<std::byte> join_framed(
    const std::vector<Fixture>& fixtures)
{
    std::vector<std::byte> result;
    for (const Fixture& fixture : fixtures)
    {
        const std::vector<std::byte> frame = frame_message(fixture.message);
        result.insert(result.end(), frame.begin(), frame.end());
    }
    return result;
}

} // namespace

TEST_CASE("Every supported ITCH field decodes from an independent byte fixture",
          "[itch_fixture]")
{
    const std::vector<Fixture> fixtures = make_known_fixtures();
    REQUIRE(fixtures.size() == 21);

    for (const Fixture& fixture : fixtures)
    {
        CAPTURE(fixture.type);
        REQUIRE(std::to_integer<unsigned char>(fixture.message.front()) ==
                static_cast<unsigned char>(fixture.type));
    }

    CapturingHandler handler;
    itch::Decoder decoder(handler);
    const std::vector<std::byte> input = join_framed(fixtures);
    REQUIRE(decoder.process(input) == itch::DecodeStatus::OK);
    REQUIRE(decoder.finish() == itch::DecodeStatus::OK);
    REQUIRE(handler.types.size() == fixtures.size());

    const auto& system = *handler.system_event;
    require_header(system, 'S');
    REQUIRE(system.event_code == 'O');

    const auto& directory = *handler.stock_directory;
    require_header(directory, 'R');
    REQUIRE(directory.stock == STOCK);
    REQUIRE(directory.market_category == 'Q');
    REQUIRE(directory.financial_status_indicator == 'N');
    REQUIRE(directory.round_lot_size == 0x11223344U);
    REQUIRE(directory.round_lots_only == 'Y');
    REQUIRE(directory.issue_classification == 'A');
    REQUIRE(directory.issue_sub_type == itch::IssueSubType{'B', 'C'});
    REQUIRE(directory.authenticity == 'P');
    REQUIRE(directory.short_sale_threshold_indicator == 'Y');
    REQUIRE(directory.ipo_flag == 'N');
    REQUIRE(directory.luld_reference_price_tier == '1');
    REQUIRE(directory.etp_flag == 'Y');
    REQUIRE(directory.etp_leverage_factor == 0x55667788U);
    REQUIRE(directory.inverse_indicator == 'N');

    const auto& action = *handler.trading_action;
    require_header(action, 'H');
    REQUIRE(action.stock == STOCK);
    REQUIRE(action.trading_state == 'H');
    REQUIRE(action.reserved == ' ');
    REQUIRE(action.reason == itch::TradingReason{'T', '1', '2', '3'});

    const auto& reg_sho = *handler.reg_sho;
    require_header(reg_sho, 'Y');
    REQUIRE(reg_sho.stock == STOCK);
    REQUIRE(reg_sho.action == '1');

    const auto& participant = *handler.participant;
    require_header(participant, 'L');
    REQUIRE(participant.participant_id ==
            itch::MarketParticipantId{'M', 'P', 'I', 'D'});
    REQUIRE(participant.stock == STOCK);
    REQUIRE(participant.primary_market_maker == 'Y');
    REQUIRE(participant.market_maker_mode == 'N');
    REQUIRE(participant.market_participant_state == 'A');

    const auto& decline = *handler.mwcb_decline;
    require_header(decline, 'V');
    REQUIRE(decline.level_1 == 0x0102030405060708ULL);
    REQUIRE(decline.level_2 == 0x1112131415161718ULL);
    REQUIRE(decline.level_3 == 0x2122232425262728ULL);

    const auto& status = *handler.mwcb_status;
    require_header(status, 'W');
    REQUIRE(status.breached_level == '2');

    const auto& ipo = *handler.ipo;
    require_header(ipo, 'K');
    REQUIRE(ipo.stock == STOCK);
    REQUIRE(ipo.release_time == 0x11223344U);
    REQUIRE(ipo.release_qualifier == 'A');
    REQUIRE(ipo.price == 0x55667788U);

    const auto& add = *handler.add;
    require_header(add, 'A');
    REQUIRE(add.order_reference_number == 0x0102030405060708ULL);
    REQUIRE(add.buy_sell_indicator == 'B');
    REQUIRE(add.shares == 0x11223344U);
    REQUIRE(add.stock == STOCK);
    REQUIRE(add.price == 0x55667788U);

    const auto& attributed = *handler.attributed_add;
    require_header(attributed, 'F');
    REQUIRE(attributed.order_reference_number == 0x1112131415161718ULL);
    REQUIRE(attributed.buy_sell_indicator == 'S');
    REQUIRE(attributed.shares == 0x22334455U);
    REQUIRE(attributed.stock == STOCK);
    REQUIRE(attributed.price == 0x66778899U);
    REQUIRE(attributed.attribution ==
            itch::MarketParticipantId{'A', 'T', 'T', 'R'});

    const auto& executed = *handler.executed;
    require_header(executed, 'E');
    REQUIRE(executed.order_reference_number == 0x0102030405060708ULL);
    REQUIRE(executed.executed_shares == 0x11223344U);
    REQUIRE(executed.match_number == 0x1112131415161718ULL);

    const auto& executed_price = *handler.executed_with_price;
    require_header(executed_price, 'C');
    REQUIRE(executed_price.order_reference_number ==
            0x2122232425262728ULL);
    REQUIRE(executed_price.executed_shares == 0x33445566U);
    REQUIRE(executed_price.match_number == 0x3132333435363738ULL);
    REQUIRE(executed_price.printable == 'Y');
    REQUIRE(executed_price.execution_price == 0x778899AAU);

    const auto& cancel = *handler.cancel;
    require_header(cancel, 'X');
    REQUIRE(cancel.order_reference_number == 0x4142434445464748ULL);
    REQUIRE(cancel.cancelled_shares == 0x44556677U);

    const auto& deleted = *handler.deleted;
    require_header(deleted, 'D');
    REQUIRE(deleted.order_reference_number == 0x5152535455565758ULL);

    const auto& replace = *handler.replace;
    require_header(replace, 'U');
    REQUIRE(replace.original_order_reference_number ==
            0x6162636465666768ULL);
    REQUIRE(replace.new_order_reference_number == 0x7172737475767778ULL);
    REQUIRE(replace.shares == 0x55667788U);
    REQUIRE(replace.price == 0x99AABBCCU);

    const auto& trade = *handler.trade;
    require_header(trade, 'P');
    REQUIRE(trade.order_reference_number == 0x0102030405060708ULL);
    REQUIRE(trade.buy_sell_indicator == 'S');
    REQUIRE(trade.shares == 0x11223344U);
    REQUIRE(trade.stock == STOCK);
    REQUIRE(trade.price == 0x55667788U);
    REQUIRE(trade.match_number == 0x1112131415161718ULL);

    const auto& cross = *handler.cross_trade;
    require_header(cross, 'Q');
    REQUIRE(cross.shares == 0x2122232425262728ULL);
    REQUIRE(cross.stock == STOCK);
    REQUIRE(cross.cross_price == 0x33445566U);
    REQUIRE(cross.match_number == 0x3132333435363738ULL);
    REQUIRE(cross.cross_type == 'O');

    const auto& broken = *handler.broken_trade;
    require_header(broken, 'B');
    REQUIRE(broken.match_number == 0x4142434445464748ULL);

    const auto& noii = *handler.noii;
    require_header(noii, 'I');
    REQUIRE(noii.paired_shares == 0x5152535455565758ULL);
    REQUIRE(noii.imbalance_shares == 0x6162636465666768ULL);
    REQUIRE(noii.imbalance_direction == 'B');
    REQUIRE(noii.stock == STOCK);
    REQUIRE(noii.far_price == 0x11223344U);
    REQUIRE(noii.near_price == 0x55667788U);
    REQUIRE(noii.current_reference_price == 0x99AABBCCU);
    REQUIRE(noii.cross_type == 'C');
    REQUIRE(noii.price_variation_indicator == 'A');

    const auto& rpii = *handler.rpii;
    require_header(rpii, 'N');
    REQUIRE(rpii.stock == STOCK);
    REQUIRE(rpii.interest_flag == 'B');

    const auto& luld = *handler.luld;
    require_header(luld, 'J');
    REQUIRE(luld.stock == STOCK);
    REQUIRE(luld.auction_collar_reference_price == 0x11223344U);
    REQUIRE(luld.upper_auction_collar_price == 0x55667788U);
    REQUIRE(luld.lower_auction_collar_price == 0x99AABBCCU);
    REQUIRE(luld.auction_collar_extension == 0xDDEEFF00U);
}

TEST_CASE("Every ITCH fixture survives every stream boundary",
          "[itch_fixture]")
{
    const std::vector<Fixture> fixtures = make_known_fixtures();

    for (const Fixture& fixture : fixtures)
    {
        CAPTURE(fixture.type);
        REQUIRE(fixture.message.size() <= itch::FrameDecoder::MAX_MESSAGE_SIZE);
        const std::vector<std::byte> frame = frame_message(fixture.message);

        {
            CapturingHandler handler;
            itch::Decoder decoder(handler);
            REQUIRE(decoder.process(frame) == itch::DecodeStatus::OK);
            REQUIRE(decoder.finish() == itch::DecodeStatus::OK);
            REQUIRE(handler.types == std::vector<char>{fixture.type});
        }

        {
            for (std::size_t split = 0; split <= frame.size(); ++split)
            {
                CapturingHandler handler;
                itch::Decoder decoder(handler);
                REQUIRE(decoder.process(std::span(frame).first(split)) ==
                        itch::DecodeStatus::OK);
                REQUIRE(decoder.process(std::span(frame).subspan(split)) ==
                        itch::DecodeStatus::OK);
                REQUIRE(decoder.finish() == itch::DecodeStatus::OK);
                REQUIRE(handler.types == std::vector<char>{fixture.type});
            }
        }

        {
            CapturingHandler handler;
            itch::Decoder decoder(handler);
            for (const std::byte value : frame)
            {
                REQUIRE(decoder.process(std::span(&value, 1)) ==
                        itch::DecodeStatus::OK);
            }
            REQUIRE(decoder.finish() == itch::DecodeStatus::OK);
            REQUIRE(handler.types == std::vector<char>{fixture.type});
        }
    }
}

TEST_CASE("ITCH decoder handles many messages in one input chunk",
          "[itch_fixture]")
{
    const std::vector<Fixture> fixtures = make_known_fixtures();
    const std::vector<std::byte> input = join_framed(fixtures);

    std::vector<char> expected;
    expected.reserve(fixtures.size());
    for (const Fixture& fixture : fixtures)
        expected.push_back(fixture.type);

    CapturingHandler handler;
    itch::Decoder decoder(handler);
    REQUIRE(decoder.process(input) == itch::DecodeStatus::OK);
    REQUIRE(decoder.finish() == itch::DecodeStatus::OK);
    REQUIRE(handler.types == expected);
}

TEST_CASE("Deterministic malformed ITCH inputs remain memory safe",
          "[itch_fixture]")
{
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (std::size_t iteration = 0; iteration < 2'000; ++iteration)
    {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;

        const std::size_t length = static_cast<std::size_t>(state % 129U);
        std::vector<std::byte> input(length);
        for (std::byte& value : input)
        {
            state ^= state << 13U;
            state ^= state >> 7U;
            state ^= state << 17U;
            value = as_byte(static_cast<std::uint8_t>(state));
        }

        CapturingHandler handler;
        itch::Decoder decoder(handler);
        static_cast<void>(decoder.process(input));
        static_cast<void>(decoder.finish());
    }

    SUCCEED();
}
