#include "providers/nasdaq/itch/decoder.h"
#include "providers/nasdaq/itch/market_adapter.h"

#include "matching/events/market_handler.h"
#include "matching/manager/market_manager.h"
#include "matching/order.h"

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

void append_header(std::vector<std::byte>& output,
                   char type,
                   std::uint16_t stock_locate)
{
    output.push_back(as_byte(static_cast<std::uint8_t>(type)));
    append_integer<2>(output, stock_locate);
    append_integer<2>(output, 1U);
    append_integer<6>(output, 2U);
}

void append_frame(std::vector<std::byte>& output,
                  std::span<const std::byte> message)
{
    append_integer<2>(output, message.size());
    output.insert(output.end(), message.begin(), message.end());
}

[[nodiscard]] std::vector<std::byte> directory_message(
    std::uint16_t stock_locate,
    const itch::StockName& stock)
{
    std::vector<std::byte> message;
    append_header(message, itch::StockDirectoryMessage::TYPE, stock_locate);
    append_text(message, stock);
    message.push_back(as_byte('Q'));
    message.push_back(as_byte('N'));
    append_integer<4>(message, 100U);
    message.push_back(as_byte('N'));
    message.push_back(as_byte('A'));
    append_text(message, itch::IssueSubType{' ', ' '});
    message.push_back(as_byte('P'));
    message.push_back(as_byte(' '));
    message.push_back(as_byte('N'));
    message.push_back(as_byte('1'));
    message.push_back(as_byte('N'));
    append_integer<4>(message, 0U);
    message.push_back(as_byte('N'));
    return message;
}

[[nodiscard]] std::vector<std::byte> add_message(std::uint16_t stock_locate,
                                                 std::uint64_t order_id,
                                                 char side,
                                                 std::uint32_t shares,
                                                 std::uint32_t price,
                                                 bool attributed = false)
{
    std::vector<std::byte> message;
    append_header(message,
                  attributed ? itch::AddOrderWithAttributionMessage::TYPE
                             : itch::AddOrderMessage::TYPE,
                  stock_locate);
    append_integer<8>(message, order_id);
    message.push_back(as_byte(static_cast<std::uint8_t>(side)));
    append_integer<4>(message, shares);
    append_text(message,
                itch::StockName{'T', 'E', 'S', 'T', ' ', ' ', ' ', ' '});
    append_integer<4>(message, price);
    if (attributed)
        append_text(message, itch::MarketParticipantId{'M', 'P', 'I', 'D'});
    return message;
}

[[nodiscard]] std::vector<std::byte> execute_message(
    std::uint16_t stock_locate,
    std::uint64_t order_id,
    std::uint32_t shares)
{
    std::vector<std::byte> message;
    append_header(message, itch::OrderExecutedMessage::TYPE, stock_locate);
    append_integer<8>(message, order_id);
    append_integer<4>(message, shares);
    append_integer<8>(message, 10U);
    return message;
}

[[nodiscard]] std::vector<std::byte> execute_with_price_message(
    std::uint16_t stock_locate,
    std::uint64_t order_id,
    std::uint32_t shares,
    std::uint32_t price)
{
    std::vector<std::byte> message;
    append_header(
        message, itch::OrderExecutedWithPriceMessage::TYPE, stock_locate);
    append_integer<8>(message, order_id);
    append_integer<4>(message, shares);
    append_integer<8>(message, 11U);
    message.push_back(as_byte('Y'));
    append_integer<4>(message, price);
    return message;
}

[[nodiscard]] std::vector<std::byte> cancel_message(std::uint16_t stock_locate,
                                                    std::uint64_t order_id,
                                                    std::uint32_t shares)
{
    std::vector<std::byte> message;
    append_header(message, itch::OrderCancelMessage::TYPE, stock_locate);
    append_integer<8>(message, order_id);
    append_integer<4>(message, shares);
    return message;
}

