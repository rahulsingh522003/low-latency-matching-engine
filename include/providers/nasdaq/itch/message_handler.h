#ifndef PROVIDERS_NASDAQ_ITCH_MESSAGE_HANDLER_H
#define PROVIDERS_NASDAQ_ITCH_MESSAGE_HANDLER_H

#include "messages.h"

namespace providers::nasdaq::itch {

class Decoder;

class MessageHandler
{
    friend class Decoder;

public:
    MessageHandler() noexcept = default;
    MessageHandler(const MessageHandler&) = delete;
    MessageHandler(MessageHandler&&) = delete;
    virtual ~MessageHandler() noexcept = default;

    MessageHandler& operator=(const MessageHandler&) = delete;
    MessageHandler& operator=(MessageHandler&&) = delete;

protected:
    virtual bool on_message(const SystemEventMessage&) noexcept { return true; }
    virtual bool on_message(const StockDirectoryMessage&) noexcept { return true; }
    virtual bool on_message(const StockTradingActionMessage&) noexcept { return true; }
    virtual bool on_message(const RegShoRestrictionMessage&) noexcept { return true; }
    virtual bool on_message(const MarketParticipantPositionMessage&) noexcept
    {
        return true;
    }
    virtual bool on_message(const MwcbDeclineLevelMessage&) noexcept { return true; }
    virtual bool on_message(const MwcbStatusMessage&) noexcept { return true; }
    virtual bool on_message(const IpoQuotingPeriodUpdateMessage&) noexcept
    {
        return true;
    }
    virtual bool on_message(const AddOrderMessage&) noexcept { return true; }
    virtual bool on_message(const AddOrderWithAttributionMessage&) noexcept
    {
        return true;
    }
    virtual bool on_message(const OrderExecutedMessage&) noexcept { return true; }
    virtual bool on_message(const OrderExecutedWithPriceMessage&) noexcept
    {
        return true;
    }
    virtual bool on_message(const OrderCancelMessage&) noexcept { return true; }
    virtual bool on_message(const OrderDeleteMessage&) noexcept { return true; }
    virtual bool on_message(const OrderReplaceMessage&) noexcept { return true; }
    virtual bool on_message(const TradeMessage&) noexcept { return true; }
    virtual bool on_message(const CrossTradeMessage&) noexcept { return true; }
    virtual bool on_message(const BrokenTradeMessage&) noexcept { return true; }
    virtual bool on_message(const NoiiMessage&) noexcept { return true; }
    virtual bool on_message(const RpiiMessage&) noexcept { return true; }
    virtual bool on_message(const LuldAuctionCollarMessage&) noexcept { return true; }
    virtual bool on_message(const UnknownMessage&) noexcept { return true; }
};

} // namespace providers::nasdaq::itch

#endif // PROVIDERS_NASDAQ_ITCH_MESSAGE_HANDLER_H
