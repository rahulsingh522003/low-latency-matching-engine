#include "providers/nasdaq/itch/frame_decoder.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using providers::nasdaq::itch::FrameDecoder;
using providers::nasdaq::itch::FrameStatus;

[[nodiscard]] constexpr std::byte as_byte(std::uint8_t value) noexcept
{
    return static_cast<std::byte>(value);
}

[[nodiscard]] std::vector<std::byte> make_frame(
    std::span<const std::byte> message)
{
    std::vector<std::byte> frame(message.size() + FrameDecoder::LENGTH_PREFIX_SIZE);
    frame[0] = as_byte(static_cast<std::uint8_t>(message.size() >> 8U));
    frame[1] = as_byte(static_cast<std::uint8_t>(message.size()));
    std::copy(message.begin(), message.end(), frame.begin() + 2);
    return frame;
}

class FrameCollector
{
public:
    bool operator()(std::span<const std::byte> frame) noexcept
    {
        if (!accept_ || (count_ == frames_.size()) ||
            (frame.size() > FrameDecoder::MAX_MESSAGE_SIZE))
        {
            return false;
        }

        sizes_[count_] = frame.size();
        std::copy(frame.begin(), frame.end(), frames_[count_].begin());
        ++count_;
        return true;
    }

    [[nodiscard]] std::size_t count() const noexcept { return count_; }

    [[nodiscard]] bool equals(std::size_t index,
                              std::span<const std::byte> expected) const noexcept
    {
        if ((index >= count_) || (sizes_[index] != expected.size()))
            return false;

        return std::equal(expected.begin(),
                          expected.end(),
                          frames_[index].begin());
    }

    void reject() noexcept { accept_ = false; }

private:
    std::array<std::array<std::byte, FrameDecoder::MAX_MESSAGE_SIZE>, 16> frames_;
    std::array<std::size_t, 16> sizes_{};
    std::size_t count_{0};
    bool accept_{true};
};

} // namespace

TEST_CASE("Complete ITCH frames use the caller's contiguous storage",
          "[itch_frame]")
{
    const std::array message{as_byte('A'), as_byte(1), as_byte(2), as_byte(3)};
    const std::vector<std::byte> frame = make_frame(message);

    FrameDecoder decoder;
    const std::byte* observed_address = nullptr;
    std::size_t observed_size = 0;
    const auto handler = [&](std::span<const std::byte> decoded) noexcept {
        observed_address = decoded.data();
        observed_size = decoded.size();
        return true;
    };

    REQUIRE(decoder.process(frame, handler) == FrameStatus::OK);
    REQUIRE(observed_address == frame.data() + FrameDecoder::LENGTH_PREFIX_SIZE);
    REQUIRE(observed_size == message.size());
    REQUIRE_FALSE(decoder.has_partial_frame());
    REQUIRE(decoder.finish() == FrameStatus::OK);
}

TEST_CASE("An ITCH frame can be split at every byte boundary", "[itch_frame]")
{
    std::array<std::byte, FrameDecoder::MAX_MESSAGE_SIZE> message{};
    for (std::size_t index = 0; index < message.size(); ++index)
        message[index] = as_byte(static_cast<std::uint8_t>(index + 1));

    const std::vector<std::byte> frame = make_frame(message);
    for (std::size_t split = 0; split <= frame.size(); ++split)
    {
        FrameDecoder decoder;
        FrameCollector collector;

        REQUIRE(decoder.process(
                    std::span<const std::byte>(frame.data(), split), collector) ==
                FrameStatus::OK);
        REQUIRE(decoder.process(
                    std::span<const std::byte>(frame.data() + split,
                                               frame.size() - split),
                    collector) == FrameStatus::OK);
        REQUIRE(collector.count() == 1);
        REQUIRE(collector.equals(0, message));
        REQUIRE(decoder.finish() == FrameStatus::OK);
    }
}

