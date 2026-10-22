#include <cassert>

namespace matching::containers {

template <typename T>
inline IntrusiveList<T>::~IntrusiveList() noexcept
{
    assert(((front_ == nullptr) && (back_ == nullptr) && (size_ == 0)) && "Intrusive list front must be empty before destruction");
}

template <typename T>
inline void IntrusiveList<T>::push_back(T& item) noexcept
{
    assert(((size_ == 0) == (front_ == nullptr)) && "Intrusive list size and front disagree");
    assert(((front_ == nullptr) == (back_ == nullptr)) && "Intrusive list endpoints disagree");
    assert((item.next == nullptr) && "Inserted node must not have a next link");
    assert((item.previous == nullptr) && "Inserted node must not have a previous link");
    assert((front_ != &item) && "Node is already the list front");
    assert((back_ != &item) && "Node is already the list back");

    item.previous = back_;
    item.next = nullptr;

    if (back_ != nullptr)
        back_->next = &item;
    else
        front_ = &item;

    back_ = &item;
    ++size_;
}

template <typename T>
inline void IntrusiveList<T>::unlink(T& item) noexcept
{
    assert((size_ > 0) && "Cannot unlink a node from an empty intrusive list");
    assert(((size_ == 0) == (front_ == nullptr)) && "Intrusive list size and front disagree");
    assert(((front_ == nullptr) == (back_ == nullptr)) && "Intrusive list endpoints disagree");

    if (item.previous != nullptr)
    {
        assert((item.previous->next == &item) && "Previous node does not link to this node");
        item.previous->next = item.next;
    }
    else
    {
        assert((front_ == &item) && "Unlinked node does not belong to this list");
        front_ = item.next;
    }

    if (item.next != nullptr)
    {
        assert((item.next->previous == &item) && "Next node does not link to this node");
        item.next->previous = item.previous;
    }
    else
    {
        assert((back_ == &item) && "Unlinked node does not belong to this list");
        back_ = item.previous;
    }

    item.next = nullptr;
    item.previous = nullptr;
    --size_;

    assert(((size_ == 0) == (front_ == nullptr)) && "Intrusive list size and front disagree");
    assert(((front_ == nullptr) == (back_ == nullptr)) && "Intrusive list endpoints disagree");
}

} // namespace matching::containers
