#ifndef MATCHING_CONTAINERS_INTRUSIVE_AVL_TREE_H
#define MATCHING_CONTAINERS_INTRUSIVE_AVL_TREE_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

namespace matching::containers {

template <typename T, typename Compare = std::less<T>>
class IntrusiveAvlTree
{
public:
    struct Node
    {
        T* parent{nullptr};
        T* left{nullptr};
        T* right{nullptr};
        std::int8_t balance{0};

        Node() noexcept = default;
        Node(const Node&) = delete;
        Node(Node&&) = delete;
        ~Node() noexcept;

        Node& operator=(const Node&) = delete;
        Node& operator=(Node&&) = delete;
    };

    /*
    BasicIterator<false>  --> mutable iterator (non-const)
    BasicIterator<true>   --> read-only iterator (const)
    */
    template <bool IsConst>
    class BasicIterator
    {
        template <bool>
        friend class BasicIterator;
        friend class IntrusiveAvlTree;

        using TreeType = std::conditional_t<IsConst, const IntrusiveAvlTree, IntrusiveAvlTree>;
        using NodeType = std::conditional_t<IsConst, const T, T>;

    public:
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = NodeType*;
        using reference = NodeType&;
        using iterator_category = std::bidirectional_iterator_tag;
        using iterator_concept = std::bidirectional_iterator_tag;

        BasicIterator() noexcept = default;

        template <bool OtherConst>
            requires(IsConst && !OtherConst)
        BasicIterator(const BasicIterator<OtherConst>& other) noexcept
            : tree_(other.tree_),
              node_(other.node_)
        {
        }

        [[nodiscard]] reference operator*() const noexcept;
        [[nodiscard]] pointer operator->() const noexcept;

        BasicIterator& operator++() noexcept;
        BasicIterator operator++(int) noexcept;
        BasicIterator& operator--() noexcept;
        BasicIterator operator--(int) noexcept;

        friend bool operator==(const BasicIterator&, const BasicIterator&) noexcept = default;

    private:
        BasicIterator(TreeType* tree, NodeType* node) noexcept : tree_(tree), node_(node) {}

        TreeType* tree_{nullptr};
        NodeType* node_{nullptr};
    };

    using iterator = BasicIterator<false>;
    using const_iterator = BasicIterator<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    IntrusiveAvlTree() noexcept(std::is_nothrow_default_constructible_v<Compare>) = default;
    explicit IntrusiveAvlTree(Compare compare) noexcept(std::is_nothrow_move_constructible_v<Compare>);
    IntrusiveAvlTree(const IntrusiveAvlTree&) = delete;
    IntrusiveAvlTree(IntrusiveAvlTree&&) = delete;
    ~IntrusiveAvlTree() noexcept;

    IntrusiveAvlTree& operator=(const IntrusiveAvlTree&) = delete;
    IntrusiveAvlTree& operator=(IntrusiveAvlTree&&) = delete;

    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] T* root() noexcept { return root_; }
    [[nodiscard]] const T* root() const noexcept { return root_; }
    [[nodiscard]] T* minimum() noexcept { return minimum_node(root_); }
    [[nodiscard]] const T* minimum() const noexcept { return minimum_node(root_); }
    [[nodiscard]] T* maximum() noexcept { return maximum_node(root_); }
    [[nodiscard]] const T* maximum() const noexcept { return maximum_node(root_); }

    [[nodiscard]] T* next(T& node) const noexcept { return next_node(&node); }
    [[nodiscard]] const T* next(const T& node) const noexcept { return next_node(&node); }
    [[nodiscard]] T* previous(T& node) const noexcept { return previous_node(&node); }
    [[nodiscard]] const T* previous(const T& node) const noexcept { return previous_node(&node); }

    [[nodiscard]] iterator begin() noexcept { return iterator(this, minimum()); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(this, minimum()); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return const_iterator(this, minimum()); }
    [[nodiscard]] iterator end() noexcept { return iterator(this, nullptr); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(this, nullptr); }
    [[nodiscard]] const_iterator cend() const noexcept { return const_iterator(this, nullptr); }

    [[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
    [[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

    [[nodiscard]] T* find(const T& key) noexcept;
    [[nodiscard]] const T* find(const T& key) const noexcept;
    [[nodiscard]] T* lower_bound(const T& key) noexcept;
    [[nodiscard]] const T* lower_bound(const T& key) const noexcept;
    [[nodiscard]] T* upper_bound(const T& key) noexcept;
    [[nodiscard]] const T* upper_bound(const T& key) const noexcept;

    [[nodiscard]] std::pair<T*, bool> insert(T& node) noexcept;
    [[nodiscard]] T* erase(T& node) noexcept;
    [[nodiscard]] T* erase_key(const T& key) noexcept;
    iterator erase(iterator position) noexcept;

private:
    [[no_unique_address]] Compare compare_{};
    std::size_t size_{0};
    T* root_{nullptr};

    [[nodiscard]] bool less(const T& left, const T& right) const noexcept;
    [[nodiscard]] bool belongs(const T* node) const noexcept;

    [[nodiscard]] static T* minimum_node(T* node) noexcept;
    [[nodiscard]] static const T* minimum_node(const T* node) noexcept;
    [[nodiscard]] static T* maximum_node(T* node) noexcept;
    [[nodiscard]] static const T* maximum_node(const T* node) noexcept;
    [[nodiscard]] static T* next_node(T* node) noexcept;
    [[nodiscard]] static const T* next_node(const T* node) noexcept;
    [[nodiscard]] static T* previous_node(T* node) noexcept;
    [[nodiscard]] static const T* previous_node(const T* node) noexcept;

    void replace_node(T* node, T* replacement) noexcept;
    T* rotate_left(T* node) noexcept;
    T* rotate_right(T* node) noexcept;
    void rebalance_after_insert(T* node) noexcept;
    void rebalance_after_erase(T* node, bool left_subtree_shrank) noexcept;
};

} // namespace matching::containers

#include "intrusive_avl_tree.inl"

#endif // MATCHING_CONTAINERS_INTRUSIVE_AVL_TREE_H
