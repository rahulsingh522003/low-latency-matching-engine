#include "matching/memory/object_pool.h"

#include <catch_amalgamated.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

struct TrackedObject
{
    static inline std::size_t constructions{0};
    static inline std::size_t destructions{0};

    int value;

    explicit TrackedObject(int object_value) noexcept : value(object_value)
    {
        ++constructions;
    }

    ~TrackedObject() noexcept
    {
        ++destructions;
    }

    static void reset_counts() noexcept
    {
        constructions = 0;
        destructions = 0;
    }
};

struct alignas(128) OverAlignedObject
{
    std::uint64_t value;

    explicit OverAlignedObject(std::uint64_t object_value) noexcept : value(object_value) {}
};

} // namespace

TEST_CASE("Object pool constructs, destroys, and reuses slots", "[memory][object_pool]")
{
    TrackedObject::reset_counts();

    {
        matching::memory::ObjectPool<TrackedObject> pool(64, 1);
        TrackedObject* first = pool.create(11);

        REQUIRE(first != nullptr);
        REQUIRE(first->value == 11);
        REQUIRE(pool.live_count() == 1);
        REQUIRE(pool.free_count() == (pool.capacity() - 1));

        pool.release(*first);
        REQUIRE(pool.live_count() == 0);
        REQUIRE(pool.free_count() == pool.capacity());

        TrackedObject* reused = pool.create(22);
        REQUIRE(reused == first);
        REQUIRE(reused->value == 22);
        pool.release(*reused);
    }

    REQUIRE(TrackedObject::constructions == 2);
    REQUIRE(TrackedObject::destructions == 2);
}

TEST_CASE("Object pool grows by complete pages and preserves addresses", "[memory][object_pool]")
{
    TrackedObject::reset_counts();
    matching::memory::ObjectPool<TrackedObject> pool(64, 2);
    const std::size_t initial_capacity = pool.capacity();
    std::vector<TrackedObject*> objects;
    objects.reserve(initial_capacity + 1);

    for (std::size_t index = 0; index <= initial_capacity; ++index)
    {
        TrackedObject* object = pool.create(static_cast<int>(index));
        REQUIRE(object != nullptr);
        objects.push_back(object);
    }

    REQUIRE(pool.page_count() == 2);
    REQUIRE(pool.capacity() == (2 * initial_capacity));
    REQUIRE(objects.front()->value == 0);

    for (std::size_t index = 0; index < objects.size(); ++index)
        REQUIRE(objects[index]->value == static_cast<int>(index));

    for (TrackedObject* object : objects)
        pool.release(*object);

    REQUIRE(pool.live_count() == 0);
    REQUIRE(TrackedObject::constructions == objects.size());
    REQUIRE(TrackedObject::destructions == objects.size());
}

TEST_CASE("Object pool supports repeated construction in the same released slot",
          "[memory][object_pool]")
{
    constexpr std::size_t CYCLE_COUNT = 4'096;

    TrackedObject::reset_counts();
    matching::memory::ObjectPool<TrackedObject> pool(64, 1);
    TrackedObject* reused_slot = nullptr;

    for (std::size_t cycle = 0; cycle < CYCLE_COUNT; ++cycle)
    {
        TrackedObject* object = pool.create(static_cast<int>(cycle));
        REQUIRE(object != nullptr);

        if (reused_slot == nullptr)
            reused_slot = object;
        else
            REQUIRE(object == reused_slot);

        REQUIRE(object->value == static_cast<int>(cycle));
        pool.release(*object);
    }

    REQUIRE(pool.live_count() == 0);
    REQUIRE(TrackedObject::constructions == CYCLE_COUNT);
    REQUIRE(TrackedObject::destructions == CYCLE_COUNT);
}

TEST_CASE("Object pool reservation and maximum page count are explicit", "[memory][object_pool]")
{
    matching::memory::ObjectPool<TrackedObject> pool(64, 3);
    const std::size_t slots_per_page = pool.slots_per_page();

    REQUIRE(pool.reserve((2 * slots_per_page) - 1));
    REQUIRE(pool.page_count() == 2);
    REQUIRE(pool.capacity() == (2 * slots_per_page));

    REQUIRE_FALSE(pool.reserve((3 * slots_per_page) + 1));
    REQUIRE(pool.page_count() == 3);
    REQUIRE(pool.capacity() == (3 * slots_per_page));
}

TEST_CASE("Object pool reports exhaustion without falling back to the heap", "[memory][object_pool]")
{
    matching::memory::ObjectPool<TrackedObject> pool(64, 1);
    std::vector<TrackedObject*> objects;
    objects.reserve(pool.capacity());

    for (std::size_t index = 0; index < pool.capacity(); ++index)
    {
        TrackedObject* object = pool.create(static_cast<int>(index));
        REQUIRE(object != nullptr);
        objects.push_back(object);
    }

    REQUIRE(pool.create(999) == nullptr);

    for (TrackedObject* object : objects)
        pool.release(*object);
}

TEST_CASE("Object pool honors over-aligned object requirements", "[memory][object_pool]")
{
    matching::memory::ObjectPool<OverAlignedObject> pool(2 * sizeof(OverAlignedObject), 1);
    OverAlignedObject* first = pool.create(1U);
    OverAlignedObject* second = pool.create(2U);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(reinterpret_cast<std::uintptr_t>(first) % alignof(OverAlignedObject) == 0);
    REQUIRE(reinterpret_cast<std::uintptr_t>(second) % alignof(OverAlignedObject) == 0);
    REQUIRE(pool.create(3U) == nullptr);

    pool.release(*first);
    pool.release(*second);
}
