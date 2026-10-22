#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace matching::containers {

template <typename Key, typename Value, typename Hash>
inline HashMap<Key, Value, Hash>::HashMap(std::size_t initial_bucket_count,
                                          Key empty_key,
                                          Hash hash)
    : hash_(std::move(hash)),
      empty_key_(empty_key),
      buckets_(normalize_bucket_count(initial_bucket_count), Bucket{empty_key, Value{}})
{
    assert_basic_invariants();
}

template <typename Key, typename Value, typename Hash>
inline Value* HashMap<Key, Value, Hash>::find(const Key& key) noexcept
{
    const std::size_t index = find_index(key);
    return (index == NOT_FOUND) ? nullptr : &buckets_[index].value;
}

template <typename Key, typename Value, typename Hash>
inline const Value* HashMap<Key, Value, Hash>::find(const Key& key) const noexcept
{
    const std::size_t index = find_index(key);
    return (index == NOT_FOUND) ? nullptr : &buckets_[index].value;
}

template <typename Key, typename Value, typename Hash>
inline bool HashMap<Key, Value, Hash>::contains(const Key& key) const noexcept
{
    return find_index(key) != NOT_FOUND;
}

template <typename Key, typename Value, typename Hash>
inline bool HashMap<Key, Value, Hash>::insert(const Key& key, const Value& value)
{
    if (key == empty_key_)
        return false;

    std::size_t index = find_slot(key);
    if (!is_empty(buckets_[index]))
        return false;

    if ((size_ + 1) > (buckets_.size() / 2))
    {
        reserve(size_ + 1);
        index = find_slot(key);
        assert(is_empty(buckets_[index]) && "Rehashed table must contain an insertion slot");
    }

    buckets_[index] = Bucket{key, value};
    ++size_;
    assert_basic_invariants();
    return true;
}

template <typename Key, typename Value, typename Hash>
inline bool HashMap<Key, Value, Hash>::erase(const Key& key) noexcept
{
    const std::size_t erased_index = find_index(key);
    if (erased_index == NOT_FOUND)
        return false;

    std::size_t hole = erased_index;
    for (std::size_t current = next_index(hole);; current = next_index(current))
    {
        if (is_empty(buckets_[current]))
        {
            buckets_[hole] = Bucket{empty_key_, Value{}};
            --size_;
            assert_basic_invariants();
            return true;
        }

        const std::size_t ideal = key_to_index(buckets_[current].key);
        if (probe_distance(ideal, hole) < probe_distance(ideal, current))
        {
            buckets_[hole] = buckets_[current];
            hole = current;
        }
    }
}

template <typename Key, typename Value, typename Hash>
template <typename Function>
inline void HashMap<Key, Value, Hash>::for_each(Function&& function) const
    noexcept(std::is_nothrow_invocable_v<Function&, const Key&, const Value&>)
{
    for (const Bucket& bucket : buckets_)
    {
        if (!is_empty(bucket))
            function(bucket.key, bucket.value);
    }
}

template <typename Key, typename Value, typename Hash>
inline void HashMap<Key, Value, Hash>::clear() noexcept
{
    for (Bucket& bucket : buckets_)
    {
        if (!is_empty(bucket))
            bucket = Bucket{empty_key_, Value{}};
    }

    size_ = 0;
    assert_basic_invariants();
}

template <typename Key, typename Value, typename Hash>
inline void HashMap<Key, Value, Hash>::reserve(std::size_t minimum_entry_count)
{
    if (minimum_entry_count <= (buckets_.size() / 2))
        return;

    if (minimum_entry_count > (MAXIMUM_BUCKET_COUNT / 2))
        throw std::length_error("HashMap entry capacity is too large");

    rehash(minimum_entry_count * 2);
}

template <typename Key, typename Value, typename Hash>
inline void HashMap<Key, Value, Hash>::rehash(std::size_t minimum_bucket_count)
{
    if (size_ > (MAXIMUM_BUCKET_COUNT / 2))
        throw std::length_error("HashMap size cannot satisfy the maximum load factor");

    const std::size_t required_for_entries =
        (size_ == 0) ? MINIMUM_BUCKET_COUNT : (size_ * 2);
    const std::size_t requested = std::max(minimum_bucket_count, required_for_entries);
    const std::size_t new_bucket_count = normalize_bucket_count(requested);

    if (new_bucket_count == buckets_.size())
        return;

    std::vector<Bucket> replacement(new_bucket_count, Bucket{empty_key_, Value{}});
    const std::size_t replacement_mask = new_bucket_count - 1;

    for (const Bucket& bucket : buckets_)
    {
        if (is_empty(bucket))
            continue;

        std::size_t index = hash_(bucket.key) & replacement_mask;
        while (replacement[index].key != empty_key_)
            index = (index + 1) & replacement_mask;

        replacement[index] = bucket;
    }

    buckets_.swap(replacement);
    assert_basic_invariants();
}

template <typename Key, typename Value, typename Hash>
inline std::size_t HashMap<Key, Value, Hash>::normalize_bucket_count(
    std::size_t requested)
{
    std::size_t result = MINIMUM_BUCKET_COUNT;
    while (result < requested)
    {
        if (result > (MAXIMUM_BUCKET_COUNT / 2))
            throw std::length_error("HashMap bucket capacity is too large");

        result *= 2;
    }

    return result;
}

template <typename Key, typename Value, typename Hash>
inline bool HashMap<Key, Value, Hash>::is_empty(const Bucket& bucket) const noexcept
{
    return bucket.key == empty_key_;
}

template <typename Key, typename Value, typename Hash>
inline std::size_t HashMap<Key, Value, Hash>::key_to_index(const Key& key) const noexcept
{
    return hash_(key) & (buckets_.size() - 1);
}

template <typename Key, typename Value, typename Hash>
inline std::size_t HashMap<Key, Value, Hash>::next_index(std::size_t index) const noexcept
{
    return (index + 1) & (buckets_.size() - 1);
}

template <typename Key, typename Value, typename Hash>
inline std::size_t HashMap<Key, Value, Hash>::probe_distance(
    std::size_t ideal,
    std::size_t position) const noexcept
{
    return (position - ideal) & (buckets_.size() - 1);
}

template <typename Key, typename Value, typename Hash>
inline std::size_t HashMap<Key, Value, Hash>::find_index(const Key& key) const noexcept
{
    if (key == empty_key_)
        return NOT_FOUND;

    for (std::size_t index = key_to_index(key);; index = next_index(index))
    {
        const Bucket& bucket = buckets_[index];
        if (is_empty(bucket))
            return NOT_FOUND;
        if (bucket.key == key)
            return index;
    }
}

template <typename Key, typename Value, typename Hash>
inline std::size_t HashMap<Key, Value, Hash>::find_slot(const Key& key) const noexcept
{
    for (std::size_t index = key_to_index(key);; index = next_index(index))
    {
        const Bucket& bucket = buckets_[index];
        if (is_empty(bucket) || (bucket.key == key))
            return index;
    }
}

template <typename Key, typename Value, typename Hash>
inline void HashMap<Key, Value, Hash>::assert_basic_invariants() const noexcept
{
    assert(!buckets_.empty() && "HashMap must always have buckets");
    assert(((buckets_.size() & (buckets_.size() - 1)) == 0) &&
           "HashMap bucket count must be a power of two");
    assert((size_ <= (buckets_.size() / 2)) &&
           "HashMap load factor must not exceed one half");
}

} // namespace matching::containers
