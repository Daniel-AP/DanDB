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
#include <vector>

using dandb::btree::BTree;
using dandb::btree::BTreeInternalPage;
using dandb::btree::BTreeLeafPage;
using dandb::btree::initialize_internal;
using dandb::btree::initialize_leaf;
using dandb::core::StatusCode;
using dandb::storage::PageId;
using dandb::storage::Pager;
using dandb::testutil::TempDir;

namespace {

    constexpr std::uint16_t KEY_SIZE = 8;
    constexpr std::uint16_t VALUE_SIZE = 32;

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

    PageId create_leaf_with_entry(Pager& pager, std::uint8_t key_value, std::uint8_t stored_value) {

        auto page_handle_result = pager.new_page();
        REQUIRE(page_handle_result.ok());

        auto& page_handle = page_handle_result.value();
        const auto page_id = page_handle.page()->id();

        auto mutable_page_result = page_handle.mutable_page();
        REQUIRE(mutable_page_result.ok());

        auto& page = *mutable_page_result.value();
        REQUIRE(initialize_leaf(page.data(), KEY_SIZE, VALUE_SIZE).ok());

        auto leaf_page_result = BTreeLeafPage<std::byte>::open(page.data());
        REQUIRE(leaf_page_result.ok());

        auto key = make_key(key_value);
        auto value = make_value(stored_value);
        REQUIRE(leaf_page_result.value().insert_entry(0, key, value).ok());

        return page_id;
    }

    void insert_root_leaf_entry(BTree& tree, Pager& pager, std::uint8_t key_value, std::uint8_t stored_value) {

        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto mutable_page_result = root_page_result.value().mutable_page();
        REQUIRE(mutable_page_result.ok());

        auto leaf_page_result = BTreeLeafPage<std::byte>::open(mutable_page_result.value()->data());
        REQUIRE(leaf_page_result.ok());

        auto& leaf_page = leaf_page_result.value();
        auto key = make_key(key_value);
        auto value = make_value(stored_value);

        auto position_result = leaf_page.find_insertion_position(key);
        REQUIRE(position_result.ok());
        REQUIRE(leaf_page.insert_entry(position_result.value(), key, value).ok());
    }

    void require_found_value(const BTree& tree, std::uint8_t key_value, std::uint8_t stored_value) {

        auto key = make_key(key_value);
        auto found_value = tree.find(key);
        REQUIRE(found_value.ok());

        const auto expected_value = make_value(stored_value);
        REQUIRE(found_value.value() == std::vector<std::byte>{ expected_value.begin(), expected_value.end() });
    }

    void require_missing_key(const BTree& tree, std::uint8_t key_value) {

        auto key = make_key(key_value);
        auto result = tree.find(key);
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::NotFound);
    }

}

