#include "benchmark_support.h"

#include "matching/containers/hash_map.h"
#include "matching/containers/intrusive_avl_tree.h"
#include "matching/containers/intrusive_list.h"
#include "matching/fast_hash.h"
#include "matching/memory/object_pool.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

namespace support = matching_benchmark;

struct Options
{
    std::size_t operations{2'000'000};
    std::size_t elements{65'536};
    std::size_t repetitions{7};
    std::filesystem::path output_directory;
};

struct Result
{
    std::uint64_t elapsed_ns{0};
    std::uint64_t validation{0};
};

[[nodiscard]] bool parse_options(int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view option = argv[index];
        if ((option == "--operations") || (option == "--elements") ||
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
                if (option == "--operations")
                    options.operations = parsed;
                else if (option == "--elements")
                    options.elements = parsed;
                else
                    options.repetitions = parsed;
            }
        }
        else
            return false;
    }

    return !options.output_directory.empty();
}

[[nodiscard]] std::uint64_t next_random(std::uint64_t& state) noexcept
{
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
}

[[nodiscard]] std::vector<std::size_t> make_permutation(std::size_t size)
{
    std::vector<std::size_t> permutation(size);
    for (std::size_t index = 0; index < size; ++index)
        permutation[index] = index;

    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (std::size_t remaining = size; remaining > 1; --remaining)
    {
        const std::size_t selected = static_cast<std::size_t>(
            next_random(state) % static_cast<std::uint64_t>(remaining));
        std::swap(permutation[remaining - 1], permutation[selected]);
    }
    return permutation;
}

struct PoolValue
{
    std::uint64_t key;
    std::uint64_t value;

    PoolValue(std::uint64_t object_key, std::uint64_t object_value) noexcept
        : key(object_key),
          value(object_value)
    {
    }
};

#if defined(__clang__)
[[clang::noinline]]
#elif defined(__GNUC__)
[[gnu::noinline]]
#endif
[[nodiscard]] std::uint64_t observe_object(const PoolValue* object) noexcept
{
    return object->key ^ (object->value << 1U) ^
           static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(object));
}

