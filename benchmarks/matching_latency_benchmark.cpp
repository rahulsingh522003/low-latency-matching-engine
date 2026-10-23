#include "benchmark_support.h"

#include "matching/errors.h"
#include "matching/level.h"
#include "matching/events/event_handler.h"
#include "matching/manager/market_manager.h"
#include "matching/order.h"
#include "matching/book/order_book.h"
#include "matching/symbol.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace support = matching_benchmark;

constexpr matching::SymbolId SYMBOL_ID = 1;
constexpr matching::Price BID_PRICE = 10'000;
constexpr matching::Price ASK_PRICE = 10'100;
constexpr matching::Quantity ORDER_QUANTITY = 100;
constexpr matching::OrderId PREWARM_ID_BASE = 10'000'000;
constexpr matching::OrderId SAMPLE_ID_BASE = 100'000'000;
constexpr std::size_t PREWARM_ORDER_COUNT = 16U * 1024U;
constexpr std::size_t DEPTH_LEVEL_COUNT = 128;

struct Options
{
    std::size_t samples{10'000};
    std::size_t warmup{1'000};
    std::size_t batch_size{64};
    std::size_t repetitions{7};
    std::filesystem::path output_directory;
};

class CountingHandler final : public matching::EventHandler
{
public:
    void reset() noexcept
    {
        callbacks_ = 0;
        executions_ = 0;
        quantity_ = 0;
    }

    [[nodiscard]] std::uint64_t validation() const noexcept
    {
        return callbacks_ ^ (executions_ << 17U) ^ (quantity_ << 31U);
    }

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
                          matching::Quantity quantity) noexcept override
    {
        observe();
        ++executions_;
        quantity_ += quantity;
    }

private:
    std::uint64_t callbacks_{0};
    std::uint64_t executions_{0};
    std::uint64_t quantity_{0};

    void observe() noexcept { ++callbacks_; }
};

class NoOpHandler final : public matching::EventHandler
{
};

template <typename Handler>
class Fixture
{
public:
    Fixture()
        : market_(handler_)
    {
        const matching::SymbolName name{'B', 'E', 'N', 'C', 'H', ' ', ' ', ' '};
        require_ok(market_.add_symbol(matching::Symbol(SYMBOL_ID, name)),
                   "add benchmark symbol");
        require_ok(market_.add_order_book(SYMBOL_ID), "add benchmark order book");
        prewarm_storage();
        reset_handler();
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    [[nodiscard]] matching::MarketManager& market() noexcept { return market_; }
    [[nodiscard]] Handler& handler() noexcept { return handler_; }

    void reset_handler() noexcept
    {
        if constexpr (requires(Handler& handler) { handler.reset(); })
            handler_.reset();
    }

    [[nodiscard]] std::uint64_t validation() const noexcept
    {
        std::uint64_t result =
            static_cast<std::uint64_t>(market_.active_order_count());
        if constexpr (requires(const Handler& handler) { handler.validation(); })
            result ^= handler_.validation();
        return result;
    }

    static void require_ok(matching::ErrorCode result, std::string_view operation)
    {
        if (result != matching::ErrorCode::OK)
            throw std::runtime_error(std::string(operation) + " failed");
    }

private:
    Handler handler_;
    matching::MarketManager market_;

    void prewarm_storage()
    {
        for (std::size_t index = 0; index < PREWARM_ORDER_COUNT; ++index)
        {
            const matching::OrderId id =
                PREWARM_ID_BASE + static_cast<matching::OrderId>(index);
            require_ok(market_.add_order(matching::Order::BuyLimit(
                           id, SYMBOL_ID, BID_PRICE, ORDER_QUANTITY)),
                       "prewarm add");
        }

        for (std::size_t index = 0; index < PREWARM_ORDER_COUNT; ++index)
        {
            const matching::OrderId id =
                PREWARM_ID_BASE + static_cast<matching::OrderId>(index);
            require_ok(market_.delete_order(SYMBOL_ID, id), "prewarm delete");
        }

        if (market_.active_order_count() != 0)
            throw std::runtime_error("Prewarm did not restore an empty registry");
    }
};

[[nodiscard]] bool parse_options(int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view option = argv[index];
        if ((option == "--samples") || (option == "--warmup") ||
            (option == "--batch-size") ||
            (option == "--repetitions") || (option == "--output"))
        {
            if ((index + 1) >= argc)
                return false;
            const std::string_view value = argv[++index];

            if (option == "--output")
                options.output_directory = value;
            else
            {
                std::size_t parsed = 0;
                if (!support::parse_positive_size(value, parsed))
                    return false;
                if (option == "--samples")
                    options.samples = parsed;
                else if (option == "--warmup")
                    options.warmup = parsed;
                else if (option == "--batch-size")
                    options.batch_size = parsed;
                else
                    options.repetitions = parsed;
            }
        }
        else
            return false;
    }