TEST_CASE("ITCH framing accepts one byte at a time", "[itch_frame]")
{
    const std::array message{as_byte('S'), as_byte(7), as_byte(8), as_byte(9)};
    const std::vector<std::byte> frame = make_frame(message);

    FrameDecoder decoder;
    FrameCollector collector;
    for (const std::byte& byte : frame)
    {
        REQUIRE(decoder.process(std::span<const std::byte>(&byte, 1), collector) ==
                FrameStatus::OK);
    }

    REQUIRE(collector.count() == 1);
    REQUIRE(collector.equals(0, message));
    REQUIRE_FALSE(decoder.has_partial_frame());
}

TEST_CASE("One input buffer can contain several ITCH frames", "[itch_frame]")
{
    const std::array first{as_byte('A'), as_byte(10), as_byte(11)};
    const std::array second{as_byte('D')};
    const std::array third{as_byte('I'), as_byte(20), as_byte(21), as_byte(22)};

    std::vector<std::byte> input = make_frame(first);
    const std::vector<std::byte> second_frame = make_frame(second);
    const std::vector<std::byte> third_frame = make_frame(third);
    input.insert(input.end(), second_frame.begin(), second_frame.end());
    input.insert(input.end(), third_frame.begin(), third_frame.end());

    FrameDecoder decoder;
    FrameCollector collector;
    REQUIRE(decoder.process(input, collector) == FrameStatus::OK);
    REQUIRE(collector.count() == 3);
    REQUIRE(collector.equals(0, first));
    REQUIRE(collector.equals(1, second));
    REQUIRE(collector.equals(2, third));
}

TEST_CASE("Invalid ITCH lengths fail until the decoder is reset", "[itch_frame]")
{
    FrameDecoder decoder;
    FrameCollector collector;

    const std::array zero_length{as_byte(0), as_byte(0)};
    REQUIRE(decoder.process(zero_length, collector) ==
            FrameStatus::INVALID_MESSAGE_LENGTH);
    REQUIRE(decoder.failed());

    const std::array valid_message{as_byte('S')};
    const std::vector<std::byte> valid_frame = make_frame(valid_message);
    REQUIRE(decoder.process(valid_frame, collector) == FrameStatus::FAILED);
    REQUIRE(collector.count() == 0);

    decoder.reset();
    REQUIRE_FALSE(decoder.failed());
    REQUIRE(decoder.process(valid_frame, collector) == FrameStatus::OK);
    REQUIRE(collector.count() == 1);

    decoder.reset();
    const std::array oversized_length{
        as_byte(0),
        as_byte(static_cast<std::uint8_t>(FrameDecoder::MAX_MESSAGE_SIZE + 1))};
    REQUIRE(decoder.process(oversized_length, collector) ==
            FrameStatus::INVALID_MESSAGE_LENGTH);
    REQUIRE(decoder.failed());
}

TEST_CASE("Finishing detects a truncated prefix or message", "[itch_frame]")
{
    FrameCollector collector;

    SECTION("One length byte is missing")
    {
        FrameDecoder decoder;
        const std::array input{as_byte(0)};
        REQUIRE(decoder.process(input, collector) == FrameStatus::OK);
        REQUIRE(decoder.has_partial_frame());
        REQUIRE(decoder.finish() == FrameStatus::TRUNCATED_INPUT);
        REQUIRE(decoder.failed());
    }

    SECTION("The message body is incomplete")
    {
        FrameDecoder decoder;
        const std::array input{as_byte(0), as_byte(3), as_byte('A'), as_byte(1)};
        REQUIRE(decoder.process(input, collector) == FrameStatus::OK);
        REQUIRE(decoder.has_partial_frame());
        REQUIRE(decoder.finish() == FrameStatus::TRUNCATED_INPUT);
        REQUIRE(decoder.failed());
    }
}

TEST_CASE("A rejected frame stops further decoding", "[itch_frame]")
{
    const std::array message{as_byte('A')};
    const std::vector<std::byte> frame = make_frame(message);

    FrameDecoder decoder;
    FrameCollector collector;
    collector.reject();

    REQUIRE(decoder.process(frame, collector) == FrameStatus::FRAME_REJECTED);
    REQUIRE(decoder.failed());
    REQUIRE(collector.count() == 0);
    REQUIRE(decoder.finish() == FrameStatus::FAILED);
}
