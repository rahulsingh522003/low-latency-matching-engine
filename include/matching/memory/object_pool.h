#ifndef MATCHING_MEMORY_OBJECT_POOL_H
#define MATCHING_MEMORY_OBJECT_POOL_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace matching::memory {

template <typename T>
class ObjectPool
{
    static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>);

public:
    static constexpr std::size_t DEFAULT_PAGE_SIZE = 64U * 1024U;

    explicit ObjectPool(std::size_t page_size = DEFAULT_PAGE_SIZE,
                        std::size_t maximum_pages = 0) noexcept;
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ~ObjectPool() noexcept;

    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

    template <typename... Args>
    [[nodiscard]] T* create(Args&&... args) noexcept;

    void release(T& object) noexcept;

    [[nodiscard]] bool reserve(std::size_t minimum_capacity) noexcept;

    [[nodiscard]] std::size_t page_size() const noexcept { return page_size_; }
    [[nodiscard]] std::size_t slots_per_page() const noexcept { return slots_per_page_; }
    [[nodiscard]] std::size_t page_count() const noexcept { return page_count_; }
    [[nodiscard]] std::size_t maximum_pages() const noexcept { return maximum_pages_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t live_count() const noexcept { return live_count_; }
    [[nodiscard]] std::size_t free_count() const noexcept { return capacity_ - live_count_; }

private:
    union Slot
    {
        T object;
        Slot* next;

        Slot() noexcept : next(nullptr) {}
        ~Slot() noexcept {}
    };

    struct Page
    {
        Page* next;
        Slot* slots;
    };

    std::size_t page_size_;
    std::size_t slots_per_page_;
    std::size_t maximum_pages_;
    Page* pages_{nullptr};
    Slot* free_head_{nullptr};
    std::size_t page_count_{0};
    std::size_t capacity_{0};
    std::size_t live_count_{0};

    [[nodiscard]] bool add_page() noexcept;
    [[nodiscard]] bool owns(const Slot* slot) const noexcept;
    [[nodiscard]] bool is_free(const Slot* slot) const noexcept;
};

} // namespace matching::memory

#include "object_pool.inl"

#endif // MATCHING_MEMORY_OBJECT_POOL_H
