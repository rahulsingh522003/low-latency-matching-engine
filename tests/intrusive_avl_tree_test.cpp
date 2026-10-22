#include "matching/containers/intrusive_avl_tree.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <set>
#include <type_traits>
#include <vector>

namespace {

struct TreeNode : matching::containers::IntrusiveAvlTree<TreeNode>::Node
{
    int key{0};

    TreeNode() noexcept = default;
    explicit TreeNode(int node_key) noexcept : key(node_key) {}

    friend bool operator<(const TreeNode& left, const TreeNode& right) noexcept
    {
        return left.key < right.key;
    }
};

using Tree = matching::containers::IntrusiveAvlTree<TreeNode>;

struct ValidationResult
{
    int height;
    std::size_t count;
};

ValidationResult validate_subtree(const TreeNode* node,
                                  const TreeNode* expected_parent,
                                  std::optional<int> lower_limit,
                                  std::optional<int> upper_limit)
{
    if (node == nullptr)
        return {0, 0};

    REQUIRE(node->parent == expected_parent);
    if (lower_limit.has_value())
        REQUIRE(node->key > *lower_limit);
    if (upper_limit.has_value())
        REQUIRE(node->key < *upper_limit);

    const ValidationResult left =
        validate_subtree(node->left, node, lower_limit, node->key);
    const ValidationResult right =
        validate_subtree(node->right, node, node->key, upper_limit);
    const int expected_balance = right.height - left.height;

    REQUIRE(expected_balance >= -1);
    REQUIRE(expected_balance <= 1);
    REQUIRE(static_cast<int>(node->balance) == expected_balance);

    return {1 + std::max(left.height, right.height), 1 + left.count + right.count};
}

void validate_tree(const Tree& tree, const std::set<int>& expected)
{
    const ValidationResult validation =
        validate_subtree(tree.root(), nullptr, std::nullopt, std::nullopt);
    REQUIRE(validation.count == tree.size());
    REQUIRE(tree.size() == expected.size());
    REQUIRE(tree.empty() == expected.empty());

    std::vector<int> forward;
    for (const TreeNode& node : tree)
        forward.push_back(node.key);
    REQUIRE(forward == std::vector<int>(expected.begin(), expected.end()));

    std::vector<int> reverse;
    for (auto iterator = tree.rbegin(); iterator != tree.rend(); ++iterator)
        reverse.push_back(iterator->key);
    REQUIRE(reverse == std::vector<int>(expected.rbegin(), expected.rend()));

    if (expected.empty())
    {
        REQUIRE(tree.minimum() == nullptr);
        REQUIRE(tree.maximum() == nullptr);
    }
    else
    {
        REQUIRE(tree.minimum()->key == *expected.begin());
        REQUIRE(tree.maximum()->key == *expected.rbegin());
    }
}

void erase_every_node(Tree& tree)
{
    auto iterator = tree.begin();
    while (iterator != tree.end())
        iterator = tree.erase(iterator);
}

void verify_insertion_rotation(const std::array<int, 3>& keys)
{
    TreeNode first(keys[0]);
    TreeNode second(keys[1]);
    TreeNode third(keys[2]);
    Tree tree;

    REQUIRE(tree.insert(first).second);
    REQUIRE(tree.insert(second).second);
    REQUIRE(tree.insert(third).second);
    REQUIRE(tree.root()->key == 20);
    validate_tree(tree, {10, 20, 30});

    erase_every_node(tree);
}

template <std::size_t NodeCount>
void verify_erase_rotation(const std::array<int, NodeCount>& keys,
                           int erased_key,
                           int expected_root_key)
{
    std::array<TreeNode, NodeCount> nodes;
    Tree tree;
    std::set<int> expected;
    TreeNode* erased_node = nullptr;

    for (std::size_t index = 0; index < NodeCount; ++index)
    {
        nodes[index].key = keys[index];
        REQUIRE(tree.insert(nodes[index]).second);
        expected.insert(keys[index]);

        if (keys[index] == erased_key)
            erased_node = &nodes[index];
    }

    REQUIRE(erased_node != nullptr);
    REQUIRE(tree.erase(*erased_node) == erased_node);
    expected.erase(erased_key);

    REQUIRE(tree.root() != nullptr);
    REQUIRE(tree.root()->key == expected_root_key);
    validate_tree(tree, expected);
    erase_every_node(tree);
}

} // namespace

static_assert(!std::is_copy_constructible_v<Tree::Node>);
static_assert(!std::is_copy_assignable_v<Tree::Node>);
static_assert(!std::is_move_constructible_v<Tree::Node>);
static_assert(!std::is_move_assignable_v<Tree::Node>);
static_assert(!std::is_copy_constructible_v<Tree>);
static_assert(!std::is_copy_assignable_v<Tree>);
static_assert(!std::is_move_constructible_v<Tree>);
static_assert(!std::is_move_assignable_v<Tree>);

TEST_CASE("Intrusive AVL performs all four insertion rotations", "[containers][avl]")
{
    SECTION("left-left")
    {
        verify_insertion_rotation({30, 20, 10});
    }

    SECTION("right-right")
    {
        verify_insertion_rotation({10, 20, 30});
    }

    SECTION("left-right")
    {
        verify_insertion_rotation({30, 10, 20});
    }

    SECTION("right-left")
    {
        verify_insertion_rotation({10, 30, 20});
    }
}

