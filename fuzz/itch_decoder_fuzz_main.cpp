#include "providers/nasdaq/itch/messages.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace itch = providers::nasdaq::itch;

struct KnownFrame
{
    char type;
    std::size_t size;
};

constexpr std::array KNOWN_FRAMES{
    KnownFrame{itch::SystemEventMessage::TYPE,
               itch::SystemEventMessage::WIRE_SIZE},
    KnownFrame{itch::StockDirectoryMessage::TYPE,
               itch::StockDirectoryMessage::WIRE_SIZE},
    KnownFrame{itch::StockTradingActionMessage::TYPE,
               itch::StockTradingActionMessage::WIRE_SIZE},
    KnownFrame{itch::RegShoRestrictionMessage::TYPE,
               itch::RegShoRestrictionMessage::WIRE_SIZE},
    KnownFrame{itch::MarketParticipantPositionMessage::TYPE,
               itch::MarketParticipantPositionMessage::WIRE_SIZE},
    KnownFrame{itch::MwcbDeclineLevelMessage::TYPE,
               itch::MwcbDeclineLevelMessage::WIRE_SIZE},
    KnownFrame{itch::MwcbStatusMessage::TYPE,
               itch::MwcbStatusMessage::WIRE_SIZE},
    KnownFrame{itch::IpoQuotingPeriodUpdateMessage::TYPE,
               itch::IpoQuotingPeriodUpdateMessage::WIRE_SIZE},
    KnownFrame{itch::AddOrderMessage::TYPE, itch::AddOrderMessage::WIRE_SIZE},
    KnownFrame{itch::AddOrderWithAttributionMessage::TYPE,
               itch::AddOrderWithAttributionMessage::WIRE_SIZE},
    KnownFrame{itch::OrderExecutedMessage::TYPE,
               itch::OrderExecutedMessage::WIRE_SIZE},
    KnownFrame{itch::OrderExecutedWithPriceMessage::TYPE,
               itch::OrderExecutedWithPriceMessage::WIRE_SIZE},
    KnownFrame{itch::OrderCancelMessage::TYPE,
               itch::OrderCancelMessage::WIRE_SIZE},
    KnownFrame{itch::OrderDeleteMessage::TYPE,
               itch::OrderDeleteMessage::WIRE_SIZE},
    KnownFrame{itch::OrderReplaceMessage::TYPE,
               itch::OrderReplaceMessage::WIRE_SIZE},
    KnownFrame{itch::TradeMessage::TYPE, itch::TradeMessage::WIRE_SIZE},
    KnownFrame{itch::CrossTradeMessage::TYPE,
               itch::CrossTradeMessage::WIRE_SIZE},
    KnownFrame{itch::BrokenTradeMessage::TYPE,
               itch::BrokenTradeMessage::WIRE_SIZE},
    KnownFrame{itch::NoiiMessage::TYPE, itch::NoiiMessage::WIRE_SIZE},
    KnownFrame{itch::RpiiMessage::TYPE, itch::RpiiMessage::WIRE_SIZE},
    KnownFrame{itch::LuldAuctionCollarMessage::TYPE,
               itch::LuldAuctionCollarMessage::WIRE_SIZE}};

void advance(std::uint64_t& state) noexcept
{
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
}

void fill_random(std::vector<std::uint8_t>& input,
                 std::uint64_t& state) noexcept
{
    for (std::uint8_t& value : input)
    {
        advance(state);
        value = static_cast<std::uint8_t>(state);
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::uint64_t iterations = 100'000;
    if (argc == 2)
    {
        const char* begin = argv[1];
        const char* end = begin;
        while (*end != '\0')
            ++end;
        const auto result = std::from_chars(begin, end, iterations);
        if ((result.ec != std::errc{}) || (result.ptr != end))
        {
            std::cerr << "Invalid iteration count\n";
            return 2;
        }
    }

    std::uint64_t state = 0xD1B54A32D192ED03ULL;
    std::vector<std::uint8_t> input;
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration)
    {
        advance(state);
        const std::uint64_t mode = iteration % 3U;
        if (mode == 0)
        {
            const KnownFrame frame =
                KNOWN_FRAMES[static_cast<std::size_t>(
                    iteration % KNOWN_FRAMES.size())];
            input.resize(frame.size + 2U);
            fill_random(input, state);
            input[0] = static_cast<std::uint8_t>(frame.size >> 8U);
            input[1] = static_cast<std::uint8_t>(frame.size);
            input[2] = static_cast<std::uint8_t>(frame.type);
        }
        else if (mode == 1)
        {
            const std::size_t payload_size =
                1U + static_cast<std::size_t>(state % 50U);
            input.resize(payload_size + 2U);
            fill_random(input, state);
            input[0] = 0;
            input[1] = static_cast<std::uint8_t>(payload_size);
            input[2] = static_cast<std::uint8_t>('Z');
        }
        else
        {
            const std::size_t size =
                static_cast<std::size_t>(state % 4'097U);
            input.resize(size);
            fill_random(input, state);
        }

        LLVMFuzzerTestOneInput(input.data(), input.size());
    }

    std::cout << "Completed " << iterations << " deterministic fuzz inputs\n";
    return 0;
}
