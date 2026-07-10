#include <catch_amalgamated.hpp>

#include <dandb/btree/BTree.h>
#include <dandb/btree/BTreeInternalPage.h>
#include <dandb/btree/BTreeLeafPage.h>
#include <dandb/btree/BTreePage.h>
#include <dandb/core/Status.h>
#include <dandb/storage/PageId.h>
#include <dandb/storage/Pager.h>
#include <testutil/TempDir.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using dandb::btree::BTree;
using dandb::btree::BTreeInternalPage;
using dandb::btree::BTreeLeafPage;
using dandb::btree::BTREE_PAGE_ENTRY_ARRAY_OFFSET;
using dandb::btree::initialize_internal;
using dandb::btree::initialize_leaf;
using dandb::core::Status;
using dandb::core::StatusCode;
using dandb::storage::INVALID_PAGE_ID;
using dandb::storage::PageId;
using dandb::storage::Pager;
using dandb::testutil::TempDir;

namespace {

    constexpr std::uint16_t KEY_SIZE = 8;
    constexpr std::uint16_t VALUE_SIZE = 32;
    constexpr std::uint16_t SMALL_LEAF_VALUE_SIZE = 2008;

    std::array<std::byte, KEY_SIZE> make_key(std::uint8_t value) {
        std::array<std::byte, KEY_SIZE> key{};

        key[KEY_SIZE-1] = static_cast<std::byte>(value);
        return key;
    }

    std::array<std::byte, VALUE_SIZE> make_value(std::uint8_t value) {
        std::array<std::byte, VALUE_SIZE> stored_value{};

        stored_value.fill(static_cast<std::byte>(value));
        return stored_value;
    }

    std::vector<std::byte> make_value(std::uint16_t value_size, std::uint8_t value) {
        return std::vector<std::byte>(value_size, static_cast<std::byte>(value));
    }

    void require_corruption(const Status& status) {
        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::Corruption);
    }

    void overwrite_leaf_key(
        std::span<std::byte> bytes,
        std::uint16_t entry_index,
        std::uint16_t value_size,
        std::span<const std::byte> key
    ) {
        const auto entry_size = static_cast<std::size_t>(KEY_SIZE)+value_size;
        const auto key_offset = BTREE_PAGE_ENTRY_ARRAY_OFFSET+static_cast<std::size_t>(entry_index)*entry_size;

        for(std::size_t byte_index = 0; byte_index < key.size(); byte_index++) {
            bytes[key_offset+byte_index] = key[byte_index];
        }
    }

}

TEST_CASE("BTree validate accepts an empty tree", "[btree][validator]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 6);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    REQUIRE(tree_result.value().validate().ok());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree validate accepts a split tree", "[btree][validator]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 8);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 30, 10, 20 }) {
        auto key = make_key(key_value);
        auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    REQUIRE(tree.validate().ok());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree validate rejects a separator below its right subtree minimum", "[btree][validator]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 8);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 30, 10, 20 }) {
        auto key = make_key(key_value);
        auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    {
        auto root_page_handle_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_handle_result.ok());

        auto root_page_result = root_page_handle_result.value().mutable_page();
        REQUIRE(root_page_result.ok());

        auto root_page_view_result = BTreeInternalPage<std::byte>::open(root_page_result.value()->data());
        REQUIRE(root_page_view_result.ok());

        const auto invalid_separator = make_key(15);
        REQUIRE(root_page_view_result.value().set_key_at(0, invalid_separator).ok());
    }

    require_corruption(tree.validate());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree validate rejects unsorted leaf keys", "[btree][validator]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 6);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto key_10 = make_key(10);
    auto value_10 = make_value(10);
    REQUIRE(tree.insert(key_10, value_10).ok());

    auto key_20 = make_key(20);
    auto value_20 = make_value(20);
    REQUIRE(tree.insert(key_20, value_20).ok());

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto mutable_page_result = root_page_result.value().mutable_page();
        REQUIRE(mutable_page_result.ok());

        auto key_5 = make_key(5);
        overwrite_leaf_key(mutable_page_result.value()->data(), 1, VALUE_SIZE, key_5);
    }

    require_corruption(tree.validate());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree validate rejects broken leaf sibling links", "[btree][validator]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 8);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 30, 10, 20 }) {
        auto key = make_key(key_value);
        auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    PageId left_leaf_page_id;
    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto root_page_view_result = BTreeInternalPage<const std::byte>::open(root_page_result.value().page()->data());
        REQUIRE(root_page_view_result.ok());

        left_leaf_page_id = root_page_view_result.value().first_child_page_id();
    }

    {
        auto left_leaf_handle_result = pager.get_page(left_leaf_page_id);
        REQUIRE(left_leaf_handle_result.ok());

        auto mutable_left_leaf_result = left_leaf_handle_result.value().mutable_page();
        REQUIRE(mutable_left_leaf_result.ok());

        auto left_leaf_view_result = BTreeLeafPage<std::byte>::open(mutable_left_leaf_result.value()->data());
        REQUIRE(left_leaf_view_result.ok());

        left_leaf_view_result.value().set_next_leaf_page_id(INVALID_PAGE_ID);
    }

    require_corruption(tree.validate());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree validate rejects an internal page with no separator keys", "[btree][validator]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 6);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();

    PageId child_page_id;
    {
        auto child_page_handle_result = pager.new_page();
        REQUIRE(child_page_handle_result.ok());

        child_page_id = child_page_handle_result.value().page()->id();
        auto child_page_result = child_page_handle_result.value().mutable_page();
        REQUIRE(child_page_result.ok());

        REQUIRE(initialize_leaf(child_page_result.value()->data(), KEY_SIZE, VALUE_SIZE).ok());
        auto child_leaf_result = BTreeLeafPage<std::byte>::open(child_page_result.value()->data());
        REQUIRE(child_leaf_result.ok());
        child_leaf_result.value().set_parent_page_id(tree.root_page_id());
    }

    {
        auto root_page_handle_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_handle_result.ok());

        auto root_page_result = root_page_handle_result.value().mutable_page();
        REQUIRE(root_page_result.ok());

        REQUIRE(initialize_internal(root_page_result.value()->data(), KEY_SIZE, VALUE_SIZE).ok());
        auto root_internal_result = BTreeInternalPage<std::byte>::open(root_page_result.value()->data());
        REQUIRE(root_internal_result.ok());

        root_internal_result.value().set_root(true);
        root_internal_result.value().set_first_child_page_id(child_page_id);
    }

    require_corruption(tree.validate());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}