[[nodiscard]] std::vector<std::byte> delete_message(std::uint16_t stock_locate,
                                                    std::uint64_t order_id)
{
    std::vector<std::byte> message;
    append_header(message, itch::OrderDeleteMessage::TYPE, stock_locate);
    append_integer<8>(message, order_id);
    return message;
}

[[nodiscard]] std::vector<std::byte> replace_message(
    std::uint16_t stock_locate,
    std::uint64_t old_order_id,
    std::uint64_t new_order_id,
    std::uint32_t shares,
    std::uint32_t price)
{
    std::vector<std::byte> message;
    append_header(message, itch::OrderReplaceMessage::TYPE, stock_locate);
    append_integer<8>(message, old_order_id);
    append_integer<8>(message, new_order_id);
    append_integer<4>(message, shares);
    append_integer<4>(message, price);
    return message;
}

class RecordingMarketHandler final : public matching::MarketHandler
{
public:
    std::size_t added_symbols{0};
    std::size_t added_books{0};
    std::size_t added_orders{0};
    std::size_t updated_orders{0};
    std::size_t deleted_orders{0};
    std::vector<matching::Price> execution_prices;
    std::vector<matching::Quantity> execution_quantities;

protected:
    void on_add_symbol(const matching::Symbol&) noexcept override
    {
        ++added_symbols;
    }
    void on_add_order_book(const matching::OrderBook&) noexcept override
    {
        ++added_books;
    }
    void on_add_order(const matching::Order&) noexcept override
    {
        ++added_orders;
    }
    void on_update_order(const matching::Order&) noexcept override
    {
        ++updated_orders;
    }
    void on_delete_order(const matching::Order&) noexcept override
    {
        ++deleted_orders;
    }
    void on_execute_order(const matching::Order&,
                          matching::Price price,
                          matching::Quantity quantity) noexcept override
    {
        execution_prices.push_back(price);
        execution_quantities.push_back(quantity);
    }
};

[[nodiscard]] itch::DecodeStatus process_messages(
    itch::Decoder& decoder,
    const std::vector<std::vector<std::byte>>& messages)
{
    std::vector<std::byte> input;
    for (const std::vector<std::byte>& message : messages)
        append_frame(input, message);
    return decoder.process(input);
}

} // namespace

TEST_CASE("ITCH adapter reconstructs one complete historical order lifecycle",
          "[itch_adapter]")
{
    RecordingMarketHandler market_handler;
    matching::MarketManager market(market_handler);
    itch::MarketAdapter adapter(
        market, itch::PriceConversionPolicy::ROUND_TO_NEAREST_CENT);
    itch::Decoder decoder(adapter);

    REQUIRE(process_messages(
                decoder,
                {directory_message(
                     7, itch::StockName{'T', 'E', 'S', 'T', ' ', ' ', ' ', ' '}),
                 add_message(7, 100, 'B', 100, 1'852'500),
                 cancel_message(7, 100, 10),
                 execute_message(7, 100, 20),
                 execute_with_price_message(7, 100, 30, 1'853'000),
                 replace_message(7, 100, 101, 50, 1'854'955),
                 delete_message(7, 101)}) == itch::DecodeStatus::OK);
    REQUIRE(decoder.finish() == itch::DecodeStatus::OK);

    REQUIRE(adapter.status() == itch::MarketAdapterStatus::OK);
    REQUIRE(adapter.engine_error() == matching::ErrorCode::OK);
    REQUIRE(market.symbol_count() == 1);
    REQUIRE(market.order_book_count() == 1);
    REQUIRE(market.active_order_count() == 0);
    REQUIRE(market.find_order(100) == nullptr);
    REQUIRE(market.find_order(101) == nullptr);
    REQUIRE(market.find_order_book(7)->empty());

    const itch::MarketAdapterStatistics& statistics = adapter.statistics();
    REQUIRE(statistics.messages == 7);
    REQUIRE(statistics.symbol_directory_messages == 1);
    REQUIRE(statistics.add_order_messages == 1);
    REQUIRE(statistics.cancel_order_messages == 1);
    REQUIRE(statistics.execute_order_messages == 2);
    REQUIRE(statistics.replace_order_messages == 1);
    REQUIRE(statistics.delete_order_messages == 1);
    REQUIRE(statistics.exact_cent_prices == 2);
    REQUIRE(statistics.rounded_prices == 1);
    REQUIRE(statistics.minimum_cent_prices == 0);

    REQUIRE(market_handler.added_symbols == 1);
    REQUIRE(market_handler.added_books == 1);
    REQUIRE(market_handler.added_orders == 2);
    REQUIRE(market_handler.updated_orders == 3);
    REQUIRE(market_handler.deleted_orders == 2);
    REQUIRE(market_handler.execution_prices ==
            std::vector<matching::Price>{18'525, 18'530});
    REQUIRE(market_handler.execution_quantities ==
            std::vector<matching::Quantity>{20, 30});
}

