#ifndef PROVIDERS_NASDAQ_ITCH_MESSAGES_H
#define PROVIDERS_NASDAQ_ITCH_MESSAGES_H

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>

namespace providers::nasdaq::itch {

static_assert(CHAR_BIT == 8, "ITCH requires eight-bit bytes");

using StockName = std::array<char, 8>;
using IssueSubType = std::array<char, 2>;
using MarketParticipantId = std::array<char, 4>;
using TradingReason = std::array<char, 4>;

struct SystemEventMessage
{
    static constexpr char TYPE = 'S';
    static constexpr std::size_t WIRE_SIZE = 12;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    char event_code;
};

struct StockDirectoryMessage
{
    static constexpr char TYPE = 'R';
    static constexpr std::size_t WIRE_SIZE = 39;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    StockName stock;
    char market_category;
    char financial_status_indicator;
    std::uint32_t round_lot_size;
    char round_lots_only;
    char issue_classification;
    IssueSubType issue_sub_type;
    char authenticity;
    char short_sale_threshold_indicator;
    char ipo_flag;
    char luld_reference_price_tier;
    char etp_flag;
    std::uint32_t etp_leverage_factor;
    char inverse_indicator;
};

struct StockTradingActionMessage
{
    static constexpr char TYPE = 'H';
    static constexpr std::size_t WIRE_SIZE = 25;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    StockName stock;
    char trading_state;
    char reserved;
    TradingReason reason;
};

struct RegShoRestrictionMessage
{
    static constexpr char TYPE = 'Y';
    static constexpr std::size_t WIRE_SIZE = 20;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    StockName stock;
    char action;
};

struct MarketParticipantPositionMessage
{
    static constexpr char TYPE = 'L';
    static constexpr std::size_t WIRE_SIZE = 26;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    MarketParticipantId participant_id;
    StockName stock;
    char primary_market_maker;
    char market_maker_mode;
    char market_participant_state;
};

struct MwcbDeclineLevelMessage
{
    static constexpr char TYPE = 'V';
    static constexpr std::size_t WIRE_SIZE = 35;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t level_1;
    std::uint64_t level_2;
    std::uint64_t level_3;
};

struct MwcbStatusMessage
{
    static constexpr char TYPE = 'W';
    static constexpr std::size_t WIRE_SIZE = 12;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    char breached_level;
};

struct IpoQuotingPeriodUpdateMessage
{
    static constexpr char TYPE = 'K';
    static constexpr std::size_t WIRE_SIZE = 28;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    StockName stock;
    std::uint32_t release_time;
    char release_qualifier;
    std::uint32_t price;
};

struct AddOrderMessage
{
    static constexpr char TYPE = 'A';
    static constexpr std::size_t WIRE_SIZE = 36;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t order_reference_number;
    char buy_sell_indicator;
    std::uint32_t shares;
    StockName stock;
    std::uint32_t price;
};

struct AddOrderWithAttributionMessage
{
    static constexpr char TYPE = 'F';
    static constexpr std::size_t WIRE_SIZE = 40;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t order_reference_number;
    char buy_sell_indicator;
    std::uint32_t shares;
    StockName stock;
    std::uint32_t price;
    MarketParticipantId attribution;
};

struct OrderExecutedMessage
{
    static constexpr char TYPE = 'E';
    static constexpr std::size_t WIRE_SIZE = 31;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t order_reference_number;
    std::uint32_t executed_shares;
    std::uint64_t match_number;
};

struct OrderExecutedWithPriceMessage
{
    static constexpr char TYPE = 'C';
    static constexpr std::size_t WIRE_SIZE = 36;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t order_reference_number;
    std::uint32_t executed_shares;
    std::uint64_t match_number;
    char printable;
    std::uint32_t execution_price;
};

struct OrderCancelMessage
{
    static constexpr char TYPE = 'X';
    static constexpr std::size_t WIRE_SIZE = 23;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t order_reference_number;
    std::uint32_t cancelled_shares;
};

struct OrderDeleteMessage
{
    static constexpr char TYPE = 'D';
    static constexpr std::size_t WIRE_SIZE = 19;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t order_reference_number;
};

struct OrderReplaceMessage
{
    static constexpr char TYPE = 'U';
    static constexpr std::size_t WIRE_SIZE = 35;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t original_order_reference_number;
    std::uint64_t new_order_reference_number;
    std::uint32_t shares;
    std::uint32_t price;
};

struct TradeMessage
{
    static constexpr char TYPE = 'P';
    static constexpr std::size_t WIRE_SIZE = 44;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t order_reference_number;
    char buy_sell_indicator;
    std::uint32_t shares;
    StockName stock;
    std::uint32_t price;
    std::uint64_t match_number;
};

struct CrossTradeMessage
{
    static constexpr char TYPE = 'Q';
    static constexpr std::size_t WIRE_SIZE = 40;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t shares;
    StockName stock;
    std::uint32_t cross_price;
    std::uint64_t match_number;
    char cross_type;
};

struct BrokenTradeMessage
{
    static constexpr char TYPE = 'B';
    static constexpr std::size_t WIRE_SIZE = 19;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t match_number;
};

struct NoiiMessage
{
    static constexpr char TYPE = 'I';
    static constexpr std::size_t WIRE_SIZE = 50;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    std::uint64_t paired_shares;
    std::uint64_t imbalance_shares;
    char imbalance_direction;
    StockName stock;
    std::uint32_t far_price;
    std::uint32_t near_price;
    std::uint32_t current_reference_price;
    char cross_type;
    char price_variation_indicator;
};

struct RpiiMessage
{
    static constexpr char TYPE = 'N';
    static constexpr std::size_t WIRE_SIZE = 20;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    StockName stock;
    char interest_flag;
};

struct LuldAuctionCollarMessage
{
    static constexpr char TYPE = 'J';
    static constexpr std::size_t WIRE_SIZE = 35;

    char type;
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint64_t timestamp;
    StockName stock;
    std::uint32_t auction_collar_reference_price;
    std::uint32_t upper_auction_collar_price;
    std::uint32_t lower_auction_collar_price;
    std::uint32_t auction_collar_extension;
};

struct UnknownMessage
{
    char type;
};

} // namespace providers::nasdaq::itch

#endif // PROVIDERS_NASDAQ_ITCH_MESSAGES_H