    return !options.output_directory.empty();
}

template <typename Function>
[[nodiscard]] std::uint64_t measure_batch(std::size_t batch_size,
                                          Function&& function)
{
    unsigned error_mask = 0;
    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    for (std::size_t index = 0; index < batch_size; ++index)
        error_mask |= static_cast<unsigned>(std::forward<Function>(function)(index));
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();

    if (error_mask != 0)
        throw std::runtime_error("A timed matching command failed");
    return support::MonotonicClock::elapsed_picoseconds(start, stop) / batch_size;
}

void write_raw_samples(std::ofstream& raw_output,
                       std::string_view benchmark,
                       std::size_t repetition,
                       const std::vector<std::uint64_t>& samples)
{
    for (std::size_t index = 0; index < samples.size(); ++index)
        raw_output << benchmark << ',' << repetition << ',' << index << ','
                   << samples[index] << '\n';
}

void add_depth_anchors(Fixture<CountingHandler>& fixture)
{
    for (std::size_t index = 0; index < DEPTH_LEVEL_COUNT; ++index)
    {
        const matching::OrderId id = 2'000'000 + index;
        const matching::Price price =
            BID_PRICE - 1 - static_cast<matching::Price>(index);
        Fixture<CountingHandler>::require_ok(
            fixture.market().add_order(
                matching::Order::BuyLimit(id, SYMBOL_ID, price, ORDER_QUANTITY)),
            "add bid depth anchor");
    }

    for (std::size_t index = 0; index < DEPTH_LEVEL_COUNT; ++index)
    {
        const matching::OrderId id = 3'000'000 + index;
        const matching::Price price =
            ASK_PRICE + 1 + static_cast<matching::Price>(index);
        Fixture<CountingHandler>::require_ok(
            fixture.market().add_order(
                matching::Order::SellLimit(id, SYMBOL_ID, price, ORDER_QUANTITY)),
            "add ask depth anchor");
    }
}

void remove_depth_anchors(Fixture<CountingHandler>& fixture)
{
    for (std::size_t index = 0; index < DEPTH_LEVEL_COUNT; ++index)
    {
        Fixture<CountingHandler>::require_ok(
            fixture.market().delete_order(SYMBOL_ID, 2'000'000 + index),
            "delete bid depth anchor");
        Fixture<CountingHandler>::require_ok(
            fixture.market().delete_order(SYMBOL_ID, 3'000'000 + index),
            "delete ask depth anchor");
    }
}

template <typename Operation, typename Reset>
std::vector<std::uint64_t> record_batches(std::size_t warmup,
                                          std::size_t samples,
                                          Operation&& operation,
                                          Reset&& reset)
{
    for (std::size_t index = 0; index < warmup; ++index)
        static_cast<void>(operation(index, false));

    std::forward<Reset>(reset)();

    std::vector<std::uint64_t> result;
    result.reserve(samples);
    for (std::size_t index = 0; index < samples; ++index)
        result.push_back(operation(warmup + index, true));
    return result;
}

std::vector<std::uint64_t> benchmark_add_existing(std::size_t warmup,
                                                  std::size_t samples,
                                                  std::size_t batch_size,
                                                  std::uint64_t& validation)
{
    Fixture<CountingHandler> fixture;
    auto& market = fixture.market();
    Fixture<CountingHandler>::require_ok(
        market.add_order(matching::Order::BuyLimit(
            1, SYMBOL_ID, BID_PRICE, ORDER_QUANTITY)),
        "add level anchor");

    std::vector<matching::Order> orders(batch_size);
    auto operation = [&](std::size_t batch, bool timed) {
        const matching::OrderId first_id =
            SAMPLE_ID_BASE +
            (static_cast<matching::OrderId>(batch) * batch_size);
        for (std::size_t index = 0; index < batch_size; ++index)
        {
            orders[index] = matching::Order::BuyLimit(
                first_id + index, SYMBOL_ID, BID_PRICE, ORDER_QUANTITY);
        }

        std::uint64_t elapsed = 0;
        if (timed)
        {
            elapsed = measure_batch(batch_size, [&](std::size_t index) {
                return market.add_order(orders[index]);
            });
        }
        else
        {
            for (const matching::Order& order : orders)
            {
                Fixture<CountingHandler>::require_ok(market.add_order(order),
                                                     "warm add existing");
            }
        }
        for (const matching::Order& order : orders)
        {
            Fixture<CountingHandler>::require_ok(
                market.delete_order(SYMBOL_ID, order.id),
                "cleanup add existing");
        }
        return elapsed;
    };

    std::vector<std::uint64_t> result =
        record_batches(warmup, samples, operation, [&]() {
            fixture.reset_handler();
        });
    validation = fixture.validation();
    Fixture<CountingHandler>::require_ok(
        market.delete_order(SYMBOL_ID, 1), "delete level anchor");
    if (market.active_order_count() != 0)
        throw std::runtime_error("Add benchmark left active orders");
    return result;
}

std::vector<std::uint64_t> benchmark_cancel(std::size_t warmup,
                                            std::size_t samples,
                                            std::size_t batch_size,
                                            std::uint64_t& validation)
{
    Fixture<CountingHandler> fixture;
    auto& market = fixture.market();
    Fixture<CountingHandler>::require_ok(
        market.add_order(matching::Order::BuyLimit(
            1, SYMBOL_ID, BID_PRICE, ORDER_QUANTITY)),
        "add cancellation level anchor");

    std::vector<matching::OrderId> ids(batch_size);
    auto operation = [&](std::size_t batch, bool timed) {
        const matching::OrderId first_id =
            SAMPLE_ID_BASE +
            (static_cast<matching::OrderId>(batch) * batch_size);
        for (std::size_t index = 0; index < batch_size; ++index)
        {
            ids[index] = first_id + index;
            Fixture<CountingHandler>::require_ok(
                market.add_order(matching::Order::BuyLimit(
                    ids[index], SYMBOL_ID, BID_PRICE, ORDER_QUANTITY)),
                "prepare cancellation");
        }
        if (timed)
        {
            return measure_batch(batch_size, [&](std::size_t index) {
                return market.delete_order(SYMBOL_ID, ids[index]);
            });
        }
        for (const matching::OrderId id : ids)
        {
            Fixture<CountingHandler>::require_ok(
                market.delete_order(SYMBOL_ID, id), "warm cancellation");
        }
        return std::uint64_t{0};
    };

    std::vector<std::uint64_t> result =
        record_batches(warmup, samples, operation, [&]() {
            fixture.reset_handler();
        });
    validation = fixture.validation();
    Fixture<CountingHandler>::require_ok(
        market.delete_order(SYMBOL_ID, 1), "delete cancellation level anchor");
    if (market.active_order_count() != 0)
        throw std::runtime_error("Cancellation benchmark left active orders");
    return result;
}

std::vector<std::uint64_t> benchmark_modify_quantity(std::size_t warmup,
                                                     std::size_t samples,
                                                     std::size_t batch_size,
                                                     std::uint64_t& validation)
{
    Fixture<CountingHandler> fixture;
    auto& market = fixture.market();
    Fixture<CountingHandler>::require_ok(
        market.add_order(matching::Order::BuyLimit(
            1, SYMBOL_ID, BID_PRICE, ORDER_QUANTITY)),
        "add modification level anchor");

    std::vector<matching::OrderId> ids(batch_size);
    auto operation = [&](std::size_t batch, bool timed) {
        const matching::OrderId first_id =
            SAMPLE_ID_BASE +
            (static_cast<matching::OrderId>(batch) * batch_size);
        for (std::size_t index = 0; index < batch_size; ++index)
        {
            ids[index] = first_id + index;
            Fixture<CountingHandler>::require_ok(
                market.add_order(matching::Order::BuyLimit(
                    ids[index], SYMBOL_ID, BID_PRICE, ORDER_QUANTITY * 2)),
                "prepare modification");
        }

        std::uint64_t elapsed = 0;
        if (timed)
        {
            elapsed = measure_batch(batch_size, [&](std::size_t index) {
                return market.modify_order(SYMBOL_ID,
                                           ids[index],
                                           BID_PRICE,
                                           ORDER_QUANTITY);
            });
        }
        else
        {
            for (const matching::OrderId id : ids)
            {
                Fixture<CountingHandler>::require_ok(
                    market.modify_order(
                        SYMBOL_ID, id, BID_PRICE, ORDER_QUANTITY),
                    "warm modification");
            }
        }
        for (const matching::OrderId id : ids)
        {
            Fixture<CountingHandler>::require_ok(
                market.delete_order(SYMBOL_ID, id), "cleanup modification");
        }
        return elapsed;
    };

    std::vector<std::uint64_t> result =
        record_batches(warmup, samples, operation, [&]() {
            fixture.reset_handler();
        });
    validation = fixture.validation();
    Fixture<CountingHandler>::require_ok(
        market.delete_order(SYMBOL_ID, 1), "delete modification level anchor");
    if (market.active_order_count() != 0)
        throw std::runtime_error("Modification benchmark left active orders");
    return result;
}

std::vector<std::uint64_t> benchmark_modify_price(std::size_t warmup,
                                                  std::size_t samples,
                                                  std::size_t batch_size,
                                                  std::uint64_t& validation)
{
    Fixture<CountingHandler> fixture;
    auto& market = fixture.market();
    add_depth_anchors(fixture);

    std::vector<matching::OrderId> ids(batch_size);
    auto operation = [&](std::size_t batch, bool timed) {
        const matching::OrderId first_id =
            SAMPLE_ID_BASE +
            (static_cast<matching::OrderId>(batch) * batch_size);
        for (std::size_t index = 0; index < batch_size; ++index)
        {
            ids[index] = first_id + index;
            Fixture<CountingHandler>::require_ok(
                market.add_order(matching::Order::BuyLimit(
                    ids[index], SYMBOL_ID, BID_PRICE - 1, ORDER_QUANTITY)),
                "prepare price modification");
        }

        std::uint64_t elapsed = 0;
        if (timed)
        {
            elapsed = measure_batch(batch_size, [&](std::size_t index) {
                return market.modify_order(SYMBOL_ID,
                                           ids[index],
                                           BID_PRICE - 2,
                                           ORDER_QUANTITY);
            });
        }
        else
        {
            for (const matching::OrderId id : ids)
            {
                Fixture<CountingHandler>::require_ok(
                    market.modify_order(
                        SYMBOL_ID, id, BID_PRICE - 2, ORDER_QUANTITY),
                    "warm price modification");
            }
        }
        for (const matching::OrderId id : ids)
        {
            Fixture<CountingHandler>::require_ok(
                market.delete_order(SYMBOL_ID, id),
                "cleanup price modification");
        }
        return elapsed;
    };

    std::vector<std::uint64_t> result =
        record_batches(warmup, samples, operation, [&]() {
            fixture.reset_handler();
        });
    validation = fixture.validation();
    remove_depth_anchors(fixture);
    if (market.active_order_count() != 0)
        throw std::runtime_error("Price-modification benchmark left active orders");
    return result;
}

std::vector<std::uint64_t> benchmark_limit_match(std::size_t warmup,
                                                 std::size_t samples,
                                                 std::size_t batch_size,
                                                 std::uint64_t& validation)
{
    Fixture<CountingHandler> fixture;
    auto& market = fixture.market();
    add_depth_anchors(fixture);
    market.enable_matching();

    std::vector<matching::Order> aggressors(batch_size);
    auto operation = [&](std::size_t batch, bool timed) {
        const matching::OrderId first_id =
            SAMPLE_ID_BASE +
            (static_cast<matching::OrderId>(batch) * batch_size * 2U);
        for (std::size_t index = 0; index < batch_size; ++index)
        {
            const matching::OrderId passive_id = first_id + (index * 2U);
            Fixture<CountingHandler>::require_ok(
                market.add_order(matching::Order::SellLimit(
                    passive_id, SYMBOL_ID, ASK_PRICE, ORDER_QUANTITY)),
                "prepare passive match");
            aggressors[index] = matching::Order::BuyLimit(
                passive_id + 1,
                SYMBOL_ID,
                ASK_PRICE,
                ORDER_QUANTITY,
                matching::OrderTimeInForce::IOC);
        }

        if (timed)
        {
            return measure_batch(batch_size, [&](std::size_t index) {
                return market.add_order(aggressors[index]);
            });
        }
        for (const matching::Order& aggressor : aggressors)
        {
            Fixture<CountingHandler>::require_ok(market.add_order(aggressor),
                                                 "warm limit match");
        }
        return std::uint64_t{0};
    };

    std::vector<std::uint64_t> result =
        record_batches(warmup, samples, operation, [&]() {
            fixture.reset_handler();
        });
    validation = fixture.validation();
    remove_depth_anchors(fixture);
    if (market.active_order_count() != 0)
        throw std::runtime_error("Matching benchmark left active orders");
    return result;
}

template <typename Handler>
std::pair<std::uint64_t, std::uint64_t> benchmark_mixed_throughput(
    std::size_t cycles)
{
    Fixture<Handler> fixture;
    auto& market = fixture.market();
    market.enable_matching();
    fixture.reset_handler();

    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    for (std::size_t index = 0; index < cycles; ++index)
    {
        const matching::OrderId base =
            SAMPLE_ID_BASE + (static_cast<matching::OrderId>(index) * 4U);
        Fixture<Handler>::require_ok(
            market.add_order(matching::Order::BuyLimit(
                base, SYMBOL_ID, BID_PRICE, ORDER_QUANTITY * 2)),
            "mixed add bid");
        Fixture<Handler>::require_ok(
            market.modify_order(
                SYMBOL_ID, base, BID_PRICE, ORDER_QUANTITY),
            "mixed modify bid");
        Fixture<Handler>::require_ok(market.delete_order(SYMBOL_ID, base),
                                     "mixed delete bid");

        Fixture<Handler>::require_ok(
            market.add_order(matching::Order::SellLimit(
                base + 1, SYMBOL_ID, ASK_PRICE, ORDER_QUANTITY)),
            "mixed add ask");
        Fixture<Handler>::require_ok(
            market.add_order(matching::Order::BuyLimit(
                base + 2,
                SYMBOL_ID,
                ASK_PRICE,
                ORDER_QUANTITY,
                matching::OrderTimeInForce::IOC)),
            "mixed execute ask");
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();

    if (market.active_order_count() != 0)
        throw std::runtime_error("Mixed benchmark left active orders");
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop),
            fixture.validation()};
}

