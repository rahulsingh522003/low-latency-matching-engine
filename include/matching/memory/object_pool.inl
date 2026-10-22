namespace matching::memory {

template <typename T>
inline ObjectPool<T>::ObjectPool(std::size_t page_size, std::size_t maximum_pages) noexcept
    : page_size_(page_size),
      slots_per_page_(page_size / sizeof(Slot)),
      maximum_pages_(maximum_pages)
{
    assert((slots_per_page_ > 0) && "Object pool page must contain at least one slot");

    if (slots_per_page_ > 0)
    {
        const bool page_added = add_page();
        static_cast<void>(page_added);
    }
}

template <typename T>
inline ObjectPool<T>::~ObjectPool() noexcept
{
    assert((live_count_ == 0) && "Object pool destroyed with live objects");

    Page* page = pages_;
    while (page != nullptr)
    {
        Page* next_page = page->next;
        std::byte* slots = reinterpret_cast<std::byte*>(page->slots);

        for (std::size_t index = 0; index < slots_per_page_; ++index)
        {
            Slot* slot = reinterpret_cast<Slot*>(slots + (index * sizeof(Slot)));
            std::destroy_at(slot);
        }

        std::destroy_at(page);
        std::free(page);
        page = next_page;
    }
}

template <typename T>
template <typename... Args>
inline T* ObjectPool<T>::create(Args&&... args) noexcept
{
    static_assert(std::is_nothrow_constructible_v<T, Args&&...>);

    if ((free_head_ == nullptr) && !add_page())
        return nullptr;

    Slot* slot = free_head_;
    free_head_ = slot->next;

    T* object = std::construct_at(std::addressof(slot->object), std::forward<Args>(args)...);
    ++live_count_;
    return object;
}

template <typename T>
inline void ObjectPool<T>::release(T& object) noexcept
{
    Slot* slot = reinterpret_cast<Slot*>(std::addressof(object));

    assert(owns(slot) && "Released object does not belong to this pool");
    assert((live_count_ > 0) && "Object pool has no live objects to release");
    assert(!is_free(slot) && "Object has already been released");

    std::destroy_at(std::addressof(object));
    std::construct_at(std::addressof(slot->next), free_head_);
    free_head_ = slot;
    --live_count_;
}

template <typename T>
inline bool ObjectPool<T>::reserve(std::size_t minimum_capacity) noexcept
{
    while (capacity_ < minimum_capacity)
    {
        if (!add_page())
            return false;
    }

    return true;
}

template <typename T>
inline bool ObjectPool<T>::add_page() noexcept
{
    constexpr std::size_t alignment_padding = alignof(Slot) - 1;
    constexpr std::size_t maximum_size = std::numeric_limits<std::size_t>::max();

    if ((maximum_pages_ != 0) && (page_count_ >= maximum_pages_))
        return false;

    const std::size_t slots_size = slots_per_page_ * sizeof(Slot);
    if ((slots_per_page_ == 0) ||
        (capacity_ > (maximum_size - slots_per_page_)) ||
        (slots_size > (maximum_size - sizeof(Page) - alignment_padding)))
    {
        return false;
    }

    const std::size_t allocation_size = sizeof(Page) + alignment_padding + slots_size;
    void* allocation = std::malloc(allocation_size);
    if (allocation == nullptr)
        return false;

    void* unaligned_slots = static_cast<std::byte*>(allocation) + sizeof(Page);
    std::size_t available_space = allocation_size - sizeof(Page);
    void* aligned_slots = std::align(alignof(Slot), slots_size, unaligned_slots, available_space);
    if (aligned_slots == nullptr)
    {
        std::free(allocation);
        return false;
    }

    Page* page = std::construct_at(
        static_cast<Page*>(allocation), Page{pages_, static_cast<Slot*>(aligned_slots)});

    std::byte* slots = static_cast<std::byte*>(aligned_slots);
    for (std::size_t index = slots_per_page_; index > 0; --index)
    {
        Slot* slot = reinterpret_cast<Slot*>(slots + ((index - 1) * sizeof(Slot)));
        std::construct_at(slot);
        slot->next = free_head_;
        free_head_ = slot;
    }

    pages_ = page;
    ++page_count_;
    capacity_ += slots_per_page_;
    return true;
}

template <typename T>
inline bool ObjectPool<T>::owns(const Slot* slot) const noexcept
{
    const auto slot_address = reinterpret_cast<std::uintptr_t>(slot);
    const std::size_t slots_size = slots_per_page_ * sizeof(Slot);

    for (const Page* page = pages_; page != nullptr; page = page->next)
    {
        const auto first_address = reinterpret_cast<std::uintptr_t>(page->slots);
        const auto end_address = first_address + slots_size;

        if ((slot_address >= first_address) && (slot_address < end_address))
            return ((slot_address - first_address) % sizeof(Slot)) == 0;
    }

    return false;
}

template <typename T>
inline bool ObjectPool<T>::is_free(const Slot* slot) const noexcept
{
    for (const Slot* current = free_head_; current != nullptr; current = current->next)
    {
        if (current == slot)
            return true;
    }

    return false;
}

} // namespace matching::memory
