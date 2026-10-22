#ifndef PROVIDERS_NASDAQ_ITCH_FRAME_DECODER_H
#define PROVIDERS_NASDAQ_ITCH_FRAME_DECODER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace providers::nasdaq::itch {

enum class FrameStatus : std::uint8_t
{
    OK = 0,
    INVALID_MESSAGE_LENGTH = 1,
    FRAME_REJECTED = 2,
    TRUNCATED_INPUT = 3,
    FAILED = 4
};

class FrameDecoder
{
public:
    static constexpr std::size_t LENGTH_PREFIX_SIZE = 2;
    static constexpr std::size_t MIN_MESSAGE_SIZE = 1;
    static constexpr std::size_t MAX_MESSAGE_SIZE = 50;

    FrameDecoder() noexcept = default;
    FrameDecoder(const FrameDecoder&) = delete;
    FrameDecoder(FrameDecoder&&) = delete;
    ~FrameDecoder() noexcept = default;

    FrameDecoder& operator=(const FrameDecoder&) = delete;
    FrameDecoder& operator=(FrameDecoder&&) = delete;

    template <typename FrameHandler>
    [[nodiscard]] FrameStatus process(std::span<const std::byte> input,
                                      FrameHandler&& handler) noexcept;

    [[nodiscard]] FrameStatus finish() noexcept;
    void reset() noexcept;

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool has_partial_frame() const noexcept
    {
        return (expected_message_size_ != 0) || (cached_size_ != 0);
    }

private:
    std::array<std::byte, MAX_MESSAGE_SIZE> cache_;
    std::size_t cached_size_{0};
    std::uint16_t expected_message_size_{0};
    bool failed_{false};

    [[nodiscard]] static std::uint16_t decode_length(
        const std::byte* bytes) noexcept;
    [[nodiscard]] FrameStatus accept_length(std::uint16_t length) noexcept;

    template <typename FrameHandler>
    [[nodiscard]] FrameStatus dispatch(std::span<const std::byte> frame,
                                       FrameHandler& handler) noexcept;
};

} // namespace providers::nasdaq::itch

#include "frame_decoder.inl"

#endif // PROVIDERS_NASDAQ_ITCH_FRAME_DECODER_H
