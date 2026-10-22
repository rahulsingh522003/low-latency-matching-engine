#ifndef PROVIDERS_NASDAQ_ITCH_DECODER_H
#define PROVIDERS_NASDAQ_ITCH_DECODER_H

#include "frame_decoder.h"
#include "message_handler.h"

#include <cstdint>
#include <span>

namespace providers::nasdaq::itch {

enum class DecodeStatus : std::uint8_t
{
    OK = 0,
    INVALID_MESSAGE_LENGTH = 1,
    INVALID_MESSAGE_SIZE = 2,
    MESSAGE_REJECTED = 3,
    TRUNCATED_INPUT = 4,
    FAILED = 5
};

class Decoder
{
public:
    explicit Decoder(MessageHandler& handler) noexcept;
    Decoder(const Decoder&) = delete;
    Decoder(Decoder&&) = delete;
    ~Decoder() noexcept = default;

    Decoder& operator=(const Decoder&) = delete;
    Decoder& operator=(Decoder&&) = delete;

    [[nodiscard]] DecodeStatus process(std::span<const std::byte> input) noexcept;
    [[nodiscard]] DecodeStatus finish() noexcept;
    void reset() noexcept;

    [[nodiscard]] bool failed() const noexcept { return frame_decoder_.failed(); }
    [[nodiscard]] bool has_partial_frame() const noexcept
    {
        return frame_decoder_.has_partial_frame();
    }

private:
    MessageHandler& handler_;
    FrameDecoder frame_decoder_;
    DecodeStatus message_status_{DecodeStatus::OK};

    [[nodiscard]] static DecodeStatus translate_frame_status(
        FrameStatus status,
        DecodeStatus message_status) noexcept;
    [[nodiscard]] DecodeStatus decode_message(
        std::span<const std::byte> frame) noexcept;

    [[nodiscard]] DecodeStatus decode_system_event(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_stock_directory(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_stock_trading_action(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_reg_sho_restriction(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_market_participant_position(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_mwcb_decline_level(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_mwcb_status(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_ipo_quoting_period_update(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_add_order(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_add_order_with_attribution(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_order_executed(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_order_executed_with_price(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_order_cancel(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_order_delete(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_order_replace(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_trade(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_cross_trade(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_broken_trade(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_noii(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_rpii(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_luld_auction_collar(
        std::span<const std::byte> frame) noexcept;
    [[nodiscard]] DecodeStatus decode_unknown(
        std::span<const std::byte> frame) noexcept;
};

} // namespace providers::nasdaq::itch

#endif // PROVIDERS_NASDAQ_ITCH_DECODER_H