using LatencyFunction = std::vector<std::uint64_t> (*)(
    std::size_t, std::size_t, std::size_t, std::uint64_t&);

struct LatencyWorkload
{
    std::string_view name;
    LatencyFunction function;
};

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options))
    {
        std::cerr << "Usage: matching_latency_benchmark --output <directory> "
                     "[--samples N] [--warmup N] [--batch-size N] "
                     "[--repetitions N]\n";
        return 2;
    }

    try
    {
        support::prepare_output_directory(options.output_directory);
        std::ofstream summary = support::open_output(
            options.output_directory / "matching_latency_summary.csv");
        std::ofstream raw = support::open_output(
            options.output_directory / "matching_latency_raw.csv");
        std::ofstream throughput = support::open_output(
            options.output_directory / "matching_throughput.csv");

        support::write_batched_latency_header(summary);
        raw << "benchmark,repetition,batch,amortized_ps_per_command\n";
        throughput << "benchmark,repetition,commands,total_ns,ns_per_command,"
                      "commands_per_second,validation\n";

        const std::uint64_t timer_overhead =
            support::measure_timer_overhead_picoseconds(2'000);
        const std::vector<LatencyWorkload> workloads{
            {"add_existing_level", benchmark_add_existing},
            {"cancel_existing_level", benchmark_cancel},
            {"modify_quantity_decrease", benchmark_modify_quantity},
            {"modify_price_existing_level", benchmark_modify_price},
            {"match_one_limit_order", benchmark_limit_match},
        };

        for (std::size_t repetition = 1; repetition <= options.repetitions;
             ++repetition)
        {
            for (const LatencyWorkload& workload : workloads)
            {
                std::uint64_t validation = 0;
                const std::vector<std::uint64_t> samples = workload.function(
                    options.warmup,
                    options.samples,
                    options.batch_size,
                    validation);
                const support::Distribution distribution = support::summarize(samples);
                support::write_batched_distribution_row(summary,
                                                        workload.name,
                                                        repetition,
                                                        options.batch_size,
                                                        distribution,
                                                        timer_overhead,
                                                        validation);
                write_raw_samples(raw, workload.name, repetition, samples);
            }

            constexpr std::size_t commands_per_cycle = 5;
            const auto write_throughput = [&](std::string_view name,
                                              auto result) {
                const std::size_t command_count =
                    options.samples * options.batch_size * commands_per_cycle;
                const long double ns_per_command =
                    static_cast<long double>(result.first) /
                    static_cast<long double>(command_count);
                const long double commands_per_second =
                    (static_cast<long double>(command_count) * 1'000'000'000.0L) /
                    static_cast<long double>(result.first);
                throughput << name << ',' << repetition << ',' << command_count << ','
                           << result.first << ',' << std::fixed << std::setprecision(3)
                           << ns_per_command << ',' << commands_per_second << ','
                           << result.second << '\n';
            };

            write_throughput(
                "mixed_noop_callbacks",
                benchmark_mixed_throughput<NoOpHandler>(
                    options.samples * options.batch_size));
            write_throughput(
                "mixed_counting_callbacks",
                benchmark_mixed_throughput<CountingHandler>(
                    options.samples * options.batch_size));

            std::cout << "Completed matching repetition " << repetition << '/'
                      << options.repetitions << '\n';
        }

        std::cout << "Estimated timer call-pair cost p50: "
                  << (static_cast<long double>(timer_overhead) / 1'000.0L)
                  << " ns\n"
                  << "Results: " << options.output_directory << '\n';
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Benchmark failed: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
