#include "matching/containers/hash_map.h"
#include "matching/fast_hash.h"
#include "matching/order.h"

#include <catch_amalgamated.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <type_traits>
#include <unordered_map>

namespace {

struct ConstantHash
{
    std::size_t result;

    [[nodiscard]] std::size_t operator()(std::uint64_t) const noexcept { return result; }
};

using IntegerMap =
    matching::containers::HashMap<std::uint64_t, std::uint64_t, matching::FastHash>;
using DefaultIntegerMap = matching::containers::HashMap<std::uint64_t, std::uint64_t>;
using CollisionMap = matching::containers::HashMap<std::uint64_t, std::uint64_t, ConstantHash>;
using ActiveOrderMap = matching::containers::HashMap<matching::OrderId,
                                                     matching::OrderNode*,
                                                     matching::FastHash>;

} // namespace

static_assert(!std::is_copy_constructible_v<IntegerMap>);
static_assert(!std::is_copy_assignable_v<IntegerMap>);
static_assert(!std::is_move_constructible_v<IntegerMap>);
static_assert(!std::is_move_assignable_v<IntegerMap>);
static_assert(sizeof(DefaultIntegerMap) > 0);

TEST_CASE("Hash map reserves zero as its empty key", "[containers][hash_map]")
{
    IntegerMap map(3, 0);

    REQUIRE(map.empty());
    REQUIRE(map.size() == 0);
    REQUIRE(map.bucket_count() == 4);
    REQUIRE(map.empty_key() == 0);
    REQUIRE(map.find(0) == nullptr);
    REQUIRE_FALSE(map.contains(0));
    REQUIRE_FALSE(map.insert(0, 99));
    REQUIRE_FALSE(map.erase(0));
}