TEST_CASE("Intrusive AVL deletion performs single and double rotations", "[containers][avl]")
{
    SECTION("right-heavy node with balanced right child")
    {
        verify_erase_rotation(std::array{2, 1, 4, 3, 5}, 1, 4);
    }

    SECTION("right-right")
    {
        verify_erase_rotation(std::array{2, 1, 4, 5}, 1, 4);
    }

    SECTION("right-left")
    {
        verify_erase_rotation(std::array{2, 1, 5, 4}, 1, 4);
    }

    SECTION("left-heavy node with balanced left child")
    {
        verify_erase_rotation(std::array{4, 2, 5, 1, 3}, 5, 2);
    }

    SECTION("left-left")
    {
        verify_erase_rotation(std::array{4, 2, 5, 1}, 5, 2);
    }

    SECTION("left-right")
    {
        verify_erase_rotation(std::array{4, 1, 5, 2}, 5, 2);
    }
}

TEST_CASE("Intrusive AVL lookup, bounds, and traversal are ordered", "[containers][avl]")
{
    TreeNode ten(10);
    TreeNode twenty(20);
    TreeNode thirty(30);
    TreeNode forty(40);
    Tree tree;

    REQUIRE(tree.insert(thirty).second);
    REQUIRE(tree.insert(ten).second);
    REQUIRE(tree.insert(forty).second);
    REQUIRE(tree.insert(twenty).second);
    REQUIRE_FALSE(tree.insert(twenty).second);

    const TreeNode fifteen(15);
    const TreeNode exact_twenty(20);
    const TreeNode forty_one(41);

    REQUIRE(tree.find(exact_twenty) == &twenty);
    REQUIRE(tree.find(fifteen) == nullptr);
    REQUIRE(tree.lower_bound(fifteen) == &twenty);
    REQUIRE(tree.lower_bound(exact_twenty) == &twenty);
    REQUIRE(tree.upper_bound(exact_twenty) == &thirty);
    REQUIRE(tree.upper_bound(forty_one) == nullptr);
    REQUIRE(tree.next(twenty) == &thirty);
    REQUIRE(tree.previous(twenty) == &ten);

    validate_tree(tree, {10, 20, 30, 40});
    erase_every_node(tree);
}

TEST_CASE("Intrusive AVL deletion detaches the exact requested node", "[containers][avl]")
{
    std::array<TreeNode, 11> nodes;
    const std::array<int, 11> keys{50, 30, 70, 20, 40, 60, 80, 35, 45, 55, 65};
    Tree tree;
    std::set<int> expected;

    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        nodes[index].key = keys[index];
        REQUIRE(tree.insert(nodes[index]).second);
        expected.insert(keys[index]);
    }
    validate_tree(tree, expected);

    const std::array<std::size_t, 11> erase_order{0, 3, 5, 1, 9, 10, 4, 2, 6, 7, 8};
    for (const std::size_t index : erase_order)
    {
        TreeNode* erased = tree.erase(nodes[index]);
        REQUIRE(erased == &nodes[index]);
        REQUIRE(erased->parent == nullptr);
        REQUIRE(erased->left == nullptr);
        REQUIRE(erased->right == nullptr);
        REQUIRE(erased->balance == 0);
        expected.erase(erased->key);
        validate_tree(tree, expected);
    }
}

TEST_CASE("Intrusive AVL matches a standard ordered container under random updates",
          "[containers][avl]")
{
    constexpr std::size_t NODE_COUNT = 257;
    constexpr std::size_t OPERATION_COUNT = 20'000;

    std::array<TreeNode, NODE_COUNT> nodes;
    for (std::size_t index = 0; index < nodes.size(); ++index)
        nodes[index].key = static_cast<int>(index);

    Tree tree;
    std::set<int> expected;
    std::mt19937 generator(0x5A17B4C3U);
    std::uniform_int_distribution<int> key_distribution(0, static_cast<int>(NODE_COUNT - 1));

    for (std::size_t operation = 0; operation < OPERATION_COUNT; ++operation)
    {
        const int key = key_distribution(generator);
        const std::size_t index = static_cast<std::size_t>(key);
        const bool present = expected.contains(key);

        if ((generator() & 1U) == 0U)
        {
            const auto [stored, inserted] = tree.insert(nodes[index]);
            REQUIRE(stored == &nodes[index]);
            REQUIRE(inserted != present);
            if (inserted)
                expected.insert(key);
        }
        else if (present)
        {
            TreeNode* erased = nullptr;
            if ((generator() & 1U) == 0U)
                erased = tree.erase(nodes[index]);
            else
            {
                const TreeNode probe(key);
                erased = tree.erase_key(probe);
            }

            REQUIRE(erased == &nodes[index]);
            REQUIRE(erased->parent == nullptr);
            REQUIRE(erased->left == nullptr);
            REQUIRE(erased->right == nullptr);
            REQUIRE(erased->balance == 0);
            expected.erase(key);
        }
        else
        {
            const TreeNode probe(key);
            REQUIRE(tree.erase_key(probe) == nullptr);
        }

        if ((operation % 32U) == 0U)
            validate_tree(tree, expected);
    }

    validate_tree(tree, expected);
    erase_every_node(tree);
    validate_tree(tree, {});
}
