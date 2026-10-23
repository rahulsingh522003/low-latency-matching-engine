#include "benchmark_support.h"

#include "matching/level.h"
#include "matching/events/event_handler.h"
#include "matching/manager/market_manager.h"
#include "matching/order.h"
#include "matching/book/order_book.h"
#include "matching/symbol.h"
#include "providers/nasdaq/itch/decoder.h"
#include "providers/nasdaq/itch/market_adapter.h"
#include "providers/nasdaq/itch/message_handler.h"
#include "providers/nasdaq/itch/messages.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

namespace support = matching_benchmark;
namespace itch = providers::nasdaq::itch;

constexpr std::uint64_t FNV_OFFSET_BASIS = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t FNV_PRIME = 1'099'511'628'211ULL;
constexpr std::uint32_t SYMBOL_ID_CAPACITY = 1U << 16U;

enum class InputMode
{
    MEMORY,
    STREAM
};

enum class Workload
{
    ALL,
    PARSER,
    RECONSTRUCTION,
    RECONSTRUCTION_COUNTING
};

struct Options
{
    std::filesystem::path input_path;
    std::filesystem::path output_directory;
    InputMode input_mode{InputMode::MEMORY};
    Workload workload{Workload::ALL};
    std::size_t chunk_size{64U * 1024U};
    std::size_t warmup{1};
    std::size_t repetitions{7};
};

struct FeedTiming
{
    std::uint64_t elapsed_ns{0};
    std::uint64_t bytes{0};
    itch::DecodeStatus status{itch::DecodeStatus::OK};
};

struct Result
{
    std::string_view benchmark;
    std::uint64_t elapsed_ns{0};
    std::uint64_t bytes{0};
    std::uint64_t messages{0};
    std::uint64_t unknown_messages{0};
    std::size_t symbols{0};
    std::size_t books{0};
    std::size_t active_orders{0};
    std::uint64_t state_hash{0};
    std::uint64_t callbacks{0};
    std::uint64_t peak_rss_bytes{0};
};

void mix_byte(std::uint64_t& hash, std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= FNV_PRIME;
}

template <typename Integer>
void mix_integer(std::uint64_t& hash, Integer value) noexcept
{
    if constexpr (std::is_same_v<Integer, bool>)
        mix_byte(hash, value ? 1U : 0U);
    else if constexpr (std::is_enum_v<Integer>)
        mix_integer(hash, static_cast<std::underlying_type_t<Integer>>(value));
    else
    {
        using Unsigned = std::make_unsigned_t<Integer>;
        std::uint64_t current = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
        {
            mix_byte(hash, static_cast<std::uint8_t>(current));
            current >>= 8U;
        }
    }
}

template <std::size_t Width>
void mix_text(std::uint64_t& hash, const std::array<char, Width>& text) noexcept
{
    for (const char character : text)
        mix_byte(hash, static_cast<std::uint8_t>(character));
}

void mix_order(std::uint64_t& hash, const matching::Order& order) noexcept
{
    mix_integer(hash, order.id);
    mix_integer(hash, order.symbol_id);
    mix_integer(hash, order.type);
    mix_integer(hash, order.side);
    mix_integer(hash, order.time_in_force);
    mix_integer(hash, order.price);
    mix_integer(hash, order.stop_price);
    mix_integer(hash, order.quantity);
    mix_integer(hash, order.max_display_quantity);
    mix_integer(hash, order.slippage);
    mix_integer(hash, order.trailing_distance);
    mix_integer(hash, order.trailing_step);
}

class CountingMessageHandler final : public itch::MessageHandler
{
public:
    [[nodiscard]] std::uint64_t messages() const noexcept { return messages_; }
    [[nodiscard]] std::uint64_t unknown_messages() const noexcept
    {
        return unknown_messages_;
    }