TEST_CASE("ITCH adapter price conversion is explicit and observable",
          "[itch_adapter]")
{
    SECTION("exact-cent mode rejects a sub-cent price")
    {
        matching::MarketManager market;
        itch::MarketAdapter adapter(
            market, itch::PriceConversionPolicy::REQUIRE_EXACT_CENT);
        itch::Decoder decoder(adapter);

        REQUIRE(process_messages(
                    decoder,
                    {directory_message(
                         1,
                         itch::StockName{'O', 'N', 'E', ' ', ' ', ' ', ' ', ' '}),
                     add_message(1, 1, 'B', 10, 10'001)}) ==
                itch::DecodeStatus::MESSAGE_REJECTED);
        REQUIRE(adapter.status() ==
                itch::MarketAdapterStatus::PRICE_NOT_REPRESENTABLE);
        REQUIRE(market.active_order_count() == 0);
    }

    SECTION("rounding mode aggregates a positive sub-half-cent price at one cent")
    {
        matching::MarketManager market;
        itch::MarketAdapter adapter(
            market, itch::PriceConversionPolicy::ROUND_TO_NEAREST_CENT);
        itch::Decoder decoder(adapter);

        REQUIRE(process_messages(
                    decoder,
                    {directory_message(
                         2,
                         itch::StockName{'T', 'W', 'O', ' ', ' ', ' ', ' ', ' '}),
                     add_message(2, 2, 'S', 10, 1)}) == itch::DecodeStatus::OK);
        const matching::Order* order = market.find_order(2);
        REQUIRE(order != nullptr);
        REQUIRE(order->price == 1);
        REQUIRE(adapter.statistics().rounded_prices == 1);
        REQUIRE(adapter.statistics().minimum_cent_prices == 1);
    }
}

TEST_CASE("ITCH adapter rejects replay when automatic matching is enabled",
          "[itch_adapter]")
{
    matching::MarketManager market;
    market.enable_matching();
    itch::MarketAdapter adapter(
        market, itch::PriceConversionPolicy::ROUND_TO_NEAREST_CENT);
    itch::Decoder decoder(adapter);

    REQUIRE(process_messages(
                decoder,
                {directory_message(
                    1, itch::StockName{'O', 'N', 'E', ' ', ' ', ' ', ' ', ' '})}) ==
            itch::DecodeStatus::MESSAGE_REJECTED);
    REQUIRE(adapter.status() ==
            itch::MarketAdapterStatus::MATCHING_MUST_BE_DISABLED);
    REQUIRE(market.symbol_count() == 0);
    REQUIRE(market.order_book_count() == 0);
}

TEST_CASE("ITCH adapter preserves symbol routing and exposes engine rejection",
          "[itch_adapter]")
{
    matching::MarketManager market;
    itch::MarketAdapter adapter(
        market, itch::PriceConversionPolicy::ROUND_TO_NEAREST_CENT);
    itch::Decoder decoder(adapter);

    REQUIRE(process_messages(
                decoder,
                {directory_message(
                     1, itch::StockName{'O', 'N', 'E', ' ', ' ', ' ', ' ', ' '}),
                 directory_message(
                     2, itch::StockName{'T', 'W', 'O', ' ', ' ', ' ', ' ', ' '}),
                 add_message(1, 10, 'B', 5, 10'000, true),
                 add_message(2, 20, 'S', 7, 20'000)}) == itch::DecodeStatus::OK);

    REQUIRE(market.find_order(10)->symbol_id == 1);
    REQUIRE(market.find_order(20)->symbol_id == 2);
    REQUIRE(market.find_order_book(1)->best_bid()->total_quantity == 5);
    REQUIRE(market.find_order_book(2)->best_ask()->total_quantity == 7);
    REQUIRE(adapter.statistics().add_order_messages == 2);
    REQUIRE(adapter.statistics().count('F') == 1);

    REQUIRE(process_messages(decoder, {add_message(2, 10, 'S', 1, 20'000)}) ==
            itch::DecodeStatus::MESSAGE_REJECTED);
    REQUIRE(adapter.status() == itch::MarketAdapterStatus::ENGINE_REJECTED);
    REQUIRE(adapter.engine_error() == matching::ErrorCode::ORDER_DUPLICATE);
    REQUIRE(market.active_order_count() == 2);
}

TEST_CASE("ITCH adapter counts ignored and unknown messages without mutating market",
          "[itch_adapter]")
{
    matching::MarketManager market;
    itch::MarketAdapter adapter(
        market, itch::PriceConversionPolicy::ROUND_TO_NEAREST_CENT);
    itch::Decoder decoder(adapter);

    std::vector<std::byte> system;
    append_header(system, itch::SystemEventMessage::TYPE, 0);
    system.push_back(as_byte('O'));
    const std::vector<std::byte> unknown{as_byte('Z'), as_byte(0xAA)};

    REQUIRE(process_messages(decoder, {system, unknown}) == itch::DecodeStatus::OK);
    REQUIRE(decoder.finish() == itch::DecodeStatus::OK);
    REQUIRE(adapter.statistics().messages == 2);
    REQUIRE(adapter.statistics().ignored_messages == 1);
    REQUIRE(adapter.statistics().unknown_messages == 1);
    REQUIRE(adapter.statistics().count('S') == 1);
    REQUIRE(adapter.statistics().count('Z') == 1);
    REQUIRE(market.symbol_count() == 0);
    REQUIRE(market.active_order_count() == 0);
}

TEST_CASE("ITCH adapter accepts only identical repeated directory metadata",
          "[itch_adapter]")
{
    matching::MarketManager market;
    itch::MarketAdapter adapter(
        market, itch::PriceConversionPolicy::ROUND_TO_NEAREST_CENT);
    itch::Decoder decoder(adapter);
    const itch::StockName name{'O', 'N', 'E', ' ', ' ', ' ', ' ', ' '};

    REQUIRE(process_messages(
                decoder,
                {directory_message(1, name), directory_message(1, name)}) ==
            itch::DecodeStatus::OK);
    REQUIRE(adapter.statistics().symbol_directory_messages == 2);
    REQUIRE(adapter.statistics().repeated_directory_messages == 1);
    REQUIRE(market.symbol_count() == 1);
    REQUIRE(market.order_book_count() == 1);

    REQUIRE(process_messages(
                decoder,
                {directory_message(
                    1,
                    itch::StockName{'T', 'W', 'O', ' ', ' ', ' ', ' ', ' '})}) ==
            itch::DecodeStatus::MESSAGE_REJECTED);
    REQUIRE(adapter.status() == itch::MarketAdapterStatus::ENGINE_REJECTED);
    REQUIRE(adapter.engine_error() == matching::ErrorCode::SYMBOL_DUPLICATE);
    REQUIRE(market.symbol_count() == 1);
}
