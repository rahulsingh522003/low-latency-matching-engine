#include <algorithm>
#include <cstring>

namespace providers::nasdaq::itch {

template <typename FrameHandler>
inline FrameStatus FrameDecoder::process(std::span<const std::byte> input,
                                         FrameHandler&& handler) noexcept
{
    static_assert(
        std::is_nothrow_invocable_r_v<bool,
                                      FrameHandler&,
                                      std::span<const std::byte>>,
        "ITCH frame handlers must be noexcept and return bool");

    if (failed_)
        return FrameStatus::FAILED;

    std::size_t index = 0;
    while (index < input.size())
    {
        if (expected_message_size_ == 0)
        {
            if ((cached_size_ == 0) &&
                ((input.size() - index) >= LENGTH_PREFIX_SIZE))
            {
                const FrameStatus status = accept_length(
                    decode_length(input.data() + index));
                index += LENGTH_PREFIX_SIZE;
                if (status != FrameStatus::OK)
                    return status;
            }
            else
            {
                const std::size_t prefix_bytes = std::min(
                    LENGTH_PREFIX_SIZE - cached_size_, input.size() - index);
                std::memcpy(cache_.data() + cached_size_,
                            input.data() + index,
                            prefix_bytes);
                cached_size_ += prefix_bytes;
                index += prefix_bytes;

                if (cached_size_ < LENGTH_PREFIX_SIZE)
                    continue;

                const std::uint16_t length = decode_length(cache_.data());
                cached_size_ = 0;
                const FrameStatus status = accept_length(length);
                if (status != FrameStatus::OK)
                    return status;
            }
        }

        const std::size_t remaining = input.size() - index;
        if ((cached_size_ == 0) &&
            (remaining >= expected_message_size_))
        {
            const std::span<const std::byte> frame(
                input.data() + index, expected_message_size_);
            index += expected_message_size_;
            expected_message_size_ = 0;

            const FrameStatus status = dispatch(frame, handler);
            if (status != FrameStatus::OK)
                return status;
            continue;
        }

        const std::size_t required = expected_message_size_ - cached_size_;
        const std::size_t copied = std::min(required, remaining);
        std::memcpy(cache_.data() + cached_size_, input.data() + index, copied);
        cached_size_ += copied;
        index += copied;

        if (cached_size_ < expected_message_size_)
            continue;

        const std::span<const std::byte> frame(cache_.data(), cached_size_);
        cached_size_ = 0;
        expected_message_size_ = 0;

        const FrameStatus status = dispatch(frame, handler);
        if (status != FrameStatus::OK)
            return status;
    }

    return FrameStatus::OK;
}

inline FrameStatus FrameDecoder::finish() noexcept
{
    if (failed_)
        return FrameStatus::FAILED;

    if (has_partial_frame())
    {
        failed_ = true;
        return FrameStatus::TRUNCATED_INPUT;
    }

    return FrameStatus::OK;
}

inline void FrameDecoder::reset() noexcept
{
    cached_size_ = 0;
    expected_message_size_ = 0;
    failed_ = false;
}

inline std::uint16_t FrameDecoder::decode_length(const std::byte* bytes) noexcept
{
    const auto high = std::to_integer<std::uint16_t>(bytes[0]);
    const auto low = std::to_integer<std::uint16_t>(bytes[1]);
    return static_cast<std::uint16_t>((high << 8U) | low);
}

inline FrameStatus FrameDecoder::accept_length(std::uint16_t length) noexcept
{
    if ((length < MIN_MESSAGE_SIZE) || (length > MAX_MESSAGE_SIZE))
    {
        cached_size_ = 0;
        expected_message_size_ = 0;
        failed_ = true;
        return FrameStatus::INVALID_MESSAGE_LENGTH;
    }

    expected_message_size_ = length;
    return FrameStatus::OK;
}

template <typename FrameHandler>
inline FrameStatus FrameDecoder::dispatch(std::span<const std::byte> frame,
                                          FrameHandler& handler) noexcept
{
    if (!handler(frame))
    {
        failed_ = true;
        return FrameStatus::FRAME_REJECTED;
    }

    return FrameStatus::OK;
}

} // namespace providers::nasdaq::itch
