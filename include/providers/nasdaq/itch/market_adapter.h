#ifndef PROVIDERS_NASDAQ_ITCH_MARKET_ADAPTER_H
#define PROVIDERS_NASDAQ_ITCH_MARKET_ADAPTER_H

#include "message_handler.h"

#include "matching/errors.h"
#include "matching/manager/market_manager.h"
#include "matching/types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace providers::nasdaq::itch {

enum class PriceConversionPolicy : std::uint8_t
{
    REQUIRE_EXACT_CENT = 0,
    ROUND_TO_NEAREST_CENT = 1
};

enum class MarketAdapterStatus : std::uint8_t
{
    OK = 0,
    MATCHING_MUST_BE_DISABLED,
    INVALID_ORDER_SIDE,
    PRICE_NOT_REPRESENTABLE,
    ENGINE_REJECTED,
    ALLOCATION_FAILURE,
    INTERNAL_FAILURE
};

struct MarketAdapterStatistics
{
    std::uint64_t messages{0};
    std::uint64_t ignored_messages{0};
    std::uint64_t unknown_messages{0};

    std::uint64_t symbol_directory_messages{0};
    std::uint64_t repeated_directory_messages{0};
    std::uint64_t add_order_messages{0};
    std::uint64_t execute_order_messages{0};
    std::uint64_t cancel_order_messages{0};
    std::uint64_t delete_order_messages{0};
    std::uint64_t replace_order_messages{0};

    std::uint64_t exact_cent_prices{0};
    std::uint64_t rounded_prices{0};
    std::uint64_t minimum_cent_prices{0};

    std::array<std::uint64_t, 256> message_type_counts{};

    [[nodiscard]] std::uint64_t count(char message_type) const noexcept
    {
        return message_type_counts[static_cast<unsigned char>(message_type)];
    }
};

class MarketAdapter final : public MessageHandler
{
public:
    MarketAdapter(matching::MarketManager& market,
                  PriceConversionPolicy price_policy) noexcept;
    MarketAdapter(const MarketAdapter&) = delete;
    MarketAdapter(MarketAdapter&&) = delete;
    ~MarketAdapter() noexcept override = default;

    MarketAdapter& operator=(const MarketAdapter&) = delete;
    MarketAdapter& operator=(MarketAdapter&&) = delete;

    [[nodiscard]] PriceConversionPolicy price_policy() const noexcept
    {
        return price_policy_;
    }
    [[nodiscard]] MarketAdapterStatus status() const noexcept { return status_; }
    [[nodiscard]] matching::ErrorCode engine_error() const noexcept
    {
        return engine_error_;
    }
    [[nodiscard]] const MarketAdapterStatistics& statistics() const noexcept
    {
        return statistics_;
    }

protected:
    bool on_message(const SystemEventMessage& message) noexcept override;
    bool on_message(const StockDirectoryMessage& message) noexcept override;
    bool on_message(const StockTradingActionMessage& message) noexcept override;
    bool on_message(const RegShoRestrictionMessage& message) noexcept override;
    bool on_message(
        const MarketParticipantPositionMessage& message) noexcept override;
    bool on_message(const MwcbDeclineLevelMessage& message) noexcept override;
    bool on_message(const MwcbStatusMessage& message) noexcept override;
    bool on_message(
        const IpoQuotingPeriodUpdateMessage& message) noexcept override;
    bool on_message(const AddOrderMessage& message) noexcept override;
    bool on_message(
        const AddOrderWithAttributionMessage& message) noexcept override;
    bool on_message(const OrderExecutedMessage& message) noexcept override;
    bool on_message(
        const OrderExecutedWithPriceMessage& message) noexcept override;
    bool on_message(const OrderCancelMessage& message) noexcept override;
    bool on_message(const OrderDeleteMessage& message) noexcept override;
    bool on_message(const OrderReplaceMessage& message) noexcept override;
    bool on_message(const TradeMessage& message) noexcept override;
    bool on_message(const CrossTradeMessage& message) noexcept override;
    bool on_message(const BrokenTradeMessage& message) noexcept override;
    bool on_message(const NoiiMessage& message) noexcept override;
    bool on_message(const RpiiMessage& message) noexcept override;
    bool on_message(const LuldAuctionCollarMessage& message) noexcept override;
    bool on_message(const UnknownMessage& message) noexcept override;

private:
    static constexpr std::uint32_t ITCH_UNITS_PER_CENT = 100;

    matching::MarketManager& market_;
    PriceConversionPolicy price_policy_;
    MarketAdapterStatus status_{MarketAdapterStatus::OK};
    matching::ErrorCode engine_error_{matching::ErrorCode::OK};
    MarketAdapterStatistics statistics_;

    [[nodiscard]] bool begin_message(char message_type) noexcept;
    [[nodiscard]] bool ignore_message(char message_type) noexcept;
    [[nodiscard]] bool apply(matching::ErrorCode result) noexcept;
    [[nodiscard]] bool fail(MarketAdapterStatus status) noexcept;
    [[nodiscard]] bool convert_price(std::uint32_t itch_price,
                                     matching::Price& cent_price) noexcept;
    [[nodiscard]] bool add_order(matching::SymbolId symbol_id,
                                 matching::OrderId order_id,
                                 char side,
                                 std::uint32_t shares,
                                 std::uint32_t itch_price) noexcept;
};

template <typename OutputStream>
OutputStream& operator<<(OutputStream& stream, MarketAdapterStatus status);

} // namespace providers::nasdaq::itch

#include "market_adapter.inl"

#endif // PROVIDERS_NASDAQ_ITCH_MARKET_ADAPTER_H