    [[nodiscard]] std::uint64_t validation_hash() const noexcept
    {
        std::uint64_t hash = FNV_OFFSET_BASIS;
        mix_integer(hash, messages_);
        mix_integer(hash, unknown_messages_);
        for (std::size_t index = 0; index < type_counts_.size(); ++index)
        {
            if (type_counts_[index] == 0)
                continue;
            mix_integer(hash, index);
            mix_integer(hash, type_counts_[index]);
        }
        return hash;
    }

protected:
#define MATCHING_COUNT_MESSAGE(MessageType)                                      \
    bool on_message(const itch::MessageType& message) noexcept override          \
    {                                                                             \
        return observe(message.type, false);                                      \
    }

    MATCHING_COUNT_MESSAGE(SystemEventMessage)
    MATCHING_COUNT_MESSAGE(StockDirectoryMessage)
    MATCHING_COUNT_MESSAGE(StockTradingActionMessage)
    MATCHING_COUNT_MESSAGE(RegShoRestrictionMessage)
    MATCHING_COUNT_MESSAGE(MarketParticipantPositionMessage)
    MATCHING_COUNT_MESSAGE(MwcbDeclineLevelMessage)
    MATCHING_COUNT_MESSAGE(MwcbStatusMessage)
    MATCHING_COUNT_MESSAGE(IpoQuotingPeriodUpdateMessage)
    MATCHING_COUNT_MESSAGE(AddOrderMessage)
    MATCHING_COUNT_MESSAGE(AddOrderWithAttributionMessage)
    MATCHING_COUNT_MESSAGE(OrderExecutedMessage)
    MATCHING_COUNT_MESSAGE(OrderExecutedWithPriceMessage)
    MATCHING_COUNT_MESSAGE(OrderCancelMessage)
    MATCHING_COUNT_MESSAGE(OrderDeleteMessage)
    MATCHING_COUNT_MESSAGE(OrderReplaceMessage)
    MATCHING_COUNT_MESSAGE(TradeMessage)
    MATCHING_COUNT_MESSAGE(CrossTradeMessage)
    MATCHING_COUNT_MESSAGE(BrokenTradeMessage)
    MATCHING_COUNT_MESSAGE(NoiiMessage)
    MATCHING_COUNT_MESSAGE(RpiiMessage)
    MATCHING_COUNT_MESSAGE(LuldAuctionCollarMessage)

#undef MATCHING_COUNT_MESSAGE

    bool on_message(const itch::UnknownMessage& message) noexcept override
    {
        return observe(message.type, true);
    }

private:
    std::uint64_t messages_{0};
    std::uint64_t unknown_messages_{0};
    std::array<std::uint64_t, 256> type_counts_{};

    bool observe(char type, bool unknown) noexcept
    {
        ++messages_;
        ++type_counts_[static_cast<unsigned char>(type)];
        if (unknown)
            ++unknown_messages_;
        return true;
    }
};

class CountingEventHandler final : public matching::EventHandler
{
public:
    [[nodiscard]] std::uint64_t callbacks() const noexcept { return callbacks_; }

protected:
    void on_add_symbol(const matching::Symbol&) noexcept override { observe(); }
    void on_delete_symbol(const matching::Symbol&) noexcept override { observe(); }
    void on_add_order_book(const matching::OrderBook&) noexcept override { observe(); }
    void on_update_order_book(const matching::OrderBook&, bool) noexcept override
    {
        observe();
    }
    void on_delete_order_book(const matching::OrderBook&) noexcept override
    {
        observe();
    }
    void on_add_level(const matching::OrderBook&,
                      const matching::Level&,
                      bool) noexcept override
    {
        observe();
    }
    void on_update_level(const matching::OrderBook&,
                         const matching::Level&,
                         bool) noexcept override
    {
        observe();
    }
    void on_delete_level(const matching::OrderBook&,
                         const matching::Level&,
                         bool) noexcept override
    {
        observe();
    }
    void on_add_order(const matching::Order&) noexcept override { observe(); }
    void on_update_order(const matching::Order&) noexcept override { observe(); }
    void on_delete_order(const matching::Order&) noexcept override { observe(); }
    void on_execute_order(const matching::Order&,
                          matching::Price,
                          matching::Quantity) noexcept override
    {
        observe();
    }

private:
    std::uint64_t callbacks_{0};

    void observe() noexcept { ++callbacks_; }
};