TEST_CASE("Hash map rejects a duplicate without replacing its value",
          "[containers][hash_map]")
{
    ActiveOrderMap orders(8, 0);
    matching::OrderNode first(
        matching::Order::BuyLimit(42, 1, 10'000, 25, matching::OrderTimeInForce::GTC));
    matching::OrderNode duplicate(
        matching::Order::SellLimit(42, 2, 10'100, 30, matching::OrderTimeInForce::GTC));

    REQUIRE(orders.insert(first.id, &first));
    REQUIRE_FALSE(orders.insert(duplicate.id, &duplicate));
    REQUIRE(orders.size() == 1);

    matching::OrderNode** stored = orders.find(first.id);
    REQUIRE(stored != nullptr);
    REQUIRE(*stored == &first);

    const ActiveOrderMap& const_orders = orders;
    matching::OrderNode* const* const_stored = const_orders.find(first.id);
    REQUIRE(const_stored != nullptr);
    REQUIRE(*const_stored == &first);
}

TEST_CASE("Hash map preserves collision chains during backward-shift deletion",
          "[containers][hash_map]")
{
    CollisionMap map(16, 0, ConstantHash{3});

    for (std::uint64_t key = 1; key <= 6; ++key)
        REQUIRE(map.insert(key, key * 10));

    REQUIRE(map.erase(3));
    REQUIRE_FALSE(map.contains(3));
    REQUIRE(map.erase(1));
    REQUIRE_FALSE(map.contains(1));
    REQUIRE(map.erase(6));
    REQUIRE_FALSE(map.contains(6));

    for (const std::uint64_t key : {2U, 4U, 5U})
    {
        const std::uint64_t* value = map.find(key);
        REQUIRE(value != nullptr);
        REQUIRE(*value == key * 10);
    }

    REQUIRE_FALSE(map.erase(99));
    REQUIRE(map.size() == 3);
}

TEST_CASE("Hash map collision chains remain valid across the array boundary",
          "[containers][hash_map]")
{
    CollisionMap map(8, 0, ConstantHash{7});

    REQUIRE(map.insert(11, 110));
    REQUIRE(map.insert(12, 120));
    REQUIRE(map.insert(13, 130));
    REQUIRE(map.insert(14, 140));

    REQUIRE(map.erase(12));
    REQUIRE(map.find(11) != nullptr);
    REQUIRE(map.find(13) != nullptr);
    REQUIRE(map.find(14) != nullptr);
    REQUIRE(*map.find(13) == 130);
    REQUIRE(*map.find(14) == 140);
}

TEST_CASE("Hash map grows before exceeding one-half load", "[containers][hash_map]")
{
    IntegerMap map(4, 0);

    REQUIRE(map.insert(1, 10));
    REQUIRE(map.insert(2, 20));
    REQUIRE(map.bucket_count() == 4);

    REQUIRE(map.insert(3, 30));
    REQUIRE(map.bucket_count() == 8);
    REQUIRE(map.size() == 3);

    map.reserve(100);
    REQUIRE(map.bucket_count() == 256);
    REQUIRE(*map.find(1) == 10);
    REQUIRE(*map.find(2) == 20);
    REQUIRE(*map.find(3) == 30);

    map.rehash(2);
    REQUIRE(map.bucket_count() == 8);
    REQUIRE(*map.find(1) == 10);
    REQUIRE(*map.find(2) == 20);
    REQUIRE(*map.find(3) == 30);
}

TEST_CASE("Hash map clear removes every logical entry", "[containers][hash_map]")
{
    IntegerMap map(16, 0);

    REQUIRE(map.insert(1, 10));
    REQUIRE(map.insert(2, 20));
    REQUIRE(map.insert(3, 30));

    std::uint64_t key_sum = 0;
    std::uint64_t value_sum = 0;
    map.for_each([&key_sum, &value_sum](std::uint64_t key, std::uint64_t value) noexcept {
        key_sum += key;
        value_sum += value;
    });
    REQUIRE(key_sum == 6);
    REQUIRE(value_sum == 60);

    map.clear();

    REQUIRE(map.empty());
    REQUIRE(map.size() == 0);
    REQUIRE(map.find(1) == nullptr);
    REQUIRE(map.find(2) == nullptr);
    REQUIRE(map.find(3) == nullptr);
    REQUIRE(map.insert(4, 40));
    REQUIRE(*map.find(4) == 40);
}

TEST_CASE("Hash map agrees with std unordered map under random operations",
          "[containers][hash_map]")
{
    constexpr std::size_t OPERATION_COUNT = 50'000;
    constexpr std::uint64_t KEY_COUNT = 1'024;

    IntegerMap map(2, 0);
    std::unordered_map<std::uint64_t, std::uint64_t, matching::FastHash> expected;
    std::mt19937_64 generator(0xBADC0FFEEULL);

    for (std::size_t operation = 0; operation < OPERATION_COUNT; ++operation)
    {
        const std::uint64_t key = 1 + (generator() % KEY_COUNT);
        const std::uint64_t value = generator();

        switch (generator() % 3)
        {
            case 0:
            {
                const bool inserted = map.insert(key, value);
                const bool expected_inserted = expected.emplace(key, value).second;
                REQUIRE(inserted == expected_inserted);
                break;
            }
            case 1:
            {
                const bool erased = map.erase(key);
                const bool expected_erased = expected.erase(key) != 0;
                REQUIRE(erased == expected_erased);
                break;
            }
            default:
            {
                const std::uint64_t* found = map.find(key);
                const auto expected_found = expected.find(key);
                REQUIRE((found != nullptr) == (expected_found != expected.end()));
                if (found != nullptr)
                    REQUIRE(*found == expected_found->second);
                break;
            }
        }

        REQUIRE(map.size() == expected.size());

        if ((operation % 257) == 0)
        {
            for (const auto& [expected_key, expected_value] : expected)
            {
                const std::uint64_t* found = map.find(expected_key);
                REQUIRE(found != nullptr);
                REQUIRE(*found == expected_value);
            }
        }
    }
}
