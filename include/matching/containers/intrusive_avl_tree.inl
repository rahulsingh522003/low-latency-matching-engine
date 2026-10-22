namespace matching::containers {

template <typename T, typename Compare>
inline IntrusiveAvlTree<T, Compare>::Node::~Node() noexcept
{
    assert((parent == nullptr) && "AVL node destroyed while linked to a parent");
    assert((left == nullptr) && "AVL node destroyed with a left child");
    assert((right == nullptr) && "AVL node destroyed with a right child");
    assert((balance == 0) && "AVL node destroyed with a non-zero balance");
}

template <typename T, typename Compare>
template <bool IsConst>
inline typename IntrusiveAvlTree<T, Compare>::template BasicIterator<IsConst>::reference
IntrusiveAvlTree<T, Compare>::BasicIterator<IsConst>::operator*() const noexcept
{
    assert((node_ != nullptr) && "Cannot dereference the AVL end iterator");
    return *node_;
}

template <typename T, typename Compare>
template <bool IsConst>
inline typename IntrusiveAvlTree<T, Compare>::template BasicIterator<IsConst>::pointer
IntrusiveAvlTree<T, Compare>::BasicIterator<IsConst>::operator->() const noexcept
{
    assert((node_ != nullptr) && "Cannot dereference the AVL end iterator");
    return node_;
}

template <typename T, typename Compare>
template <bool IsConst>
inline typename IntrusiveAvlTree<T, Compare>::template BasicIterator<IsConst>&
IntrusiveAvlTree<T, Compare>::BasicIterator<IsConst>::operator++() noexcept
{
    assert((tree_ != nullptr) && "AVL iterator is not associated with a tree");
    assert((node_ != nullptr) && "Cannot increment the AVL end iterator");
    node_ = tree_->next_node(node_);
    return *this;
}

template <typename T, typename Compare>
template <bool IsConst>
inline typename IntrusiveAvlTree<T, Compare>::template BasicIterator<IsConst>
IntrusiveAvlTree<T, Compare>::BasicIterator<IsConst>::operator++(int) noexcept
{
    BasicIterator previous(*this);
    ++(*this);
    return previous;
}

template <typename T, typename Compare>
template <bool IsConst>
inline typename IntrusiveAvlTree<T, Compare>::template BasicIterator<IsConst>&
IntrusiveAvlTree<T, Compare>::BasicIterator<IsConst>::operator--() noexcept
{
    assert((tree_ != nullptr) && "AVL iterator is not associated with a tree");
    node_ = (node_ == nullptr) ? tree_->maximum() : tree_->previous_node(node_);
    assert((node_ != nullptr) && "Cannot decrement the AVL begin iterator of an empty tree");
    return *this;
}

template <typename T, typename Compare>
template <bool IsConst>
inline typename IntrusiveAvlTree<T, Compare>::template BasicIterator<IsConst>
IntrusiveAvlTree<T, Compare>::BasicIterator<IsConst>::operator--(int) noexcept
{
    BasicIterator previous(*this);
    --(*this);
    return previous;
}

template <typename T, typename Compare>
inline IntrusiveAvlTree<T, Compare>::IntrusiveAvlTree(Compare compare) noexcept(
    std::is_nothrow_move_constructible_v<Compare>)
    : compare_(std::move(compare))
{
}

template <typename T, typename Compare>
inline IntrusiveAvlTree<T, Compare>::~IntrusiveAvlTree() noexcept
{
    assert((root_ == nullptr) && "Intrusive AVL tree destroyed while nodes are still linked");
    assert((size_ == 0) && "Intrusive AVL tree size is non-zero during destruction");
}

template <typename T, typename Compare>
inline bool IntrusiveAvlTree<T, Compare>::less(const T& left, const T& right) const noexcept
{
    return compare_(left, right);
}

template <typename T, typename Compare>
inline bool IntrusiveAvlTree<T, Compare>::belongs(const T* node) const noexcept
{
    if (node == nullptr)
        return false;

    const T* current = node;
    while (current->parent != nullptr)
        current = current->parent;
    return current == root_;
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::minimum_node(T* node) noexcept
{
    return const_cast<T*>(minimum_node(static_cast<const T*>(node)));
}

template <typename T, typename Compare>
inline const T* IntrusiveAvlTree<T, Compare>::minimum_node(const T* node) noexcept
{
    while ((node != nullptr) && (node->left != nullptr))
        node = node->left;
    return node;
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::maximum_node(T* node) noexcept
{
    return const_cast<T*>(maximum_node(static_cast<const T*>(node)));
}

template <typename T, typename Compare>
inline const T* IntrusiveAvlTree<T, Compare>::maximum_node(const T* node) noexcept
{
    while ((node != nullptr) && (node->right != nullptr))
        node = node->right;
    return node;
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::next_node(T* node) noexcept
{
    return const_cast<T*>(next_node(static_cast<const T*>(node)));
}

template <typename T, typename Compare>
inline const T* IntrusiveAvlTree<T, Compare>::next_node(const T* node) noexcept
{
    if ((node != nullptr) && (node->right != nullptr))
        return minimum_node(node->right);

    while ((node != nullptr) && (node->parent != nullptr) && (node == node->parent->right))
        node = node->parent;
    return (node == nullptr) ? nullptr : node->parent;
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::previous_node(T* node) noexcept
{
    return const_cast<T*>(previous_node(static_cast<const T*>(node)));
}

template <typename T, typename Compare>
inline const T* IntrusiveAvlTree<T, Compare>::previous_node(const T* node) noexcept
{
    if ((node != nullptr) && (node->left != nullptr))
        return maximum_node(node->left);

    while ((node != nullptr) && (node->parent != nullptr) && (node == node->parent->left))
        node = node->parent;
    return (node == nullptr) ? nullptr : node->parent;
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::find(const T& key) noexcept
{
    return const_cast<T*>(std::as_const(*this).find(key));
}

template <typename T, typename Compare>
inline const T* IntrusiveAvlTree<T, Compare>::find(const T& key) const noexcept
{
    const T* current = root_;
    while (current != nullptr)
    {
        if (less(key, *current))
            current = current->left;
        else if (less(*current, key))
            current = current->right;
        else
            return current;
    }
    return nullptr;
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::lower_bound(const T& key) noexcept
{
    return const_cast<T*>(std::as_const(*this).lower_bound(key));
}

template <typename T, typename Compare>
inline const T* IntrusiveAvlTree<T, Compare>::lower_bound(const T& key) const noexcept
{
    const T* result = nullptr;
    const T* current = root_;
    while (current != nullptr)
    {
        if (!less(*current, key))
        {
            result = current;
            current = current->left;
        }
        else
            current = current->right;
    }
    return result;
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::upper_bound(const T& key) noexcept
{
    return const_cast<T*>(std::as_const(*this).upper_bound(key));
}

template <typename T, typename Compare>
inline const T* IntrusiveAvlTree<T, Compare>::upper_bound(const T& key) const noexcept
{
    const T* result = nullptr;
    const T* current = root_;
    while (current != nullptr)
    {
        if (less(key, *current))
        {
            result = current;
            current = current->left;
        }
        else
            current = current->right;
    }
    return result;
}

template <typename T, typename Compare>
inline std::pair<T*, bool> IntrusiveAvlTree<T, Compare>::insert(T& node) noexcept
{
    T* parent = nullptr;
    T* current = root_;

    while (current != nullptr)
    {
        parent = current;
        if (less(node, *current))
            current = current->left;
        else if (less(*current, node))
            current = current->right;
        else
            return {current, false};
    }

    assert((node.parent == nullptr) && "Inserted AVL node already has a parent");
    assert((node.left == nullptr) && "Inserted AVL node already has a left child");
    assert((node.right == nullptr) && "Inserted AVL node already has a right child");
    assert((node.balance == 0) && "Inserted AVL node has a stale balance");

    node.parent = parent;
    if (parent == nullptr)
        root_ = &node;
    else if (less(node, *parent))
        parent->left = &node;
    else
        parent->right = &node;

    ++size_;
    rebalance_after_insert(&node);
    return {&node, true};
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::erase_key(const T& key) noexcept
{
    T* node = find(key);
    return (node == nullptr) ? nullptr : erase(*node);
}

template <typename T, typename Compare>
inline typename IntrusiveAvlTree<T, Compare>::iterator
IntrusiveAvlTree<T, Compare>::erase(iterator position) noexcept
{
    assert((position.tree_ == this) && "AVL iterator belongs to a different tree");
    assert((position.node_ != nullptr) && "Cannot erase the AVL end iterator");

    T* following = next_node(position.node_);
    static_cast<void>(erase(*position.node_));
    return iterator(this, following);
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::erase(T& node) noexcept
{
    assert(belongs(&node) && "Erased AVL node does not belong to this tree");

    T* rebalance_node = nullptr;
    bool left_subtree_shrank = false;

    if ((node.left == nullptr) || (node.right == nullptr))
    {
        T* child = (node.left != nullptr) ? node.left : node.right;
        rebalance_node = node.parent;
        left_subtree_shrank = (rebalance_node != nullptr) && (rebalance_node->left == &node);
        replace_node(&node, child);
    }
    else
    {
        T* successor = minimum_node(node.right);
        assert(successor != nullptr);

        if (successor->parent == &node)
        {
            rebalance_node = successor;
            left_subtree_shrank = false;
            replace_node(&node, successor);
        }
        else
        {
            T* successor_parent = successor->parent;
            assert((successor_parent != nullptr) && (successor_parent->left == successor));

            successor_parent->left = successor->right;
            if (successor->right != nullptr)
                successor->right->parent = successor_parent;

            successor->right = node.right;
            successor->right->parent = successor;
            rebalance_node = successor_parent;
            left_subtree_shrank = true;
            replace_node(&node, successor);
        }

        successor->left = node.left;
        successor->left->parent = successor;
        successor->balance = node.balance;
    }

    node.parent = nullptr;
    node.left = nullptr;
    node.right = nullptr;
    node.balance = 0;
    --size_;

    if (rebalance_node != nullptr)
        rebalance_after_erase(rebalance_node, left_subtree_shrank);

    return &node;
}

template <typename T, typename Compare>
inline void IntrusiveAvlTree<T, Compare>::replace_node(T* node, T* replacement) noexcept
{
    T* parent = node->parent;
    if (parent == nullptr)
        root_ = replacement;
    else if (parent->left == node)
        parent->left = replacement;
    else
    {
        assert(parent->right == node);
        parent->right = replacement;
    }

    if (replacement != nullptr)
        replacement->parent = parent;
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::rotate_left(T* node) noexcept
{
    T* pivot = node->right;
    assert(pivot != nullptr);

    T* parent = node->parent;
    node->right = pivot->left;
    if (node->right != nullptr)
        node->right->parent = node;

    pivot->left = node;
    node->parent = pivot;
    pivot->parent = parent;

    if (parent == nullptr)
        root_ = pivot;
    else if (parent->left == node)
        parent->left = pivot;
    else
    {
        assert(parent->right == node);
        parent->right = pivot;
    }

    return pivot;
}

template <typename T, typename Compare>
inline T* IntrusiveAvlTree<T, Compare>::rotate_right(T* node) noexcept
{
    T* pivot = node->left;
    assert(pivot != nullptr);

    T* parent = node->parent;
    node->left = pivot->right;
    if (node->left != nullptr)
        node->left->parent = node;

    pivot->right = node;
    node->parent = pivot;
    pivot->parent = parent;

    if (parent == nullptr)
        root_ = pivot;
    else if (parent->left == node)
        parent->left = pivot;
    else
    {
        assert(parent->right == node);
        parent->right = pivot;
    }

    return pivot;
}

template <typename T, typename Compare>
inline void IntrusiveAvlTree<T, Compare>::rebalance_after_insert(T* node) noexcept
{
    T* child = node;
    T* parent = node->parent;

    while (parent != nullptr)
    {
        parent->balance += (parent->left == child) ? -1 : 1;

        if (parent->balance == 0)
            return;

        if ((parent->balance == -1) || (parent->balance == 1))
        {
            child = parent;
            parent = parent->parent;
            continue;
        }

        if (parent->balance == -2)
        {
            T* left = parent->left;
            assert(left != nullptr);

            if (left->balance <= 0)
            {
                rotate_right(parent);
                parent->balance = 0;
                left->balance = 0;
            }
            else
            {
                T* pivot = left->right;
                assert(pivot != nullptr);
                const std::int8_t pivot_balance = pivot->balance;

                rotate_left(left);
                rotate_right(parent);

                parent->balance = (pivot_balance == -1) ? 1 : 0;
                left->balance = (pivot_balance == 1) ? -1 : 0;
                pivot->balance = 0;
            }
            return;
        }

        assert(parent->balance == 2);
        T* right = parent->right;
        assert(right != nullptr);

        if (right->balance >= 0)
        {
            rotate_left(parent);
            parent->balance = 0;
            right->balance = 0;
        }
        else
        {
            T* pivot = right->left;
            assert(pivot != nullptr);
            const std::int8_t pivot_balance = pivot->balance;

            rotate_right(right);
            rotate_left(parent);

            parent->balance = (pivot_balance == 1) ? -1 : 0;
            right->balance = (pivot_balance == -1) ? 1 : 0;
            pivot->balance = 0;
        }
        return;
    }
}

template <typename T, typename Compare>
inline void IntrusiveAvlTree<T, Compare>::rebalance_after_erase(
    T* node, bool left_subtree_shrank) noexcept
{
    while (node != nullptr)
    {
        node->balance += left_subtree_shrank ? 1 : -1;
        T* reduced_subtree = node;

        if ((node->balance == -1) || (node->balance == 1))
            return;

        if (node->balance == -2)
        {
            T* left = node->left;
            assert(left != nullptr);

            if (left->balance <= 0)
            {
                const std::int8_t left_balance = left->balance;
                reduced_subtree = rotate_right(node);

                if (left_balance == 0)
                {
                    node->balance = -1;
                    left->balance = 1;
                    return;
                }

                node->balance = 0;
                left->balance = 0;
            }
            else
            {
                T* pivot = left->right;
                assert(pivot != nullptr);
                const std::int8_t pivot_balance = pivot->balance;

                rotate_left(left);
                reduced_subtree = rotate_right(node);

                node->balance = (pivot_balance == -1) ? 1 : 0;
                left->balance = (pivot_balance == 1) ? -1 : 0;
                pivot->balance = 0;
            }
        }
        else if (node->balance == 2)
        {
            T* right = node->right;
            assert(right != nullptr);

            if (right->balance >= 0)
            {
                const std::int8_t right_balance = right->balance;
                reduced_subtree = rotate_left(node);

                if (right_balance == 0)
                {
                    node->balance = 1;
                    right->balance = -1;
                    return;
                }

                node->balance = 0;
                right->balance = 0;
            }
            else
            {
                T* pivot = right->left;
                assert(pivot != nullptr);
                const std::int8_t pivot_balance = pivot->balance;

                rotate_right(right);
                reduced_subtree = rotate_left(node);

                node->balance = (pivot_balance == 1) ? -1 : 0;
                right->balance = (pivot_balance == -1) ? 1 : 0;
                pivot->balance = 0;
            }
        }
        else
            assert(node->balance == 0);

        T* parent = reduced_subtree->parent;
        if (parent == nullptr)
            return;

        left_subtree_shrank = parent->left == reduced_subtree;
        node = parent;
    }
}

} // namespace matching::containers
