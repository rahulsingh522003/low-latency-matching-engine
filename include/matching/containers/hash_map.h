#ifndef MATCHING_CONTAINERS_HASH_MAP_H
#define MATCHING_CONTAINERS_HASH_MAP_H

#include <cstddef>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace matching::containers {

template <typename Key,
          typename Value,
          typename Hash = std::hash<Key>>
class HashMap
{
    static_assert(std::is_trivially_copyable_v<Key>);
    static_assert(std::is_trivially_copyable_v<Value>);
    static_assert(std::is_default_constructible_v<Value>);
    static_assert(std::is_nothrow_invocable_r_v<std::size_t, const Hash&, const Key&>);
    static_assert(noexcept(std::declval<const Key&>() == std::declval<const Key&>()));

public:
    explicit HashMap(std::size_t initial_bucket_count,
                     Key empty_key,
                     Hash hash = Hash{});
    HashMap(const HashMap&) = delete;
    HashMap(HashMap&&) = delete;
    ~HashMap() = default;

    HashMap& operator=(const HashMap&) = delete;
    HashMap& operator=(HashMap&&) = delete;

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t bucket_count() const noexcept { return buckets_.size(); }
    [[nodiscard]] const Key& empty_key() const noexcept { return empty_key_; }

    [[nodiscard]] Value* find(const Key& key) noexcept;
    [[nodiscard]] const Value* find(const Key& key) const noexcept;
    [[nodiscard]] bool contains(const Key& key) const noexcept;

    [[nodiscard]] bool insert(const Key& key, const Value& value);
    [[nodiscard]] bool erase(const Key& key) noexcept;

    template <typename Function>
    void for_each(Function&& function) const
        noexcept(std::is_nothrow_invocable_v<Function&, const Key&, const Value&>);

    void clear() noexcept;
    void reserve(std::size_t minimum_entry_count);
    void rehash(std::size_t minimum_bucket_count);

private:
    struct Bucket
    {
        Key key;
        Value value;
    };

    static constexpr std::size_t MINIMUM_BUCKET_COUNT = 2;
    static constexpr std::size_t NOT_FOUND = std::numeric_limits<std::size_t>::max();
    static constexpr std::size_t MAXIMUM_BUCKET_COUNT =
        std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);

    Hash hash_;
    Key empty_key_;
    std::vector<Bucket> buckets_;
    std::size_t size_{0};

    [[nodiscard]] static std::size_t normalize_bucket_count(std::size_t requested);
    [[nodiscard]] bool is_empty(const Bucket& bucket) const noexcept;
    [[nodiscard]] std::size_t key_to_index(const Key& key) const noexcept;
    [[nodiscard]] std::size_t next_index(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t probe_distance(std::size_t ideal,
                                             std::size_t position) const noexcept;
    [[nodiscard]] std::size_t find_index(const Key& key) const noexcept;
    [[nodiscard]] std::size_t find_slot(const Key& key) const noexcept;
    void assert_basic_invariants() const noexcept;
};

} // namespace matching::containers

#include "hash_map.inl"

#endif // MATCHING_CONTAINERS_HASH_MAP_H