Result benchmark_pool(std::size_t operations,
                      std::size_t batch_size,
                      std::size_t page_size)
{
    matching::memory::ObjectPool<PoolValue> pool(page_size);
    if (!pool.reserve(batch_size))
        throw std::runtime_error("Object-pool reservation failed");

    std::vector<PoolValue*> objects(batch_size, nullptr);
    std::uint64_t checksum = 0;
    std::size_t completed = 0;

    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    while (completed < operations)
    {
        const std::size_t count = std::min(batch_size, operations - completed);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto value = static_cast<std::uint64_t>(completed + index + 1);
            objects[index] = pool.create(value, value * 3U);
            if (objects[index] == nullptr)
                throw std::runtime_error("Object-pool allocation failed");
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            checksum ^= observe_object(objects[index]);
            pool.release(*objects[index]);
        }
        completed += count;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();

    if ((pool.live_count() != 0) || (pool.capacity() < batch_size))
        throw std::runtime_error("Object-pool benchmark invariant failed");
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

Result benchmark_new_delete(std::size_t operations, std::size_t batch_size)
{
    std::vector<PoolValue*> objects(batch_size, nullptr);
    std::uint64_t checksum = 0;
    std::size_t completed = 0;

    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    while (completed < operations)
    {
        const std::size_t count = std::min(batch_size, operations - completed);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto value = static_cast<std::uint64_t>(completed + index + 1);
            objects[index] = new PoolValue(value, value * 3U);
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            checksum ^= observe_object(objects[index]);
            delete objects[index];
            objects[index] = nullptr;
        }
        completed += count;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

struct ListNode : matching::containers::IntrusiveList<ListNode>::Node
{
    std::uint64_t value{0};
};

Result benchmark_intrusive_list(std::size_t operations, std::size_t batch_size)
{
    std::unique_ptr<ListNode[]> nodes = std::make_unique<ListNode[]>(batch_size);
    std::uint64_t checksum = 0;
    std::size_t completed = 0;

    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    while (completed < operations)
    {
        const std::size_t count = std::min(batch_size, operations - completed);
        matching::containers::IntrusiveList<ListNode> list;
        for (std::size_t index = 0; index < count; ++index)
        {
            nodes[index].value = static_cast<std::uint64_t>(completed + index + 1);
            list.push_back(nodes[index]);
        }
        for (std::size_t index = count; index > 0; --index)
        {
            checksum ^= nodes[index - 1].value;
            list.unlink(nodes[index - 1]);
        }
        if (!list.empty())
            throw std::runtime_error("Intrusive-list benchmark did not drain");
        completed += count;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

Result benchmark_std_list(std::size_t operations, std::size_t batch_size)
{
    using List = std::list<std::uint64_t>;
    std::vector<List::iterator> positions(batch_size);
    std::uint64_t checksum = 0;
    std::size_t completed = 0;

    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    while (completed < operations)
    {
        const std::size_t count = std::min(batch_size, operations - completed);
        List list;
        for (std::size_t index = 0; index < count; ++index)
        {
            list.push_back(static_cast<std::uint64_t>(completed + index + 1));
            positions[index] = std::prev(list.end());
        }
        for (std::size_t index = count; index > 0; --index)
        {
            checksum ^= *positions[index - 1];
            list.erase(positions[index - 1]);
        }
        if (!list.empty())
            throw std::runtime_error("std::list benchmark did not drain");
        completed += count;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

[[nodiscard]] std::vector<std::uint64_t> make_keys(std::size_t count)
{
    std::vector<std::uint64_t> keys;
    keys.reserve(count);
    matching::FastHash hash;
    for (std::size_t index = 0; index < count; ++index)
    {
        std::uint64_t key = hash(static_cast<std::uint64_t>(index + 1));
        if (key == 0)
            key = 1;
        keys.push_back(key);
    }
    return keys;
}

Result benchmark_custom_hash(std::size_t operations,
                             const std::vector<std::uint64_t>& keys)
{
    matching::containers::HashMap<std::uint64_t, std::uint64_t, matching::FastHash>
        table(keys.size() * 2U, 0);
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        if (!table.insert(keys[index], static_cast<std::uint64_t>(index + 1)))
            throw std::runtime_error("Custom hash setup rejected a unique key");
    }

    std::uint64_t checksum = 0;
    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    for (std::size_t index = 0; index < operations; ++index)
    {
        const std::uint64_t* value = table.find(keys[index % keys.size()]);
        if (value == nullptr)
            throw std::runtime_error("Custom hash lost a key");
        checksum += *value;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

Result benchmark_unordered_hash(std::size_t operations,
                                const std::vector<std::uint64_t>& keys)
{
    std::unordered_map<std::uint64_t, std::uint64_t, matching::FastHash> table;
    table.max_load_factor(0.5F);
    table.reserve(keys.size());
    for (std::size_t index = 0; index < keys.size(); ++index)
        table.emplace(keys[index], static_cast<std::uint64_t>(index + 1));

    std::uint64_t checksum = 0;
    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    for (std::size_t index = 0; index < operations; ++index)
    {
        const auto found = table.find(keys[index % keys.size()]);
        if (found == table.end())
            throw std::runtime_error("std::unordered_map lost a key");
        checksum += found->second;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

Result benchmark_custom_hash_lifecycle(
    std::size_t operations,
    const std::vector<std::uint64_t>& keys)
{
    matching::containers::HashMap<std::uint64_t, std::uint64_t, matching::FastHash>
        table(keys.size() * 2U, 0);
    std::uint64_t checksum = 0;
    std::size_t completed = 0;

    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    while (completed < operations)
    {
        const std::size_t count = std::min(keys.size(), operations - completed);
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!table.insert(keys[index], static_cast<std::uint64_t>(index + 1)))
                throw std::runtime_error("Custom hash lifecycle insert failed");
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::uint64_t* value = table.find(keys[index]);
            if (value == nullptr)
                throw std::runtime_error("Custom hash lifecycle lookup failed");
            checksum += *value;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!table.erase(keys[index]))
                throw std::runtime_error("Custom hash lifecycle erase failed");
        }
        completed += count;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    if (!table.empty())
        throw std::runtime_error("Custom hash lifecycle did not drain");
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

Result benchmark_unordered_hash_lifecycle(
    std::size_t operations,
    const std::vector<std::uint64_t>& keys)
{
    std::unordered_map<std::uint64_t, std::uint64_t, matching::FastHash> table;
    table.max_load_factor(0.5F);
    table.reserve(keys.size());
    std::uint64_t checksum = 0;
    std::size_t completed = 0;

    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    while (completed < operations)
    {
        const std::size_t count = std::min(keys.size(), operations - completed);
        for (std::size_t index = 0; index < count; ++index)
            table.emplace(keys[index], static_cast<std::uint64_t>(index + 1));
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto found = table.find(keys[index]);
            if (found == table.end())
                throw std::runtime_error("std::unordered_map lifecycle lookup failed");
            checksum += found->second;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            if (table.erase(keys[index]) != 1)
                throw std::runtime_error("std::unordered_map lifecycle erase failed");
        }
        completed += count;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    if (!table.empty())
        throw std::runtime_error("std::unordered_map lifecycle did not drain");
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

struct AvlNode;

struct AvlCompare
{
    [[nodiscard]] bool operator()(const AvlNode& left,
                                  const AvlNode& right) const noexcept;
};

using AvlTree =
    matching::containers::IntrusiveAvlTree<AvlNode, AvlCompare>;

struct AvlNode : AvlTree::Node
{
    std::uint64_t key{0};
    std::uint64_t value{0};
};

bool AvlCompare::operator()(const AvlNode& left,
                            const AvlNode& right) const noexcept
{
    return left.key < right.key;
}

struct TreeFixture
{
    explicit TreeFixture(std::size_t count)
        : nodes(std::make_unique<AvlNode[]>(count)),
          queries(std::make_unique<AvlNode[]>(count)),
          keys(make_keys(count)),
          permutation(make_permutation(count))
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            nodes[index].key = keys[index];
            nodes[index].value = static_cast<std::uint64_t>(index + 1);
            queries[index].key = keys[index];
        }
        for (const std::size_t index : permutation)
        {
            const auto inserted = tree.insert(nodes[index]);
            if (!inserted.second)
                throw std::runtime_error("AVL setup rejected a unique key");
            ordered.emplace(nodes[index].key, nodes[index].value);
        }
    }

    ~TreeFixture() noexcept
    {
        for (const std::size_t index : permutation)
            static_cast<void>(tree.erase(nodes[index]));
    }

    std::unique_ptr<AvlNode[]> nodes;
    std::unique_ptr<AvlNode[]> queries;
    std::vector<std::uint64_t> keys;
    std::vector<std::size_t> permutation;
    AvlTree tree;
    std::map<std::uint64_t, std::uint64_t> ordered;
};

Result benchmark_avl_lookup(std::size_t operations, const TreeFixture& fixture)
{
    std::uint64_t checksum = 0;
    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    for (std::size_t index = 0; index < operations; ++index)
    {
        const AvlNode* found =
            fixture.tree.find(fixture.queries[index % fixture.keys.size()]);
        if (found == nullptr)
            throw std::runtime_error("AVL lookup lost a key");
        checksum += found->value;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

Result benchmark_map_lookup(std::size_t operations, const TreeFixture& fixture)
{
    std::uint64_t checksum = 0;
    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    for (std::size_t index = 0; index < operations; ++index)
    {
        const auto found = fixture.ordered.find(
            fixture.keys[index % fixture.keys.size()]);
        if (found == fixture.ordered.end())
            throw std::runtime_error("std::map lookup lost a key");
        checksum += found->second;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

Result benchmark_avl_lifecycle(std::size_t operations,
                               std::size_t element_count)
{
    std::unique_ptr<AvlNode[]> nodes = std::make_unique<AvlNode[]>(element_count);
    const std::vector<std::uint64_t> keys = make_keys(element_count);
    const std::vector<std::size_t> permutation = make_permutation(element_count);
    for (std::size_t index = 0; index < element_count; ++index)
    {
        nodes[index].key = keys[index];
        nodes[index].value = static_cast<std::uint64_t>(index + 1);
    }

    AvlTree tree;
    std::uint64_t checksum = 0;
    std::size_t completed = 0;
    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    while (completed < operations)
    {
        const std::size_t count = std::min(element_count, operations - completed);
        for (std::size_t position = 0; position < count; ++position)
        {
            const std::size_t index = permutation[position];
            const auto inserted = tree.insert(nodes[index]);
            if (!inserted.second)
                throw std::runtime_error("AVL lifecycle insert failed");
            checksum += nodes[index].value;
        }
        for (std::size_t position = 0; position < count; ++position)
        {
            const std::size_t index = permutation[position];
            static_cast<void>(tree.erase(nodes[index]));
        }
        completed += count;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    if (!tree.empty())
        throw std::runtime_error("AVL lifecycle did not drain");
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

Result benchmark_map_lifecycle(std::size_t operations,
                               std::size_t element_count)
{
    const std::vector<std::uint64_t> keys = make_keys(element_count);
    const std::vector<std::size_t> permutation = make_permutation(element_count);
    std::map<std::uint64_t, std::uint64_t> tree;
    std::uint64_t checksum = 0;
    std::size_t completed = 0;

    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    while (completed < operations)
    {
        const std::size_t count = std::min(element_count, operations - completed);
        for (std::size_t position = 0; position < count; ++position)
        {
            const std::size_t index = permutation[position];
            const auto inserted = tree.emplace(
                keys[index], static_cast<std::uint64_t>(index + 1));
            if (!inserted.second)
                throw std::runtime_error("std::map lifecycle insert failed");
            checksum += inserted.first->second;
        }
        for (std::size_t position = 0; position < count; ++position)
        {
            if (tree.erase(keys[permutation[position]]) != 1)
                throw std::runtime_error("std::map lifecycle erase failed");
        }
        completed += count;
    }
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    if (!tree.empty())
        throw std::runtime_error("std::map lifecycle did not drain");
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

#if defined(__clang__)
[[clang::noinline]]
#elif defined(__GNUC__)
[[gnu::noinline]]
#endif
[[nodiscard]] std::uint64_t read_cached_maximum(const AvlNode* cached,
                                                 std::uint64_t salt) noexcept
{
    return cached->key ^ salt;
}

#if defined(__clang__)
[[clang::noinline]]
#elif defined(__GNUC__)
[[gnu::noinline]]
#endif
[[nodiscard]] std::uint64_t read_tree_maximum(const AvlTree& tree,
                                               std::uint64_t salt) noexcept
{
    return tree.maximum()->key ^ salt;
}

Result benchmark_cached_maximum(std::size_t operations,
                                const TreeFixture& fixture)
{
    const AvlNode* cached = fixture.tree.maximum();
    if (cached == nullptr)
        throw std::runtime_error("Cannot cache an empty tree maximum");

    std::uint64_t checksum = 0;
    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    for (std::size_t index = 0; index < operations; ++index)
        checksum += read_cached_maximum(cached, static_cast<std::uint64_t>(index));
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

Result benchmark_traversed_maximum(std::size_t operations,
                                   const TreeFixture& fixture)
{
    std::uint64_t checksum = 0;
    support::compiler_barrier();
    const support::MonotonicClock::Tick start = support::MonotonicClock::now();
    for (std::size_t index = 0; index < operations; ++index)
        checksum += read_tree_maximum(fixture.tree,
                                      static_cast<std::uint64_t>(index));
    const support::MonotonicClock::Tick stop = support::MonotonicClock::now();
    support::compiler_barrier();
    return {support::MonotonicClock::elapsed_nanoseconds(start, stop), checksum};
}

void write_result(std::ofstream& output,
                  std::string_view benchmark,
                  std::string_view implementation,
                  std::size_t repetition,
                  std::size_t operations,
                  const Result& result)
{
    const long double ns_per_operation =
        static_cast<long double>(result.elapsed_ns) /
        static_cast<long double>(operations);
    const long double operations_per_second =
        (static_cast<long double>(operations) * 1'000'000'000.0L) /
        static_cast<long double>(result.elapsed_ns);
    output << benchmark << ',' << implementation << ',' << repetition << ','
           << operations << ',' << result.elapsed_ns << ',' << std::fixed
           << std::setprecision(3) << ns_per_operation << ','
           << operations_per_second << ',' << result.validation << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options))
    {
        std::cerr << "Usage: structure_benchmark --output <directory> "
                     "[--operations N] [--elements N] [--repetitions N]\n";
        return 2;
    }

    try
    {
        support::prepare_output_directory(options.output_directory);
        std::ofstream output = support::open_output(
            options.output_directory / "structure_summary.csv");
        output << "benchmark,implementation,repetition,operations,total_ns,"
                  "ns_per_operation,operations_per_second,validation\n";

        const std::vector<std::uint64_t> keys = make_keys(options.elements);
        TreeFixture tree_fixture(options.elements);

        for (std::size_t repetition = 1; repetition <= options.repetitions;
             ++repetition)
        {
            write_result(output,
                         "object_lifecycle",
                         "block_pool_64k",
                         repetition,
                         options.operations,
                         benchmark_pool(
                             options.operations, options.elements, 64U * 1024U));
            write_result(output,
                         "object_lifecycle",
                         "block_pool_256k",
                         repetition,
                         options.operations,
                         benchmark_pool(
                             options.operations, options.elements, 256U * 1024U));
            write_result(output,
                         "object_lifecycle",
                         "block_pool_1m",
                         repetition,
                         options.operations,
                         benchmark_pool(
                             options.operations, options.elements, 1024U * 1024U));
            write_result(output,
                         "object_lifecycle",
                         "new_delete",
                         repetition,
                         options.operations,
                         benchmark_new_delete(options.operations, options.elements));
            write_result(output,
                         "fifo_append_cancel",
                         "intrusive_list",
                         repetition,
                         options.operations,
                         benchmark_intrusive_list(options.operations,
                                                  options.elements));
            write_result(output,
                         "fifo_append_cancel",
                         "std_list",
                         repetition,
                         options.operations,
                         benchmark_std_list(options.operations, options.elements));

            const Result custom_hash =
                benchmark_custom_hash(options.operations, keys);
            const Result standard_hash =
                benchmark_unordered_hash(options.operations, keys);
            if (custom_hash.validation != standard_hash.validation)
                throw std::runtime_error("Hash lookup checksums disagree");
            write_result(output,
                         "active_id_lookup",
                         "linear_probe_hash",
                         repetition,
                         options.operations,
                         custom_hash);
            write_result(output,
                         "active_id_lookup",
                         "std_unordered_map",
                         repetition,
                         options.operations,
                         standard_hash);

            const Result custom_hash_lifecycle =
                benchmark_custom_hash_lifecycle(options.operations, keys);
            const Result standard_hash_lifecycle =
                benchmark_unordered_hash_lifecycle(options.operations, keys);
            if (custom_hash_lifecycle.validation !=
                standard_hash_lifecycle.validation)
            {
                throw std::runtime_error("Hash lifecycle checksums disagree");
            }
            write_result(output,
                         "active_id_insert_find_erase",
                         "linear_probe_hash",
                         repetition,
                         options.operations,
                         custom_hash_lifecycle);
            write_result(output,
                         "active_id_insert_find_erase",
                         "std_unordered_map",
                         repetition,
                         options.operations,
                         standard_hash_lifecycle);

            const Result avl = benchmark_avl_lookup(options.operations, tree_fixture);
            const Result map = benchmark_map_lookup(options.operations, tree_fixture);
            if (avl.validation != map.validation)
                throw std::runtime_error("Ordered lookup checksums disagree");
            write_result(output,
                         "price_level_lookup",
                         "intrusive_avl",
                         repetition,
                         options.operations,
                         avl);
            write_result(output,
                         "price_level_lookup",
                         "std_map",
                         repetition,
                         options.operations,
                         map);

            const Result avl_lifecycle =
                benchmark_avl_lifecycle(options.operations, options.elements);
            const Result map_lifecycle =
                benchmark_map_lifecycle(options.operations, options.elements);
            if (avl_lifecycle.validation != map_lifecycle.validation)
                throw std::runtime_error("Ordered lifecycle checksums disagree");
            write_result(output,
                         "price_level_insert_erase",
                         "intrusive_avl",
                         repetition,
                         options.operations,
                         avl_lifecycle);
            write_result(output,
                         "price_level_insert_erase",
                         "std_map",
                         repetition,
                         options.operations,
                         map_lifecycle);

            const Result cached =
                benchmark_cached_maximum(options.operations, tree_fixture);
            const Result traversed =
                benchmark_traversed_maximum(options.operations, tree_fixture);
            if (cached.validation != traversed.validation)
                throw std::runtime_error("Best-price checksums disagree");
            write_result(output,
                         "best_price_read",
                         "cached_pointer",
                         repetition,
                         options.operations,
                         cached);
            write_result(output,
                         "best_price_read",
                         "tree_traversal",
                         repetition,
                         options.operations,
                         traversed);

            std::cout << "Completed structure repetition " << repetition << '/'
                      << options.repetitions << '\n';
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