class NoOpEventHandler final : public matching::EventHandler
{
public:
    [[nodiscard]] std::uint64_t callbacks() const noexcept { return 0; }
};

template <typename Tree>
void mix_tree(std::uint64_t& hash, const Tree& tree) noexcept
{
    mix_integer(hash, tree.size());
    for (const matching::LevelNode& level : tree)
    {
        mix_integer(hash, level.type);
        mix_integer(hash, level.price);
        mix_integer(hash, level.total_quantity);
        mix_integer(hash, level.displayed_quantity);
        mix_integer(hash, level.order_count);
        for (const matching::OrderNode* order = level.orders.front();
             order != nullptr;
             order = order->next)
        {
            mix_order(hash, *order);
        }
    }
}

[[nodiscard]] std::uint64_t market_state_hash(
    const matching::MarketManager& market) noexcept
{
    std::uint64_t hash = FNV_OFFSET_BASIS;
    mix_integer(hash, market.symbol_count());
    mix_integer(hash, market.order_book_count());
    mix_integer(hash, market.active_order_count());

    for (std::uint32_t symbol_id = 0; symbol_id < SYMBOL_ID_CAPACITY; ++symbol_id)
    {
        const matching::Symbol* symbol = market.find_symbol(symbol_id);
        if (symbol == nullptr)
            continue;
        mix_integer(hash, symbol->id);
        mix_text(hash, symbol->name);

        const matching::OrderBook* book = market.find_order_book(symbol_id);
        if (book == nullptr)
            continue;
        mix_tree(hash, book->bids());
        mix_tree(hash, book->asks());
        mix_tree(hash, book->buy_stops());
        mix_tree(hash, book->sell_stops());
        mix_tree(hash, book->trailing_buy_stops());
        mix_tree(hash, book->trailing_sell_stops());
    }
    return hash;
}

[[nodiscard]] bool parse_options(int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view option = argv[index];
        if ((option == "--input") || (option == "--output") ||
            (option == "--mode") || (option == "--workload") ||
            (option == "--chunk-size") || (option == "--warmup") ||
            (option == "--repetitions"))
        {
            if ((index + 1) >= argc)
                return false;
            const std::string_view value = argv[++index];
            if (option == "--input")
                options.input_path = value;
            else if (option == "--output")
                options.output_directory = value;
            else if (option == "--mode")
            {
                if (value == "memory")
                    options.input_mode = InputMode::MEMORY;
                else if (value == "stream")
                    options.input_mode = InputMode::STREAM;
                else
                    return false;
            }
            else if (option == "--workload")
            {
                if (value == "all")
                    options.workload = Workload::ALL;
                else if (value == "parser")
                    options.workload = Workload::PARSER;
                else if (value == "reconstruction")
                    options.workload = Workload::RECONSTRUCTION;
                else if (value == "reconstruction-counting")
                    options.workload = Workload::RECONSTRUCTION_COUNTING;
                else
                    return false;
            }
            else
            {
                std::size_t parsed = 0;
                const bool parsed_size =
                    (option == "--warmup")
                        ? support::parse_nonnegative_size(value, parsed)
                        : support::parse_positive_size(value, parsed);
                if (!parsed_size)
                    return false;
                if (option == "--chunk-size")
                    options.chunk_size = parsed;
                else if (option == "--warmup")
                    options.warmup = parsed;
                else
                    options.repetitions = parsed;
            }
        }
        else
            return false;
    }

    return !options.input_path.empty() && !options.output_directory.empty();
}

[[nodiscard]] std::vector<std::byte> read_input(
    const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("Unable to open input file");

    const std::streampos end = input.tellg();
    if (end < 0)
        throw std::runtime_error("Unable to determine input size");
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("Input file is too large for memory mode");

    std::vector<std::byte> data(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    if (!input || (static_cast<std::size_t>(input.gcount()) != data.size()))
        throw std::runtime_error("Unable to read complete input file");
    return data;
}

template <typename Decoder>
FeedTiming process_memory(Decoder& decoder,
                          std::span<const std::byte> data,
                          std::size_t chunk_size) noexcept
{
    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    std::size_t offset = 0;
    itch::DecodeStatus status = itch::DecodeStatus::OK;
    while (offset < data.size())
    {
        const std::size_t count = std::min(chunk_size, data.size() - offset);
        status = decoder.process(data.subspan(offset, count));
        if (status != itch::DecodeStatus::OK)
            break;
        offset += count;
    }
    if (status == itch::DecodeStatus::OK)
        status = decoder.finish();
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop),
            static_cast<std::uint64_t>(offset),
            status};
}

