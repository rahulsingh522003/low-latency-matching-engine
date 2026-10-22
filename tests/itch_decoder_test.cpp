#include "providers/nasdaq/itch/decoder.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

namespace itch = providers::nasdaq::itch;

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
        output.push_back(as_byte(
            static_cast<std::uint8_t>(widened >> shift)));
    }
}

template <std::size_t Width>
void append_text(std::vector<std::byte>& output,
                 const std::array<char, Width>& text)
{
    for (const char character : text)
        output.push_back(as_byte(static_cast<std::uint8_t>(character)));
}

[[nodiscard]] std::vector<std::byte> frame_message(
    std::span<const std::byte> message)
{
    std::vector<std::byte> frame;
    frame.reserve(message.size() + itch::FrameDecoder::LENGTH_PREFIX_SIZE);
    append_integer<2>(frame, message.size());
    frame.insert(frame.end(), message.begin(), message.end());
    return frame;
}

class RecordingHandler final : public itch::MessageHandler
{
public:
    itch::AddOrderMessage add_order{};
    itch::StockTradingActionMessage trading_action{};
    itch::AddOrderWithAttributionMessage attributed_order{};
    char unknown_type{0};
    std::size_t add_order_count{0};
    std::size_t trading_action_count{0};
    std::size_t attributed_order_count{0};
    std::size_t unknown_count{0};
    bool accept{true};

protected:
    bool on_message(const itch::AddOrderMessage& message) noexcept override
    {
        add_order = message;
        ++add_order_count;
        return accept;
    }

    bool on_message(
        const itch::StockTradingActionMessage& message) noexcept override
    {
        trading_action = message;
        ++trading_action_count;
        return accept;
    }

    bool on_message(
        const itch::AddOrderWithAttributionMessage& message) noexcept override
    {
        attributed_order = message;
        ++attributed_order_count;
        return accept;
    }

    bool on_message(const itch::UnknownMessage& message) noexcept override
    {
        unknown_type = message.type;
        ++unknown_count;
        return accept;
    }
};

[[nodiscard]] std::vector<std::byte> make_add_order_message()
{
    std::vector<std::byte> message;
    message.reserve(itch::AddOrderMessage::WIRE_SIZE);
    message.push_back(as_byte(itch::AddOrderMessage::TYPE));
    append_integer<2>(message, 0x1234U);
    append_integer<2>(message, 0x5678U);
    append_integer<6>(message, 0x010203040506ULL);
    append_integer<8>(message, 0x0102030405060708ULL);
    message.push_back(as_byte('B'));
    append_integer<4>(message, 0x11223344U);
    append_text(message, itch::StockName{'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '});
    append_integer<4>(message, 0x55667788U);
    return message;
}

} // namespace

TEST_CASE("ITCH decoder populates every Add Order field", "[itch_decoder]")
{
    const std::vector<std::byte> message = make_add_order_message();
    REQUIRE(message.size() == itch::AddOrderMessage::WIRE_SIZE);
    const std::vector<std::byte> frame = frame_message(message);

    RecordingHandler handler;
    itch::Decoder decoder(handler);
    REQUIRE(decoder.process(frame) == itch::DecodeStatus::OK);
    REQUIRE(decoder.finish() == itch::DecodeStatus::OK);
    REQUIRE(handler.add_order_count == 1);

    const itch::AddOrderMessage& decoded = handler.add_order;
    REQUIRE(decoded.type == 'A');
    REQUIRE(decoded.stock_locate == 0x1234U);
    REQUIRE(decoded.tracking_number == 0x5678U);
    REQUIRE(decoded.timestamp == 0x010203040506ULL);
    REQUIRE(decoded.order_reference_number == 0x0102030405060708ULL);
    REQUIRE(decoded.buy_sell_indicator == 'B');
    REQUIRE(decoded.shares == 0x11223344U);
    REQUIRE(decoded.stock ==
            itch::StockName{'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '});
    REQUIRE(decoded.price == 0x55667788U);
}

TEST_CASE("ITCH decoder retains complete four-byte text fields", "[itch_decoder]")
{
    std::vector<std::byte> trading_action;
    trading_action.reserve(itch::StockTradingActionMessage::WIRE_SIZE);
    trading_action.push_back(as_byte(itch::StockTradingActionMessage::TYPE));
    append_integer<2>(trading_action, 7U);
    append_integer<2>(trading_action, 8U);
    append_integer<6>(trading_action, 9U);
    append_text(
        trading_action,
        itch::StockName{'T', 'E', 'S', 'T', ' ', ' ', ' ', ' '});
    trading_action.push_back(as_byte('H'));
    trading_action.push_back(as_byte(' '));
    append_text(trading_action, itch::TradingReason{'T', '1', '2', '3'});
    REQUIRE(trading_action.size() ==
            itch::StockTradingActionMessage::WIRE_SIZE);

    std::vector<std::byte> attributed = make_add_order_message();
    attributed[0] = as_byte(itch::AddOrderWithAttributionMessage::TYPE);
    append_text(attributed,
                itch::MarketParticipantId{'M', 'P', 'I', 'D'});
    REQUIRE(attributed.size() ==
            itch::AddOrderWithAttributionMessage::WIRE_SIZE);

    std::vector<std::byte> input = frame_message(trading_action);
    const std::vector<std::byte> attributed_frame = frame_message(attributed);
    input.insert(input.end(), attributed_frame.begin(), attributed_frame.end());

    RecordingHandler handler;
    itch::Decoder decoder(handler);
    REQUIRE(decoder.process(input) == itch::DecodeStatus::OK);
    REQUIRE(handler.trading_action_count == 1);
    REQUIRE(handler.trading_action.reason ==
            itch::TradingReason{'T', '1', '2', '3'});
    REQUIRE(handler.attributed_order_count == 1);
    REQUIRE(handler.attributed_order.attribution ==
            itch::MarketParticipantId{'M', 'P', 'I', 'D'});
}

TEST_CASE("Known ITCH types require their exact wire size", "[itch_decoder]")
{
    std::vector<std::byte> message = make_add_order_message();
    message.pop_back();
    const std::vector<std::byte> frame = frame_message(message);

    RecordingHandler handler;
    itch::Decoder decoder(handler);
    REQUIRE(decoder.process(frame) == itch::DecodeStatus::INVALID_MESSAGE_SIZE);
    REQUIRE(decoder.failed());
    REQUIRE(handler.add_order_count == 0);
    REQUIRE(decoder.process(frame) == itch::DecodeStatus::FAILED);

    decoder.reset();
    REQUIRE_FALSE(decoder.failed());
    REQUIRE(decoder.process(frame_message(make_add_order_message())) ==
            itch::DecodeStatus::OK);
    REQUIRE(handler.add_order_count == 1);
}

TEST_CASE("Unknown ITCH types are reported without reading their payload",
          "[itch_decoder]")
{
    const std::array message{
        as_byte('Z'), as_byte(0xDE), as_byte(0xAD), as_byte(0xBE), as_byte(0xEF)};

    RecordingHandler handler;
    itch::Decoder decoder(handler);
    REQUIRE(decoder.process(frame_message(message)) == itch::DecodeStatus::OK);
    REQUIRE(handler.unknown_count == 1);
    REQUIRE(handler.unknown_type == 'Z');
    REQUIRE_FALSE(decoder.failed());

    handler.accept = false;
    REQUIRE(decoder.process(frame_message(message)) ==
            itch::DecodeStatus::MESSAGE_REJECTED);
    REQUIRE(decoder.failed());
}