TEST_CASE("BTree create_new initializes an empty leaf root", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    const auto& tree = tree_result.value();
    REQUIRE(tree.root_page_id().is_valid());
    REQUIRE(tree.key_size() == KEY_SIZE);
    REQUIRE(tree.value_size() == VALUE_SIZE);

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto leaf_page_result = BTreeLeafPage<const std::byte>::open(root_page_result.value().page()->data());
        REQUIRE(leaf_page_result.ok());

        const auto& leaf_page = leaf_page_result.value();
        REQUIRE(leaf_page.is_root());
        REQUIRE(leaf_page.key_count() == 0);
        REQUIRE(leaf_page.key_size() == KEY_SIZE);
        REQUIRE(leaf_page.value_size() == VALUE_SIZE);
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree open_existing accepts a matching root page", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto created_tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(created_tree_result.ok());

    const auto root_page_id = created_tree_result.value().root_page_id();
    auto opened_tree_result = BTree::open_existing(pager, root_page_id, KEY_SIZE, VALUE_SIZE);
    REQUIRE(opened_tree_result.ok());

    const auto& opened_tree = opened_tree_result.value();
    REQUIRE(opened_tree.root_page_id() == root_page_id);
    REQUIRE(opened_tree.key_size() == KEY_SIZE);
    REQUIRE(opened_tree.value_size() == VALUE_SIZE);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree open_existing rejects a non-root page", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    dandb::storage::PageId non_root_page_id;
    {
        auto page_handle_result = pager.new_page();
        REQUIRE(page_handle_result.ok());

        auto& page_handle = page_handle_result.value();
        non_root_page_id = page_handle.page()->id();

        auto mutable_page_result = page_handle.mutable_page();
        REQUIRE(mutable_page_result.ok());
        REQUIRE(initialize_leaf(mutable_page_result.value()->data(), KEY_SIZE, VALUE_SIZE).ok());
    }

    auto opened_tree_result = BTree::open_existing(pager, non_root_page_id, KEY_SIZE, VALUE_SIZE);
    REQUIRE_FALSE(opened_tree_result.ok());
    REQUIRE(opened_tree_result.status().code() == StatusCode::InvalidArgument);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree open_existing rejects mismatched root layout", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto created_tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(created_tree_result.ok());

    const auto root_page_id = created_tree_result.value().root_page_id();

    SECTION("key size does not match") {
        auto opened_tree_result = BTree::open_existing(pager, root_page_id, KEY_SIZE+1, VALUE_SIZE);
        REQUIRE_FALSE(opened_tree_result.ok());
        REQUIRE(opened_tree_result.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("value size does not match") {
        auto opened_tree_result = BTree::open_existing(pager, root_page_id, KEY_SIZE, VALUE_SIZE+1);
        REQUIRE_FALSE(opened_tree_result.ok());
        REQUIRE(opened_tree_result.status().code() == StatusCode::InvalidArgument);
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree find reports not found in an empty root leaf", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    require_missing_key(tree_result.value(), 10);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree find returns values from a one-page leaf", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    insert_root_leaf_entry(tree, pager, 10, 10);
    insert_root_leaf_entry(tree, pager, 30, 30);

    require_found_value(tree, 10, 10);
    require_found_value(tree, 30, 30);
    require_missing_key(tree, 20);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree insert rejects duplicate keys", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto key = make_key(10);
    auto original_value = make_value(10);
    REQUIRE(tree.insert(key, original_value).ok());

    auto duplicate_value = make_value(20);
    auto duplicate_status = tree.insert(key, duplicate_value);
    REQUIRE_FALSE(duplicate_status.ok());
    REQUIRE(duplicate_status.code() == StatusCode::ConstraintViolation);

    require_found_value(tree, 10, 10);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree find rejects invalid key sizes", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    std::array<std::byte, KEY_SIZE+1> oversized_key{};
    auto result = tree_result.value().find(oversized_key);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == StatusCode::InvalidArgument);

    std::array<std::byte, KEY_SIZE-1> undersized_key{};
    result = tree_result.value().find(undersized_key);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == StatusCode::InvalidArgument);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree find routes equal separator keys to the right child", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 6);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    const auto left_leaf_page_id = create_leaf_with_entry(pager, 10, 10);
    const auto middle_leaf_page_id = create_leaf_with_entry(pager, 20, 20);
    const auto right_leaf_page_id = create_leaf_with_entry(pager, 30, 30);

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto mutable_page_result = root_page_result.value().mutable_page();
        REQUIRE(mutable_page_result.ok());

        auto& root_page = *mutable_page_result.value();
        REQUIRE(initialize_internal(root_page.data(), KEY_SIZE, VALUE_SIZE).ok());

        auto internal_page_result = BTreeInternalPage<std::byte>::open(root_page.data());
        REQUIRE(internal_page_result.ok());

        auto& internal_page = internal_page_result.value();
        internal_page.set_root(true);
        internal_page.set_first_child_page_id(left_leaf_page_id);

        auto middle_separator = make_key(20);
        auto right_separator = make_key(30);
        REQUIRE(internal_page.insert_entry(0, middle_separator, middle_leaf_page_id).ok());
        REQUIRE(internal_page.insert_entry(1, right_separator, right_leaf_page_id).ok());
    }

    auto search_key = make_key(20);
    auto found_value = tree.find(search_key);
    REQUIRE(found_value.ok());

    const auto expected_value = make_value(20);
    REQUIRE(found_value.value() == std::vector<std::byte>{ expected_value.begin(), expected_value.end() });

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree find routes internal boundary keys to the correct child", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 6);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    const auto left_leaf_page_id = create_leaf_with_entry(pager, 10, 10);
    const auto middle_leaf_page_id = create_leaf_with_entry(pager, 25, 25);
    const auto right_leaf_page_id = create_leaf_with_entry(pager, 40, 40);

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto mutable_page_result = root_page_result.value().mutable_page();
        REQUIRE(mutable_page_result.ok());

        auto& root_page = *mutable_page_result.value();
        REQUIRE(initialize_internal(root_page.data(), KEY_SIZE, VALUE_SIZE).ok());

        auto internal_page_result = BTreeInternalPage<std::byte>::open(root_page.data());
        REQUIRE(internal_page_result.ok());

        auto& internal_page = internal_page_result.value();
        internal_page.set_root(true);
        internal_page.set_first_child_page_id(left_leaf_page_id);

        auto middle_separator = make_key(20);
        auto right_separator = make_key(30);
        REQUIRE(internal_page.insert_entry(0, middle_separator, middle_leaf_page_id).ok());
        REQUIRE(internal_page.insert_entry(1, right_separator, right_leaf_page_id).ok());
    }

    require_found_value(tree, 10, 10);
    require_found_value(tree, 25, 25);
    require_found_value(tree, 40, 40);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}
