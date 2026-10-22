#ifndef MATCHING_BENCHMARK_SUPPORT_H
#define MATCHING_BENCHMARK_SUPPORT_H

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <chrono>
#endif

namespace matching_benchmark {

class MonotonicClock
{
public:
    using Tick = std::uint64_t;

    [[nodiscard]] static Tick now() noexcept
    {
#if defined(__APPLE__)
        return mach_continuous_time();
#else
        return static_cast<Tick>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
#endif
    }

    [[nodiscard]] static std::uint64_t elapsed_nanoseconds(Tick start,
                                                           Tick stop) noexcept
    {
        const Tick elapsed = stop - start;
#if defined(__APPLE__)
        const mach_timebase_info_data_t timebase = timebase_info();
        const std::uint64_t denominator = timebase.denom;
        const std::uint64_t whole = elapsed / denominator;
        const std::uint64_t remainder = elapsed % denominator;
        return (whole * timebase.numer) +
               ((remainder * timebase.numer) / denominator);
#else
        return elapsed;
#endif
    }

    [[nodiscard]] static std::uint64_t elapsed_picoseconds(Tick start,
                                                           Tick stop) noexcept
    {
        const Tick elapsed = stop - start;
#if defined(__APPLE__)
        const mach_timebase_info_data_t timebase = timebase_info();
        const std::uint64_t denominator = timebase.denom;
        const std::uint64_t whole = elapsed / denominator;
        const std::uint64_t remainder = elapsed % denominator;
        return (whole * timebase.numer * 1'000U) +
               ((remainder * timebase.numer * 1'000U) / denominator);
#else
        return elapsed * 1'000U;
#endif
    }

private:
#if defined(__APPLE__)
    [[nodiscard]] static mach_timebase_info_data_t timebase_info() noexcept
    {
        static const mach_timebase_info_data_t value = []() noexcept {
            mach_timebase_info_data_t result{};
            static_cast<void>(mach_timebase_info(&result));
            return result;
        }();
        return value;
    }
#endif
};

inline void compiler_barrier() noexcept
{
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

struct Distribution
{
    std::size_t samples{0};
    std::uint64_t total{0};
    std::uint64_t minimum{0};
    std::uint64_t p50{0};
    std::uint64_t p90{0};
    std::uint64_t p99{0};
    std::uint64_t p999{0};
    std::uint64_t maximum{0};

    [[nodiscard]] long double mean() const noexcept
    {
        return (samples == 0)
                   ? 0.0L
                   : static_cast<long double>(total) /
                         static_cast<long double>(samples);
    }
};

[[nodiscard]] inline std::uint64_t percentile(
    const std::vector<std::uint64_t>& sorted,
    std::size_t numerator,
    std::size_t denominator)
{
    if (sorted.empty() || (denominator == 0) || (numerator > denominator))
        throw std::invalid_argument("Invalid percentile request");

    const std::size_t rank =
        ((sorted.size() * numerator) + denominator - 1) / denominator;
    const std::size_t index = (rank == 0) ? 0 : rank - 1;
    return sorted[index];
}

[[nodiscard]] inline Distribution summarize(
    std::span<const std::uint64_t> samples)
{
    if (samples.empty())
        throw std::invalid_argument("Cannot summarize an empty sample set");

    std::vector<std::uint64_t> sorted(samples.begin(), samples.end());
    std::sort(sorted.begin(), sorted.end());

    Distribution result;
    result.samples = sorted.size();
    result.minimum = sorted.front();
    result.p50 = percentile(sorted, 50, 100);
    result.p90 = percentile(sorted, 90, 100);
    result.p99 = percentile(sorted, 99, 100);
    result.p999 = percentile(sorted, 999, 1000);
    result.maximum = sorted.back();

    for (const std::uint64_t sample : sorted)
    {
        if (result.total > (std::numeric_limits<std::uint64_t>::max() - sample))
            throw std::overflow_error("Benchmark duration sum overflowed");
        result.total += sample;
    }
    return result;
}

[[nodiscard]] inline std::uint64_t measure_timer_overhead(std::size_t samples)
{
    std::vector<std::uint64_t> durations;
    durations.reserve(samples);
    for (std::size_t index = 0; index < samples; ++index)
    {
        compiler_barrier();
        const MonotonicClock::Tick start = MonotonicClock::now();
        const MonotonicClock::Tick stop = MonotonicClock::now();
        compiler_barrier();
        durations.push_back(MonotonicClock::elapsed_nanoseconds(start, stop));
    }
    return summarize(durations).p50;
}

[[nodiscard]] inline std::uint64_t measure_timer_overhead_picoseconds(
    std::size_t samples)
{
    constexpr std::size_t pairs_per_sample = 256;
    std::vector<std::uint64_t> durations;
    durations.reserve(samples);
    for (std::size_t index = 0; index < samples; ++index)
    {
        compiler_barrier();
        const MonotonicClock::Tick start = MonotonicClock::now();
        MonotonicClock::Tick observed = 0;
        for (std::size_t pair = 0; pair < pairs_per_sample; ++pair)
        {
            const MonotonicClock::Tick first = MonotonicClock::now();
            const MonotonicClock::Tick second = MonotonicClock::now();
            observed ^= second - first;
        }
        const MonotonicClock::Tick stop = MonotonicClock::now() + (observed & 0U);
        compiler_barrier();
        durations.push_back(
            MonotonicClock::elapsed_picoseconds(start, stop) / pairs_per_sample);
    }
    return summarize(durations).p50;
}

[[nodiscard]] inline bool parse_positive_size(std::string_view text,
                                              std::size_t& value) noexcept
{
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return (parsed.ec == std::errc{}) && (parsed.ptr == end) && (value > 0);
}

[[nodiscard]] inline bool parse_nonnegative_size(std::string_view text,
                                                 std::size_t& value) noexcept
{
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return (parsed.ec == std::errc{}) && (parsed.ptr == end);
}

[[nodiscard]] inline std::uint64_t peak_resident_set_bytes() noexcept
{
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0;
    const auto maximum = static_cast<std::uint64_t>(usage.ru_maxrss);
#if defined(__APPLE__)
    return maximum;
#else
    return maximum * 1'024U;
#endif
#else
    return 0;
#endif
}

inline void prepare_output_directory(const std::filesystem::path& directory)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        throw std::runtime_error("Unable to create benchmark output directory: " +
                                 error.message());
}

inline std::ofstream open_output(const std::filesystem::path& path)
{
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Unable to open benchmark output: " + path.string());
    return output;
}

inline void write_distribution_row(std::ofstream& output,
                                   std::string_view benchmark,
                                   std::size_t repetition,
                                   const Distribution& distribution,
                                   std::uint64_t timer_overhead,
                                   std::uint64_t validation)
{
    output << benchmark << ',' << repetition << ',' << distribution.samples << ','
           << distribution.total << ',' << std::fixed << std::setprecision(3)
           << distribution.mean() << ',' << distribution.minimum << ','
           << distribution.p50 << ',' << distribution.p90 << ','
           << distribution.p99 << ',' << distribution.p999 << ','
           << distribution.maximum << ',' << timer_overhead << ',' << validation
           << '\n';
}

inline void write_latency_header(std::ofstream& output)
{
    output << "benchmark,repetition,samples,total_sampled_ns,mean_ns,min_ns,"
              "p50_ns,p90_ns,p99_ns,p99_9_ns,max_ns,timer_overhead_p50_ns,"
              "validation\n";
}

inline void write_batched_distribution_row(std::ofstream& output,
                                           std::string_view benchmark,
                                           std::size_t repetition,
                                           std::size_t batch_size,
                                           const Distribution& picoseconds,
                                           std::uint64_t timer_overhead_ps,
                                           std::uint64_t validation)
{
    constexpr long double picoseconds_per_nanosecond = 1'000.0L;
    output << benchmark << ',' << repetition << ',' << picoseconds.samples << ','
           << batch_size << ',' << picoseconds.total << ',' << std::fixed
           << std::setprecision(3)
           << (picoseconds.mean() / picoseconds_per_nanosecond) << ','
           << (static_cast<long double>(picoseconds.minimum) /
               picoseconds_per_nanosecond)
           << ','
           << (static_cast<long double>(picoseconds.p50) /
               picoseconds_per_nanosecond)
           << ','
           << (static_cast<long double>(picoseconds.p90) /
               picoseconds_per_nanosecond)
           << ','
           << (static_cast<long double>(picoseconds.p99) /
               picoseconds_per_nanosecond)
           << ','
           << (static_cast<long double>(picoseconds.p999) /
               picoseconds_per_nanosecond)
           << ','
           << (static_cast<long double>(picoseconds.maximum) /
               picoseconds_per_nanosecond)
           << ','
           << (static_cast<long double>(timer_overhead_ps) /
               picoseconds_per_nanosecond)
           << ',' << validation << '\n';
}

inline void write_batched_latency_header(std::ofstream& output)
{
    output << "benchmark,repetition,batches,commands_per_batch,"
              "total_amortized_ps,mean_batch_average_ns,min_batch_average_ns,"
              "p50_batch_average_ns,p90_batch_average_ns,p99_batch_average_ns,"
              "p99_9_batch_average_ns,max_batch_average_ns,"
              "timer_call_pair_p50_ns,validation\n";
}

} // namespace matching_benchmark

#endif // MATCHING_BENCHMARK_SUPPORT_H