template <typename Decoder>
FeedTiming process_stream(Decoder& decoder,
                          const std::filesystem::path& path,
                          std::size_t chunk_size)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Unable to open input stream");
    std::vector<std::byte> buffer(chunk_size);
    std::uint64_t bytes = 0;
    itch::DecodeStatus status = itch::DecodeStatus::OK;

    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    while (input)
    {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count <= 0)
            break;
        const auto size = static_cast<std::size_t>(count);
        status = decoder.process(std::span(buffer.data(), size));
        bytes += static_cast<std::uint64_t>(size);
        if (status != itch::DecodeStatus::OK)
            break;
    }
    if ((status == itch::DecodeStatus::OK) && !input.bad())
        status = decoder.finish();
    else if (input.bad())
        status = itch::DecodeStatus::FAILED;
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), bytes, status};
}

template <typename Decoder>
FeedTiming process(Decoder& decoder,
                   const Options& options,
                   std::span<const std::byte> memory_input)
{
    if (options.input_mode == InputMode::MEMORY)
        return process_memory(decoder, memory_input, options.chunk_size);
    return process_stream(decoder, options.input_path, options.chunk_size);
}

Result run_parser(const Options& options,
                  std::span<const std::byte> memory_input)
{
    CountingMessageHandler handler;
    itch::Decoder decoder(handler);
    const FeedTiming timing = process(decoder, options, memory_input);
    if (timing.status != itch::DecodeStatus::OK)
        throw std::runtime_error("Parser workload rejected the input");
    return {"itch_parser",
            timing.elapsed_ns,
            timing.bytes,
            handler.messages(),
            handler.unknown_messages(),
            0,
            0,
            0,
            handler.validation_hash(),
            handler.messages(),
            support::peak_resident_set_bytes()};
}

template <typename Handler>
Result run_reconstruction(const Options& options,
                          std::span<const std::byte> memory_input,
                          std::string_view benchmark_name)
{
    Handler handler;
    matching::MarketManager market(handler);
    itch::MarketAdapter adapter(
        market, itch::PriceConversionPolicy::ROUND_TO_NEAREST_CENT);
    itch::Decoder decoder(adapter);
    const FeedTiming timing = process(decoder, options, memory_input);
    if ((timing.status != itch::DecodeStatus::OK) ||
        (adapter.status() != itch::MarketAdapterStatus::OK))
    {
        throw std::runtime_error("Reconstruction workload rejected the input");
    }

    const itch::MarketAdapterStatistics& statistics = adapter.statistics();
    if ((statistics.messages == 0) || (market.symbol_count() == 0) ||
        (market.symbol_count() != market.order_book_count()))
    {
        throw std::runtime_error("Reconstruction final-state validation failed");
    }

    return {benchmark_name,
            timing.elapsed_ns,
            timing.bytes,
            statistics.messages,
            statistics.unknown_messages,
            market.symbol_count(),
            market.order_book_count(),
            market.active_order_count(),
            market_state_hash(market),
            handler.callbacks(),
            support::peak_resident_set_bytes()};
}

[[nodiscard]] bool same_validation(const Result& left,
                                   const Result& right) noexcept
{
    return (left.bytes == right.bytes) && (left.messages == right.messages) &&
           (left.unknown_messages == right.unknown_messages) &&
           (left.symbols == right.symbols) && (left.books == right.books) &&
           (left.active_orders == right.active_orders) &&
           (left.state_hash == right.state_hash) &&
           (left.callbacks == right.callbacks);
}

