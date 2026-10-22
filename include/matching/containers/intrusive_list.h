#ifndef MATCHING_CONTAINERS_INTRUSIVE_LIST_H
#define MATCHING_CONTAINERS_INTRUSIVE_LIST_H

#include <cstddef>

namespace matching::containers {

template <typename T>
class IntrusiveList
{
public:
    struct Node
    {
        T* next{nullptr};
        T* previous{nullptr};

        Node() noexcept = default;
        Node(const Node&) = delete;
        Node(Node&&) = delete;
        Node& operator=(const Node&) = delete;
        Node& operator=(Node&&) = delete;
    };

    IntrusiveList() noexcept = default;
    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList(IntrusiveList&&) = delete;
    ~IntrusiveList() noexcept;

    IntrusiveList& operator=(const IntrusiveList&) = delete;
    IntrusiveList& operator=(IntrusiveList&&) = delete;

    [[nodiscard]] bool empty() const noexcept { return front_ == nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] T* front() noexcept { return front_; }
    [[nodiscard]] const T* front() const noexcept { return front_; }
    [[nodiscard]] T* back() noexcept { return back_; }
    [[nodiscard]] const T* back() const noexcept { return back_; }

    void push_back(T& item) noexcept;
    void unlink(T& item) noexcept;

private:
    std::size_t size_{0};
    T* front_{nullptr};
    T* back_{nullptr};
};

} // namespace matching::containers

#include "intrusive_list.inl"

#endif // MATCHING_CONTAINERS_INTRUSIVE_LIST_H
