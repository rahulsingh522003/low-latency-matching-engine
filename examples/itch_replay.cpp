#include "providers/nasdaq/itch/decoder.h"
#include "providers/nasdaq/itch/market_adapter.h"

#include "matching/level.h"
#include "matching/market_handler.h"
#include "matching/market_manager.h"
#include "matching/order.h"
#include "matching/order_book.h"
#include "matching/symbol.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

namespace itch = providers::nasdaq::itch;

constexpr std::uint64_t FNV_OFFSET_BASIS = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t FNV_PRIME = 1'099'511'628'211ULL;
constexpr std::size_t DEFAULT_CHUNK_SIZE = 64U * 1024U;
constexpr std::uint32_t ITCH_SYMBOL_CAPACITY = 1U << 16U;

void mix_byte(std::uint64_t& hash, std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= FNV_PRIME;
}

template <typename Integer>
void mix_integer(std::uint64_t& hash, Integer value) noexcept
{
    if constexpr (std::is_same_v<Integer, bool>)
    {
        mix_byte(hash, value ? 1U : 0U);
    }
    else if constexpr (std::is_enum_v<Integer>)
    {
        mix_integer(hash, static_cast<std::underlying_type_t<Integer>>(value));
    }
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

class ReplayMarketHandler final : public matching::MarketHandler
{
public:
    [[nodiscard]] std::uint64_t callbacks() const noexcept { return callbacks_; }
    [[nodiscard]] std::uint64_t event_hash() const noexcept { return event_hash_; }
    [[nodiscard]] std::uint64_t book_summary_callbacks() const noexcept
    {
        return book_summary_callbacks_;
    }
    [[nodiscard]] std::size_t maximum_symbols() const noexcept
    {
        return maximum_symbols_;
    }
    [[nodiscard]] std::size_t maximum_books() const noexcept
    {
        return maximum_books_;
    }
    [[nodiscard]] std::size_t maximum_orders() const noexcept
    {
        return maximum_orders_;
    }
    [[nodiscard]] std::size_t maximum_displayed_levels() const noexcept
    {
        return maximum_displayed_levels_;
    }
    [[nodiscard]] std::size_t maximum_orders_at_level() const noexcept
    {
        return maximum_orders_at_level_;
    }
    [[nodiscard]] std::uint64_t added_orders() const noexcept
    {
        return added_orders_;
    }
    [[nodiscard]] std::uint64_t updated_orders() const noexcept
    {
        return updated_orders_;
    }
    [[nodiscard]] std::uint64_t deleted_orders() const noexcept
    {
        return deleted_orders_;
    }
    [[nodiscard]] std::uint64_t executed_orders() const noexcept
    {
        return executed_orders_;
    }

protected:
    void on_add_symbol(const matching::Symbol& symbol) noexcept override
    {
        begin_event(1);
        mix_integer(event_hash_, symbol.id);
        mix_text(event_hash_, symbol.name);
        ++symbols_;
        if (symbols_ > maximum_symbols_)
            maximum_symbols_ = symbols_;
    }

    void on_delete_symbol(const matching::Symbol& symbol) noexcept override
    {
        begin_event(2);
        mix_integer(event_hash_, symbol.id);
        --symbols_;
    }

    void on_add_order_book(const matching::OrderBook& book) noexcept override
    {
        begin_event(3);
        mix_integer(event_hash_, book.symbol().id);
        ++books_;
        if (books_ > maximum_books_)
            maximum_books_ = books_;
    }

    void on_update_order_book(const matching::OrderBook& book,
                              bool top) noexcept override
    {
        begin_event(4);
        ++book_summary_callbacks_;
        mix_integer(event_hash_, book.symbol().id);
        mix_integer(event_hash_, top);
        const std::size_t displayed_levels =
            (book.bids().size() > book.asks().size()) ? book.bids().size()
                                                     : book.asks().size();
        if (displayed_levels > maximum_displayed_levels_)
            maximum_displayed_levels_ = displayed_levels;
    }

    void on_delete_order_book(const matching::OrderBook& book) noexcept override
    {
        begin_event(5);
        mix_integer(event_hash_, book.symbol().id);
        --books_;
    }

    void on_add_level(const matching::OrderBook& book,
                      const matching::Level& level,
                      bool top) noexcept override
    {
        record_level(6, book, level, top);
    }

    void on_update_level(const matching::OrderBook& book,
                         const matching::Level& level,
                         bool top) noexcept override
    {
        record_level(7, book, level, top);
    }

    void on_delete_level(const matching::OrderBook& book,
                         const matching::Level& level,
                         bool top) noexcept override
    {
        record_level(8, book, level, top);
    }

    void on_add_order(const matching::Order& order) noexcept override
    {
        begin_event(9);
        mix_order(event_hash_, order);
        ++orders_;
        ++added_orders_;
        if (orders_ > maximum_orders_)
            maximum_orders_ = orders_;
    }

    void on_update_order(const matching::Order& order) noexcept override
    {
        begin_event(10);
        mix_order(event_hash_, order);
        ++updated_orders_;
    }

    void on_delete_order(const matching::Order& order) noexcept override
    {
        begin_event(11);
        mix_order(event_hash_, order);
        --orders_;
        ++deleted_orders_;
    }

    void on_execute_order(const matching::Order& order,
                          matching::Price price,
                          matching::Quantity quantity) noexcept override
    {
        begin_event(12);
        mix_order(event_hash_, order);
        mix_integer(event_hash_, price);
        mix_integer(event_hash_, quantity);
        ++executed_orders_;
    }

private:
    std::uint64_t callbacks_{0};
    std::uint64_t book_summary_callbacks_{0};
    std::uint64_t event_hash_{FNV_OFFSET_BASIS};
    std::size_t symbols_{0};
    std::size_t books_{0};
    std::size_t orders_{0};
    std::size_t maximum_symbols_{0};
    std::size_t maximum_books_{0};
    std::size_t maximum_orders_{0};
    std::size_t maximum_displayed_levels_{0};
    std::size_t maximum_orders_at_level_{0};
    std::uint64_t added_orders_{0};
    std::uint64_t updated_orders_{0};
    std::uint64_t deleted_orders_{0};
    std::uint64_t executed_orders_{0};

    void begin_event(std::uint8_t event_type) noexcept
    {
        ++callbacks_;
        mix_byte(event_hash_, event_type);
    }

    void record_level(std::uint8_t event_type,
                      const matching::OrderBook& book,
                      const matching::Level& level,
                      bool top) noexcept
    {
        begin_event(event_type);
        mix_integer(event_hash_, book.symbol().id);
        mix_integer(event_hash_, level.type);
        mix_integer(event_hash_, level.price);
        mix_integer(event_hash_, level.total_quantity);
        mix_integer(event_hash_, level.displayed_quantity);
        mix_integer(event_hash_, level.order_count);
        mix_integer(event_hash_, top);
        if (level.order_count > maximum_orders_at_level_)
            maximum_orders_at_level_ = level.order_count;
    }
};

struct StateSummary
{
    std::uint64_t hash{FNV_OFFSET_BASIS};
    std::size_t symbols{0};
    std::size_t books{0};
    std::size_t levels{0};
    std::size_t orders{0};
    matching::Quantity quantity{0};
};

template <typename Tree>
void summarize_tree(StateSummary& summary,
                    std::uint8_t tree_type,
                    const Tree& tree) noexcept
{
    mix_byte(summary.hash, tree_type);
    mix_integer(summary.hash, tree.size());
    for (const matching::LevelNode& level : tree)
    {
        ++summary.levels;
        mix_integer(summary.hash, level.type);
        mix_integer(summary.hash, level.price);
        mix_integer(summary.hash, level.total_quantity);
        mix_integer(summary.hash, level.displayed_quantity);
        mix_integer(summary.hash, level.order_count);

        for (const matching::OrderNode* order = level.orders.front();
             order != nullptr;
             order = order->next)
        {
            ++summary.orders;
            summary.quantity += order->quantity;
            mix_order(summary.hash, *order);
        }
    }
}

[[nodiscard]] StateSummary summarize_market(
    const matching::MarketManager& market) noexcept
{
    StateSummary summary;
    mix_integer(summary.hash, market.symbol_count());
    mix_integer(summary.hash, market.order_book_count());
    mix_integer(summary.hash, market.active_order_count());

    for (std::uint32_t symbol_id = 0; symbol_id < ITCH_SYMBOL_CAPACITY;
         ++symbol_id)
    {
        const matching::Symbol* symbol = market.find_symbol(symbol_id);
        if (symbol == nullptr)
            continue;

        ++summary.symbols;
        mix_byte(summary.hash, 0xA1U);
        mix_integer(summary.hash, symbol->id);
        mix_text(summary.hash, symbol->name);

        const matching::OrderBook* book = market.find_order_book(symbol_id);
        if (book == nullptr)
            continue;

        ++summary.books;
        summarize_tree(summary, 1, book->bids());
        summarize_tree(summary, 2, book->asks());
        summarize_tree(summary, 3, book->buy_stops());
        summarize_tree(summary, 4, book->sell_stops());
        summarize_tree(summary, 5, book->trailing_buy_stops());
        summarize_tree(summary, 6, book->trailing_sell_stops());
    }

    mix_integer(summary.hash, summary.symbols);
    mix_integer(summary.hash, summary.books);
    mix_integer(summary.hash, summary.levels);
    mix_integer(summary.hash, summary.orders);
    mix_integer(summary.hash, summary.quantity);
    return summary;
}

struct Options
{
    std::string_view input_path;
    itch::PriceConversionPolicy price_policy;
    std::size_t chunk_size{DEFAULT_CHUNK_SIZE};
};

[[nodiscard]] bool parse_size(std::string_view text,
                              std::size_t& value) noexcept
{
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return (result.ec == std::errc{}) && (result.ptr == end) && (value > 0);
}

[[nodiscard]] bool parse_options(int argc, char** argv, Options& options)
{
    if ((argc < 3) || (argc > 5))
        return false;

    options.input_path = argv[1];
    const std::string_view policy = argv[2];
    if (policy == "--exact-cent")
        options.price_policy = itch::PriceConversionPolicy::REQUIRE_EXACT_CENT;
    else if (policy == "--round-cent")
        options.price_policy =
            itch::PriceConversionPolicy::ROUND_TO_NEAREST_CENT;
    else
        return false;

    if (argc == 3)
        return true;
    if ((argc != 5) || (std::string_view(argv[3]) != "--chunk-size"))
        return false;
    return parse_size(argv[4], options.chunk_size);
}

void print_hex(std::string_view label, std::uint64_t value)
{
    std::cout << label << "0x" << std::hex << std::setw(16) << std::setfill('0')
              << value << std::dec << std::setfill(' ') << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    Options options{};
    if (!parse_options(argc, argv, options))
    {
        std::cerr << "Usage: itch_replay <file> (--exact-cent|--round-cent) "
                     "[--chunk-size bytes]\n";
        return 2;
    }

    std::ifstream input(std::string(options.input_path), std::ios::binary);
    if (!input)
    {
        std::cerr << "Unable to open input file: " << options.input_path << '\n';
        return 2;
    }

    ReplayMarketHandler market_handler;
    matching::MarketManager market(market_handler);
    itch::MarketAdapter adapter(market, options.price_policy);
    itch::Decoder decoder(adapter);

    std::vector<std::byte> buffer(options.chunk_size);
    std::uint64_t bytes_read = 0;
    while (input)
    {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count <= 0)
            break;

        const auto size = static_cast<std::size_t>(count);
        const itch::DecodeStatus status =
            decoder.process(std::span(buffer.data(), size));
        bytes_read += size;
        if (status != itch::DecodeStatus::OK)
        {
            std::cerr << "Replay failed after " << bytes_read
                      << " bytes; decoder status=" << static_cast<int>(status)
                      << "; adapter status=" << adapter.status()
                      << "; engine error=" << adapter.engine_error() << '\n';
            return 1;
        }
    }

    if (input.bad())
    {
        std::cerr << "I/O failure after " << bytes_read << " bytes\n";
        return 1;
    }

    const itch::DecodeStatus final_status = decoder.finish();
    if (final_status != itch::DecodeStatus::OK)
    {
        std::cerr << "Replay ended with decoder status="
                  << static_cast<int>(final_status) << '\n';
        return 1;
    }

    const itch::MarketAdapterStatistics& feed = adapter.statistics();
    const StateSummary state = summarize_market(market);

    std::cout << "Replay completed\n"
              << "Bytes: " << bytes_read << '\n'
              << "Messages: " << feed.messages << '\n'
              << "Unknown messages: " << feed.unknown_messages << '\n'
              << "Ignored messages: " << feed.ignored_messages << '\n'
              << "Directory messages: " << feed.symbol_directory_messages << '\n'
              << "Repeated directory messages: "
              << feed.repeated_directory_messages << '\n'
              << "Add messages: " << feed.add_order_messages << '\n'
              << "Execute messages: " << feed.execute_order_messages << '\n'
              << "Cancel messages: " << feed.cancel_order_messages << '\n'
              << "Delete messages: " << feed.delete_order_messages << '\n'
              << "Replace messages: " << feed.replace_order_messages << '\n'
              << "Exact-cent prices: " << feed.exact_cent_prices << '\n'
              << "Rounded prices: " << feed.rounded_prices << '\n'
              << "Minimum-cent prices: " << feed.minimum_cent_prices << '\n'
              << "Market callbacks: " << market_handler.callbacks() << '\n'
              << "State-change callbacks: "
              << (market_handler.callbacks() -
                  market_handler.book_summary_callbacks())
              << '\n'
              << "Maximum symbols: " << market_handler.maximum_symbols() << '\n'
              << "Maximum books: " << market_handler.maximum_books() << '\n'
              << "Maximum active orders: " << market_handler.maximum_orders() << '\n'
              << "Maximum displayed levels/side: "
              << market_handler.maximum_displayed_levels() << '\n'
              << "Maximum orders/level: "
              << market_handler.maximum_orders_at_level() << '\n'
              << "Order callbacks (add/update/delete/execute): "
              << market_handler.added_orders() << '/'
              << market_handler.updated_orders() << '/'
              << market_handler.deleted_orders() << '/'
              << market_handler.executed_orders() << '\n'
              << "Final symbols/books/levels/orders: " << state.symbols << '/'
              << state.books << '/' << state.levels << '/' << state.orders << '\n'
              << "Final resting quantity: " << state.quantity << '\n';
    print_hex("Event hash: ", market_handler.event_hash());
    print_hex("State hash: ", state.hash);

    if ((state.symbols != market.symbol_count()) ||
        (state.books != market.order_book_count()) ||
        (state.orders != market.active_order_count()))
    {
        std::cerr << "Final-state traversal disagrees with manager counters\n";
        return 1;
    }

    return 0;
}