void write_result(std::ofstream& output,
                  const Result& result,
                  InputMode mode,
                  std::size_t repetition)
{
    const long double ns_per_message =
        static_cast<long double>(result.elapsed_ns) /
        static_cast<long double>(result.messages);
    const long double messages_per_second =
        (static_cast<long double>(result.messages) * 1'000'000'000.0L) /
        static_cast<long double>(result.elapsed_ns);
    const long double gib_per_second =
        (static_cast<long double>(result.bytes) * 1'000'000'000.0L) /
        (static_cast<long double>(result.elapsed_ns) *
         static_cast<long double>(1ULL << 30U));

    output << result.benchmark << ',' << repetition << ','
           << ((mode == InputMode::MEMORY) ? "memory" : "stream") << ','
           << result.bytes << ',' << result.messages << ',' << result.elapsed_ns
           << ',' << std::fixed << std::setprecision(3) << ns_per_message << ','
           << messages_per_second << ',' << gib_per_second << ','
           << result.unknown_messages << ',' << result.symbols << ','
           << result.books << ',' << result.active_orders << ",0x" << std::hex
           << result.state_hash << std::dec << ',' << result.callbacks << ','
           << result.peak_rss_bytes << '\n';
}

template <typename Function>
void run_workload(const Options& options,
                  std::span<const std::byte> memory_input,
                  std::ofstream& output,
                  Function&& function)
{
    Result expected{};
    bool has_expected = false;

    for (std::size_t warmup = 0; warmup < options.warmup; ++warmup)
    {
        const Result result = function(options, memory_input);
        if (!has_expected)
        {
            expected = result;
            has_expected = true;
        }
        else if (!same_validation(expected, result))
            throw std::runtime_error("Warm-up validation changed between runs");
    }

    for (std::size_t repetition = 1; repetition <= options.repetitions;
         ++repetition)
    {
        const Result result = function(options, memory_input);
        if (has_expected && !same_validation(expected, result))
            throw std::runtime_error("Measured validation changed between runs");
        expected = result;
        has_expected = true;
        write_result(output, result, options.input_mode, repetition);
        output.flush();
        std::cout << "Completed " << result.benchmark << " repetition "
                  << repetition << '/' << options.repetitions << '\n';
    }
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options))
    {
        std::cerr << "Usage: itch_pipeline_benchmark --input <file> "
                     "--output <directory> [--mode memory|stream] "
                     "[--workload all|parser|reconstruction|"
                     "reconstruction-counting] [--chunk-size N] "
                     "[--warmup N] [--repetitions N]\n";
        return 2;
    }

    try
    {
        support::prepare_output_directory(options.output_directory);
        std::ofstream output = support::open_output(
            options.output_directory / "itch_pipeline_summary.csv");
        output << "benchmark,repetition,input_mode,bytes,messages,total_ns,"
                  "ns_per_message,messages_per_second,gib_per_second,unknown_"
                  "messages,symbols,books,active_orders,state_hash,callbacks,"
                  "peak_rss_bytes\n";

        std::vector<std::byte> input;
        if (options.input_mode == InputMode::MEMORY)
            input = read_input(options.input_path);
        const std::span<const std::byte> memory_input(input);

        if ((options.workload == Workload::ALL) ||
            (options.workload == Workload::PARSER))
        {
            run_workload(options, memory_input, output, run_parser);
        }
        if ((options.workload == Workload::ALL) ||
            (options.workload == Workload::RECONSTRUCTION))
        {
            run_workload(
                options,
                memory_input,
                output,
                [](const Options& current, std::span<const std::byte> data) {
                    return run_reconstruction<NoOpEventHandler>(
                        current, data, "itch_reconstruction_noop");
                });
        }
        if ((options.workload == Workload::ALL) ||
            (options.workload == Workload::RECONSTRUCTION_COUNTING))
        {
            run_workload(
                options,
                memory_input,
                output,
                [](const Options& current, std::span<const std::byte> data) {
                    return run_reconstruction<CountingEventHandler>(
                        current, data, "itch_reconstruction_counting");
                });
        }

        std::cout << "Results: " << options.output_directory << '\n';
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Benchmark failed: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
