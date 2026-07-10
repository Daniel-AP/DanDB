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
using dandb::btree::BTreeCursor;
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
    constexpr std::uint16_t INDEX_KEY_SIZE = 4;
    constexpr std::uint16_t PRIMARY_KEY_VALUE_SIZE = KEY_SIZE;
    constexpr std::uint16_t SMALL_LEAF_VALUE_SIZE = 2008;
    constexpr std::uint16_t LEAF_BORROW_VALUE_SIZE = 1000;
    constexpr std::uint16_t SINGLE_ENTRY_LEAF_VALUE_SIZE = 4000;
    constexpr std::uint16_t SMALL_INTERNAL_KEY_SIZE = 2000;
    constexpr std::uint16_t SMALL_INTERNAL_VALUE_SIZE = 2000;

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

    std::vector<std::byte> make_sized_key(std::uint16_t key_size, std::uint8_t value) {
        std::vector<std::byte> key(key_size);

        key[key_size-1] = static_cast<std::byte>(value);
        return key;
    }

    void insert_sized_entry(
        BTree& tree,
        std::uint16_t key_size,
        std::uint16_t value_size,
        std::uint8_t value
    ) {
        const auto key = make_sized_key(key_size, value);
        const auto stored_value = make_value(value_size, value);

        REQUIRE(tree.insert(key, stored_value).ok());
    }

    template<std::size_t N>
    void require_key_equals(
        std::span<const std::byte> actual,
        const std::array<std::byte, N>& expected
    ) {
        REQUIRE(
            std::vector<std::byte>{ actual.begin(), actual.end() }
            == std::vector<std::byte>{ expected.begin(), expected.end() }
        );
    }

    std::array<std::byte, INDEX_KEY_SIZE> make_index_key(std::uint8_t value) {
        std::array<std::byte, INDEX_KEY_SIZE> key{};

        key[INDEX_KEY_SIZE-1] = static_cast<std::byte>(value);
        return key;
    }

    std::array<std::byte, PRIMARY_KEY_VALUE_SIZE> make_primary_key_value(std::uint8_t value) {
        std::array<std::byte, PRIMARY_KEY_VALUE_SIZE> primary_key{};

        primary_key[PRIMARY_KEY_VALUE_SIZE-1] = static_cast<std::byte>(value);
        return primary_key;
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

    void require_found_value(
        const BTree& tree,
        std::uint8_t key_value,
        std::uint16_t value_size,
        std::uint8_t stored_value
    ) {

        auto key = make_key(key_value);
        auto found_value = tree.find(key);
        REQUIRE(found_value.ok());

        const auto expected_value = make_value(value_size, stored_value);
        REQUIRE(found_value.value() == expected_value);
    }

    void require_missing_key(const BTree& tree, std::uint8_t key_value) {

        auto key = make_key(key_value);
        auto result = tree.find(key);
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::NotFound);
    }

    void require_next_scan_entry(
        BTreeCursor& cursor,
        std::uint8_t key_value,
        std::uint16_t value_size,
        std::uint8_t stored_value
    ) {

        auto entry_result = cursor.next();
        REQUIRE(entry_result.ok());
        REQUIRE(entry_result.value().has_value());

        const auto& entry = entry_result.value().value();
        const auto expected_key = make_key(key_value);
        const auto expected_value = make_value(value_size, stored_value);

        REQUIRE(entry.key == std::vector<std::byte>{ expected_key.begin(), expected_key.end() });
        REQUIRE(entry.value == expected_value);
    }

    void require_next_index_scan_entry(
        BTreeCursor& cursor,
        std::uint8_t index_key_value,
        std::uint8_t primary_key_value
    ) {

        auto entry_result = cursor.next();
        REQUIRE(entry_result.ok());
        REQUIRE(entry_result.value().has_value());

        const auto& entry = entry_result.value().value();
        const auto expected_key = make_index_key(index_key_value);
        const auto expected_value = make_primary_key_value(primary_key_value);

        REQUIRE(entry.key == std::vector<std::byte>{ expected_key.begin(), expected_key.end() });
        REQUIRE(entry.value == std::vector<std::byte>{ expected_value.begin(), expected_value.end() });
    }

    void require_scan_finished(BTreeCursor& cursor) {

        auto entry_result = cursor.next();
        REQUIRE(entry_result.ok());
        REQUIRE_FALSE(entry_result.value().has_value());
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

TEST_CASE("BTree scan returns no entries for an empty tree", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto cursor_result = tree_result.value().scan();
    REQUIRE(cursor_result.ok());

    require_scan_finished(cursor_result.value());

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

TEST_CASE("BTree scan returns one-leaf entries in key order", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto key_30 = make_key(30);
    auto value_30 = make_value(30);
    REQUIRE(tree.insert(key_30, value_30).ok());

    auto key_10 = make_key(10);
    auto value_10 = make_value(10);
    REQUIRE(tree.insert(key_10, value_10).ok());

    auto key_20 = make_key(20);
    auto value_20 = make_value(20);
    REQUIRE(tree.insert(key_20, value_20).ok());

    auto cursor_result = tree.scan();
    REQUIRE(cursor_result.ok());

    auto& cursor = cursor_result.value();
    require_next_scan_entry(cursor, 10, VALUE_SIZE, 10);
    require_next_scan_entry(cursor, 20, VALUE_SIZE, 20);
    require_next_scan_entry(cursor, 30, VALUE_SIZE, 30);
    require_scan_finished(cursor);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree range scan includes the lower bound and excludes the upper bound", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30 }) {
        auto key = make_key(key_value);
        auto value = make_value(key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    const auto lower_bound = make_key(20);
    const auto upper_bound = make_key(30);
    auto cursor_result = tree.scan_range(lower_bound, upper_bound);
    REQUIRE(cursor_result.ok());

    auto& cursor = cursor_result.value();
    require_next_scan_entry(cursor, 20, VALUE_SIZE, 20);
    require_scan_finished(cursor);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree range scan stops at an upper bound without a lower bound", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30 }) {
        auto key = make_key(key_value);
        auto value = make_value(key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    const auto upper_bound = make_key(20);
    auto cursor_result = tree.scan_range(std::nullopt, upper_bound);
    REQUIRE(cursor_result.ok());

    auto& cursor = cursor_result.value();
    require_next_scan_entry(cursor, 10, VALUE_SIZE, 10);
    require_scan_finished(cursor);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree range scan returns no entries for an empty range", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30 }) {
        auto key = make_key(key_value);
        auto value = make_value(key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    const auto bound = make_key(20);
    auto cursor_result = tree.scan_range(bound, bound);
    REQUIRE(cursor_result.ok());

    require_scan_finished(cursor_result.value());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree stores index-like entries as secondary keys pointing to primary keys", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, INDEX_KEY_SIZE, PRIMARY_KEY_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto index_key = make_index_key(42);
    auto primary_key = make_primary_key_value(7);
    REQUIRE(tree.insert(index_key, primary_key).ok());

    auto found_value = tree.find(index_key);
    REQUIRE(found_value.ok());
    REQUIRE(found_value.value() == std::vector<std::byte>{ primary_key.begin(), primary_key.end() });

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree scan returns index-like entries in secondary-key order", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, INDEX_KEY_SIZE, PRIMARY_KEY_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto key_30 = make_index_key(30);
    auto primary_key_3 = make_primary_key_value(3);
    REQUIRE(tree.insert(key_30, primary_key_3).ok());

    auto key_10 = make_index_key(10);
    auto primary_key_1 = make_primary_key_value(1);
    REQUIRE(tree.insert(key_10, primary_key_1).ok());

    auto key_20 = make_index_key(20);
    auto primary_key_2 = make_primary_key_value(2);
    REQUIRE(tree.insert(key_20, primary_key_2).ok());

    auto cursor_result = tree.scan();
    REQUIRE(cursor_result.ok());

    auto& cursor = cursor_result.value();
    require_next_index_scan_entry(cursor, 10, 1);
    require_next_index_scan_entry(cursor, 20, 2);
    require_next_index_scan_entry(cursor, 30, 3);
    require_scan_finished(cursor);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree insert stores values in a one-page root leaf", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto key_30 = make_key(30);
    auto value_30 = make_value(30);
    REQUIRE(tree.insert(key_30, value_30).ok());

    auto key_10 = make_key(10);
    auto value_10 = make_value(10);
    REQUIRE(tree.insert(key_10, value_10).ok());

    auto key_20 = make_key(20);
    auto value_20 = make_value(20);
    REQUIRE(tree.insert(key_20, value_20).ok());

    require_found_value(tree, 10, 10);
    require_found_value(tree, 20, 20);
    require_found_value(tree, 30, 30);

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto leaf_page_result = BTreeLeafPage<const std::byte>::open(root_page_result.value().page()->data());
        REQUIRE(leaf_page_result.ok());

        const auto& leaf_page = leaf_page_result.value();
        REQUIRE(leaf_page.is_root());
        REQUIRE(leaf_page.key_count() == 3);
    }

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

TEST_CASE("BTree insert rejects invalid key and value sizes", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto valid_key = make_key(10);
    auto valid_value = make_value(10);

    std::array<std::byte, KEY_SIZE+1> oversized_key{};
    auto status = tree.insert(oversized_key, valid_value);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);

    std::array<std::byte, KEY_SIZE-1> undersized_key{};
    status = tree.insert(undersized_key, valid_value);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);

    std::array<std::byte, VALUE_SIZE+1> oversized_value{};
    status = tree.insert(valid_key, oversized_value);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);

    std::array<std::byte, VALUE_SIZE-1> undersized_value{};
    status = tree.insert(valid_key, undersized_value);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);

    require_missing_key(tree, 10);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree insert splits a full root leaf", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 6);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto key_30 = make_key(30);
    auto value_30 = make_value(SMALL_LEAF_VALUE_SIZE, 30);
    REQUIRE(tree.insert(key_30, value_30).ok());

    auto key_10 = make_key(10);
    auto value_10 = make_value(SMALL_LEAF_VALUE_SIZE, 10);
    REQUIRE(tree.insert(key_10, value_10).ok());

    auto key_20 = make_key(20);
    auto value_20 = make_value(SMALL_LEAF_VALUE_SIZE, 20);
    REQUIRE(tree.insert(key_20, value_20).ok());

    require_found_value(tree, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_found_value(tree, 20, SMALL_LEAF_VALUE_SIZE, 20);
    require_found_value(tree, 30, SMALL_LEAF_VALUE_SIZE, 30);

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto root_page_result_view = BTreeInternalPage<const std::byte>::open(root_page_result.value().page()->data());
        REQUIRE(root_page_result_view.ok());

        const auto& root_page = root_page_result_view.value();
        REQUIRE(root_page.is_root());
        REQUIRE(root_page.key_count() == 1);

        const auto left_leaf_page_id = root_page.first_child_page_id();
        auto right_leaf_page_id_result = root_page.right_child_page_id_at(0);
        REQUIRE(right_leaf_page_id_result.ok());

        const auto right_leaf_page_id = right_leaf_page_id_result.value();
        REQUIRE(left_leaf_page_id.is_valid());
        REQUIRE(right_leaf_page_id.is_valid());
        REQUIRE(left_leaf_page_id != right_leaf_page_id);

        {
            auto left_leaf_handle_result = pager.get_page(left_leaf_page_id);
            REQUIRE(left_leaf_handle_result.ok());

            auto left_leaf_result = BTreeLeafPage<const std::byte>::open(left_leaf_handle_result.value().page()->data());
            REQUIRE(left_leaf_result.ok());

            const auto& left_leaf = left_leaf_result.value();
            REQUIRE_FALSE(left_leaf.is_root());
            REQUIRE(left_leaf.parent_page_id() == tree.root_page_id());
            REQUIRE(left_leaf.key_count() == 1);
            REQUIRE_FALSE(left_leaf.previous_leaf_page_id().is_valid());
            REQUIRE(left_leaf.next_leaf_page_id() == right_leaf_page_id);
        }

        {
            auto right_leaf_handle_result = pager.get_page(right_leaf_page_id);
            REQUIRE(right_leaf_handle_result.ok());

            auto right_leaf_result = BTreeLeafPage<const std::byte>::open(right_leaf_handle_result.value().page()->data());
            REQUIRE(right_leaf_result.ok());

            const auto& right_leaf = right_leaf_result.value();
            REQUIRE_FALSE(right_leaf.is_root());
            REQUIRE(right_leaf.parent_page_id() == tree.root_page_id());
            REQUIRE(right_leaf.key_count() == 2);
            REQUIRE(right_leaf.previous_leaf_page_id() == left_leaf_page_id);
            REQUIRE_FALSE(right_leaf.next_leaf_page_id().is_valid());

            auto separator_key_result = root_page.key_at(0);
            REQUIRE(separator_key_result.ok());

            auto first_right_key_result = right_leaf.key_at(0);
            REQUIRE(first_right_key_result.ok());

            REQUIRE(
                std::vector<std::byte>{ separator_key_result.value().begin(), separator_key_result.value().end() }
                == std::vector<std::byte>{ first_right_key_result.value().begin(), first_right_key_result.value().end() }
            );
        }
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree insert rejects duplicate keys after a root leaf split", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 6);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto key_10 = make_key(10);
    auto value_10 = make_value(SMALL_LEAF_VALUE_SIZE, 10);
    REQUIRE(tree.insert(key_10, value_10).ok());

    auto key_20 = make_key(20);
    auto value_20 = make_value(SMALL_LEAF_VALUE_SIZE, 20);
    REQUIRE(tree.insert(key_20, value_20).ok());

    auto key_30 = make_key(30);
    auto value_30 = make_value(SMALL_LEAF_VALUE_SIZE, 30);
    REQUIRE(tree.insert(key_30, value_30).ok());

    auto duplicate_value = make_value(SMALL_LEAF_VALUE_SIZE, 99);
    auto duplicate_status = tree.insert(key_20, duplicate_value);
    REQUIRE_FALSE(duplicate_status.ok());
    REQUIRE(duplicate_status.code() == StatusCode::ConstraintViolation);

    require_found_value(tree, 20, SMALL_LEAF_VALUE_SIZE, 20);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree insert supports multiple leaf splits below one root", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 12);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 50, 10, 40, 20, 30 }) {
        auto key = make_key(key_value);
        auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    require_found_value(tree, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_found_value(tree, 20, SMALL_LEAF_VALUE_SIZE, 20);
    require_found_value(tree, 30, SMALL_LEAF_VALUE_SIZE, 30);
    require_found_value(tree, 40, SMALL_LEAF_VALUE_SIZE, 40);
    require_found_value(tree, 50, SMALL_LEAF_VALUE_SIZE, 50);

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto root_page_result_view = BTreeInternalPage<const std::byte>::open(root_page_result.value().page()->data());
        REQUIRE(root_page_result_view.ok());
        REQUIRE(root_page_result_view.value().is_root());
        REQUIRE(root_page_result_view.value().key_count() > 1);
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree scan follows leaf sibling links in key order", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 12);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 50, 10, 40, 20, 30 }) {
        auto key = make_key(key_value);
        auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    auto cursor_result = tree.scan();
    REQUIRE(cursor_result.ok());

    auto& cursor = cursor_result.value();
    require_next_scan_entry(cursor, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_next_scan_entry(cursor, 20, SMALL_LEAF_VALUE_SIZE, 20);
    require_next_scan_entry(cursor, 30, SMALL_LEAF_VALUE_SIZE, 30);
    require_next_scan_entry(cursor, 40, SMALL_LEAF_VALUE_SIZE, 40);
    require_next_scan_entry(cursor, 50, SMALL_LEAF_VALUE_SIZE, 50);
    require_scan_finished(cursor);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree insert splits an internal root when separators no longer fit", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 300);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SINGLE_ENTRY_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint16_t key_index = 0; key_index < 254; key_index++) {
        const auto key_value = static_cast<std::uint8_t>(key_index);
        auto key = make_key(key_value);
        auto value = make_value(SINGLE_ENTRY_LEAF_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto root_page_view_result = BTreeInternalPage<const std::byte>::open(root_page_result.value().page()->data());
        REQUIRE(root_page_view_result.ok());
        REQUIRE(root_page_view_result.value().is_root());
        REQUIRE(root_page_view_result.value().key_count() == 1);
    }

    for(std::uint16_t key_index = 0; key_index < 254; key_index++) {
        const auto key_value = static_cast<std::uint8_t>(key_index);
        require_found_value(tree, key_value, SINGLE_ENTRY_LEAF_VALUE_SIZE, key_value);
    }

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

TEST_CASE("BTree erase removes every entry from a root leaf", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto key_10 = make_key(10);
    auto value_10 = make_value(10);
    REQUIRE(tree.insert(key_10, value_10).ok());
    REQUIRE(tree.erase(key_10).ok());

    require_missing_key(tree, 10);
    REQUIRE(tree.validate().ok());

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto root_leaf_result = BTreeLeafPage<const std::byte>::open(root_page_result.value().page()->data());
        REQUIRE(root_leaf_result.ok());
        REQUIRE(root_leaf_result.value().is_root());
        REQUIRE(root_leaf_result.value().key_count() == 0);
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase reports a missing key without changing the tree", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
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
    const auto erase_status = tree.erase(key_20);
    REQUIRE_FALSE(erase_status.ok());
    REQUIRE(erase_status.code() == StatusCode::NotFound);

    require_found_value(tree, 10, 10);
    REQUIRE(tree.validate().ok());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase rejects a key with an invalid size", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    auto key_10 = make_key(10);
    auto value_10 = make_value(10);
    REQUIRE(tree.insert(key_10, value_10).ok());

    std::array<std::byte, KEY_SIZE-1> undersized_key{};
    const auto erase_status = tree.erase(undersized_key);
    REQUIRE_FALSE(erase_status.ok());
    REQUIRE(erase_status.code() == StatusCode::InvalidArgument);

    require_found_value(tree, 10, 10);
    REQUIRE(tree.validate().ok());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase refreshes a separator after removing a leaf first key", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 6);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30 }) {
        auto key = make_key(key_value);
        auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    auto key_20 = make_key(20);
    REQUIRE(tree.erase(key_20).ok());

    require_found_value(tree, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_missing_key(tree, 20);
    require_found_value(tree, 30, SMALL_LEAF_VALUE_SIZE, 30);
    REQUIRE(tree.validate().ok());

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto root_page_view_result = BTreeInternalPage<const std::byte>::open(root_page_result.value().page()->data());
        REQUIRE(root_page_view_result.ok());

        auto separator_key_result = root_page_view_result.value().key_at(0);
        REQUIRE(separator_key_result.ok());
        require_key_equals(separator_key_result.value(), make_key(30));
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase borrows from the right leaf sibling", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 6);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30 }) {
        auto key = make_key(key_value);
        auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    auto key_10 = make_key(10);
    REQUIRE(tree.erase(key_10).ok());

    require_missing_key(tree, 10);
    require_found_value(tree, 20, SMALL_LEAF_VALUE_SIZE, 20);
    require_found_value(tree, 30, SMALL_LEAF_VALUE_SIZE, 30);
    REQUIRE(tree.validate().ok());

    auto cursor_result = tree.scan();
    REQUIRE(cursor_result.ok());

    auto& cursor = cursor_result.value();
    require_next_scan_entry(cursor, 20, SMALL_LEAF_VALUE_SIZE, 20);
    require_next_scan_entry(cursor, 30, SMALL_LEAF_VALUE_SIZE, 30);
    require_scan_finished(cursor);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase borrows from the left leaf sibling", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 6);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30, 15 }) {
        auto key = make_key(key_value);
        auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    auto key_30 = make_key(30);
    auto key_20 = make_key(20);
    REQUIRE(tree.erase(key_30).ok());
    REQUIRE(tree.erase(key_20).ok());

    require_found_value(tree, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_found_value(tree, 15, SMALL_LEAF_VALUE_SIZE, 15);
    require_missing_key(tree, 20);
    require_missing_key(tree, 30);
    REQUIRE(tree.validate().ok());

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto root_page_view_result = BTreeInternalPage<const std::byte>::open(root_page_result.value().page()->data());
        REQUIRE(root_page_view_result.ok());

        auto separator_key_result = root_page_view_result.value().key_at(0);
        REQUIRE(separator_key_result.ok());
        require_key_equals(separator_key_result.value(), make_key(15));
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase merges leaves and preserves scan order", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 8);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30, 40, 50 }) {
        auto key = make_key(key_value);
        auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    auto key_20 = make_key(20);
    REQUIRE(tree.erase(key_20).ok());

    require_found_value(tree, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_missing_key(tree, 20);
    require_found_value(tree, 30, SMALL_LEAF_VALUE_SIZE, 30);
    require_found_value(tree, 40, SMALL_LEAF_VALUE_SIZE, 40);
    require_found_value(tree, 50, SMALL_LEAF_VALUE_SIZE, 50);
    REQUIRE(tree.validate().ok());

    auto cursor_result = tree.scan();
    REQUIRE(cursor_result.ok());

    auto& cursor = cursor_result.value();
    require_next_scan_entry(cursor, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_next_scan_entry(cursor, 30, SMALL_LEAF_VALUE_SIZE, 30);
    require_next_scan_entry(cursor, 40, SMALL_LEAF_VALUE_SIZE, 40);
    require_next_scan_entry(cursor, 50, SMALL_LEAF_VALUE_SIZE, 50);
    require_scan_finished(cursor);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase refreshes a middle leaf separator after borrowing from the right", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 8);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, LEAF_BORROW_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30, 40, 50, 60, 70 }) {
        auto key = make_key(key_value);
        auto value = make_value(LEAF_BORROW_VALUE_SIZE, key_value);
        REQUIRE(tree.insert(key, value).ok());
    }

    auto key_30 = make_key(30);
    REQUIRE(tree.erase(key_30).ok());

    require_missing_key(tree, 30);
    require_found_value(tree, 40, LEAF_BORROW_VALUE_SIZE, 40);
    require_found_value(tree, 50, LEAF_BORROW_VALUE_SIZE, 50);
    require_found_value(tree, 60, LEAF_BORROW_VALUE_SIZE, 60);
    require_found_value(tree, 70, LEAF_BORROW_VALUE_SIZE, 70);
    REQUIRE(tree.validate().ok());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase borrows from the right internal sibling", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 16);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, SMALL_INTERNAL_KEY_SIZE, SMALL_INTERNAL_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30, 40, 50 }) {
        insert_sized_entry(tree, SMALL_INTERNAL_KEY_SIZE, SMALL_INTERNAL_VALUE_SIZE, key_value);
    }

    const auto root_before_erase = tree.root_page_id();
    const auto key_10 = make_sized_key(SMALL_INTERNAL_KEY_SIZE, 10);
    REQUIRE(tree.erase(key_10).ok());

    for(std::uint8_t key_value: { 20, 30, 40, 50 }) {
        const auto key = make_sized_key(SMALL_INTERNAL_KEY_SIZE, key_value);
        const auto found_value = tree.find(key);
        REQUIRE(found_value.ok());
        REQUIRE(found_value.value() == make_value(SMALL_INTERNAL_VALUE_SIZE, key_value));
    }

    const auto erased_value = tree.find(key_10);
    REQUIRE_FALSE(erased_value.ok());
    REQUIRE(erased_value.status().code() == StatusCode::NotFound);
    REQUIRE(tree.root_page_id() == root_before_erase);
    REQUIRE(tree.validate().ok());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase merges internal children and shrinks the root", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 16);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, SMALL_INTERNAL_KEY_SIZE, SMALL_INTERNAL_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30, 40 }) {
        insert_sized_entry(tree, SMALL_INTERNAL_KEY_SIZE, SMALL_INTERNAL_VALUE_SIZE, key_value);
    }

    const auto old_root_page_id = tree.root_page_id();
    const auto key_10 = make_sized_key(SMALL_INTERNAL_KEY_SIZE, 10);
    REQUIRE(tree.erase(key_10).ok());

    for(std::uint8_t key_value: { 20, 30, 40 }) {
        const auto key = make_sized_key(SMALL_INTERNAL_KEY_SIZE, key_value);
        const auto found_value = tree.find(key);
        REQUIRE(found_value.ok());
        REQUIRE(found_value.value() == make_value(SMALL_INTERNAL_VALUE_SIZE, key_value));
    }

    REQUIRE(tree.root_page_id() != old_root_page_id);
    REQUIRE(tree.validate().ok());

    {
        auto root_page_result = pager.get_page(tree.root_page_id());
        REQUIRE(root_page_result.ok());

        auto root_page_view_result = BTreeInternalPage<const std::byte>::open(root_page_result.value().page()->data());
        REQUIRE(root_page_view_result.ok());
        REQUIRE(root_page_view_result.value().is_root());
        REQUIRE(root_page_view_result.value().key_count() == 2);
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase refreshes an internal separator before merging a child", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 32);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, SMALL_INTERNAL_KEY_SIZE, SMALL_INTERNAL_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 }) {
        insert_sized_entry(tree, SMALL_INTERNAL_KEY_SIZE, SMALL_INTERNAL_VALUE_SIZE, key_value);
    }

    for(std::uint8_t key_value: { 40, 10, 70 }) {
        const auto key = make_sized_key(SMALL_INTERNAL_KEY_SIZE, key_value);
        REQUIRE(tree.erase(key).ok());
    }

    REQUIRE(tree.validate().ok());

    for(std::uint8_t key_value: { 20, 30, 50, 60, 80, 90, 100 }) {
        const auto key = make_sized_key(SMALL_INTERNAL_KEY_SIZE, key_value);
        REQUIRE(tree.find(key).ok());
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree erase preserves a multi-level tree through a delete sequence", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 32);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, SMALL_INTERNAL_KEY_SIZE, SMALL_INTERNAL_VALUE_SIZE);
    REQUIRE(tree_result.ok());

    auto& tree = tree_result.value();
    for(std::uint8_t key_value: { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 }) {
        insert_sized_entry(tree, SMALL_INTERNAL_KEY_SIZE, SMALL_INTERNAL_VALUE_SIZE, key_value);
    }

    REQUIRE(tree.validate().ok());

    for(std::uint8_t key_value: { 40, 10, 70, 20, 80, 30, 60, 50, 90, 100 }) {
        const auto key = make_sized_key(SMALL_INTERNAL_KEY_SIZE, key_value);
        REQUIRE(tree.erase(key).ok());

        const auto erased_value = tree.find(key);
        REQUIRE_FALSE(erased_value.ok());
        REQUIRE(erased_value.status().code() == StatusCode::NotFound);

        const auto validation_status = tree.validate();
        CAPTURE(key_value);
        INFO(validation_status.message());
        REQUIRE(validation_status.ok());
    }

    auto cursor_result = tree.scan();
    REQUIRE(cursor_result.ok());
    require_scan_finished(cursor_result.value());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("BTree committed inserts survive Pager reopen", "[btree][tree][transaction]") {
    const TempDir temp_dir;
    PageId root_page_id;

    {
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

        root_page_id = tree.root_page_id();
        REQUIRE(tree.validate().ok());
        REQUIRE(pager.commit_transaction().ok());
        REQUIRE(pager.close().ok());
    }

    auto reopened_result = Pager::open(temp_dir.database_path(), 8);
    REQUIRE(reopened_result.ok());

    Pager& reopened_pager = reopened_result.value();
    auto reopened_tree_result = BTree::open_existing(reopened_pager, root_page_id, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(reopened_tree_result.ok());

    auto& reopened_tree = reopened_tree_result.value();
    REQUIRE(reopened_tree.validate().ok());
    require_found_value(reopened_tree, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_found_value(reopened_tree, 20, SMALL_LEAF_VALUE_SIZE, 20);
    require_found_value(reopened_tree, 30, SMALL_LEAF_VALUE_SIZE, 30);

    auto cursor_result = reopened_tree.scan();
    REQUIRE(cursor_result.ok());

    auto& cursor = cursor_result.value();
    require_next_scan_entry(cursor, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_next_scan_entry(cursor, 20, SMALL_LEAF_VALUE_SIZE, 20);
    require_next_scan_entry(cursor, 30, SMALL_LEAF_VALUE_SIZE, 30);
    require_scan_finished(cursor);

    REQUIRE(reopened_pager.close().ok());
}

TEST_CASE("BTree rolled back inserts do not survive Pager reopen", "[btree][tree][transaction]") {
    const TempDir temp_dir;
    PageId root_page_id;

    {
        auto pager_result = Pager::create(temp_dir.database_path(), 8);
        REQUIRE(pager_result.ok());

        Pager& pager = pager_result.value();
        REQUIRE(pager.begin_transaction().ok());

        auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
        REQUIRE(tree_result.ok());

        auto& tree = tree_result.value();
        root_page_id = tree.root_page_id();

        REQUIRE(pager.commit_transaction().ok());
        REQUIRE(pager.begin_transaction().ok());

        for(std::uint8_t key_value: { 30, 10, 20 }) {
            auto key = make_key(key_value);
            auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
            REQUIRE(tree.insert(key, value).ok());
        }

        REQUIRE(tree.root_page_id() != root_page_id);
        REQUIRE(tree.validate().ok());
        REQUIRE(pager.rollback_transaction().ok());
        REQUIRE(pager.close().ok());
    }

    auto reopened_result = Pager::open(temp_dir.database_path(), 8);
    REQUIRE(reopened_result.ok());

    Pager& reopened_pager = reopened_result.value();
    auto reopened_tree_result = BTree::open_existing(reopened_pager, root_page_id, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(reopened_tree_result.ok());

    auto& reopened_tree = reopened_tree_result.value();
    REQUIRE(reopened_tree.validate().ok());
    require_missing_key(reopened_tree, 10);
    require_missing_key(reopened_tree, 20);
    require_missing_key(reopened_tree, 30);

    auto cursor_result = reopened_tree.scan();
    REQUIRE(cursor_result.ok());
    require_scan_finished(cursor_result.value());

    REQUIRE(reopened_pager.close().ok());
}

TEST_CASE("BTree committed deletes survive Pager reopen", "[btree][tree][transaction]") {
    const TempDir temp_dir;
    PageId root_page_id;

    {
        auto pager_result = Pager::create(temp_dir.database_path(), 8);
        REQUIRE(pager_result.ok());

        Pager& pager = pager_result.value();
        REQUIRE(pager.begin_transaction().ok());

        auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
        REQUIRE(tree_result.ok());

        auto& tree = tree_result.value();
        for(std::uint8_t key_value: { 10, 20, 30, 40, 50 }) {
            auto key = make_key(key_value);
            auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
            REQUIRE(tree.insert(key, value).ok());
        }

        root_page_id = tree.root_page_id();
        REQUIRE(pager.commit_transaction().ok());
        REQUIRE(pager.close().ok());
    }

    {
        auto pager_result = Pager::open(temp_dir.database_path(), 8);
        REQUIRE(pager_result.ok());

        Pager& pager = pager_result.value();
        REQUIRE(pager.begin_transaction().ok());

        auto tree_result = BTree::open_existing(pager, root_page_id, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
        REQUIRE(tree_result.ok());

        auto& tree = tree_result.value();
        auto key_20 = make_key(20);
        REQUIRE(tree.erase(key_20).ok());

        root_page_id = tree.root_page_id();
        REQUIRE(tree.validate().ok());
        REQUIRE(pager.commit_transaction().ok());
        REQUIRE(pager.close().ok());
    }

    auto reopened_result = Pager::open(temp_dir.database_path(), 8);
    REQUIRE(reopened_result.ok());

    Pager& reopened_pager = reopened_result.value();
    auto reopened_tree_result = BTree::open_existing(reopened_pager, root_page_id, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(reopened_tree_result.ok());

    auto& reopened_tree = reopened_tree_result.value();
    REQUIRE(reopened_tree.validate().ok());
    require_found_value(reopened_tree, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_missing_key(reopened_tree, 20);
    require_found_value(reopened_tree, 30, SMALL_LEAF_VALUE_SIZE, 30);
    require_found_value(reopened_tree, 40, SMALL_LEAF_VALUE_SIZE, 40);
    require_found_value(reopened_tree, 50, SMALL_LEAF_VALUE_SIZE, 50);

    REQUIRE(reopened_pager.close().ok());
}

TEST_CASE("BTree rolled back deletes do not survive Pager reopen", "[btree][tree][transaction]") {
    const TempDir temp_dir;
    PageId root_page_id;

    {
        auto pager_result = Pager::create(temp_dir.database_path(), 8);
        REQUIRE(pager_result.ok());

        Pager& pager = pager_result.value();
        REQUIRE(pager.begin_transaction().ok());

        auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
        REQUIRE(tree_result.ok());

        auto& tree = tree_result.value();
        for(std::uint8_t key_value: { 10, 20, 30, 40, 50 }) {
            auto key = make_key(key_value);
            auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
            REQUIRE(tree.insert(key, value).ok());
        }

        root_page_id = tree.root_page_id();
        REQUIRE(pager.commit_transaction().ok());
        REQUIRE(pager.begin_transaction().ok());

        auto key_20 = make_key(20);
        REQUIRE(tree.erase(key_20).ok());
        REQUIRE(tree.validate().ok());

        REQUIRE(pager.rollback_transaction().ok());
        REQUIRE(pager.close().ok());
    }

    auto reopened_result = Pager::open(temp_dir.database_path(), 8);
    REQUIRE(reopened_result.ok());

    Pager& reopened_pager = reopened_result.value();
    auto reopened_tree_result = BTree::open_existing(reopened_pager, root_page_id, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(reopened_tree_result.ok());

    auto& reopened_tree = reopened_tree_result.value();
    REQUIRE(reopened_tree.validate().ok());
    require_found_value(reopened_tree, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_found_value(reopened_tree, 20, SMALL_LEAF_VALUE_SIZE, 20);
    require_found_value(reopened_tree, 30, SMALL_LEAF_VALUE_SIZE, 30);
    require_found_value(reopened_tree, 40, SMALL_LEAF_VALUE_SIZE, 40);
    require_found_value(reopened_tree, 50, SMALL_LEAF_VALUE_SIZE, 50);

    REQUIRE(reopened_pager.close().ok());
}

TEST_CASE("BTree checkpoint preserves committed tree changes after Pager reopen", "[btree][tree][transaction]") {
    const TempDir temp_dir;
    PageId root_page_id;

    {
        auto pager_result = Pager::create(temp_dir.database_path(), 8);
        REQUIRE(pager_result.ok());

        Pager& pager = pager_result.value();
        REQUIRE(pager.begin_transaction().ok());

        auto tree_result = BTree::create_new(pager, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
        REQUIRE(tree_result.ok());

        auto& tree = tree_result.value();
        for(std::uint8_t key_value: { 10, 20, 30, 40, 50 }) {
            auto key = make_key(key_value);
            auto value = make_value(SMALL_LEAF_VALUE_SIZE, key_value);
            REQUIRE(tree.insert(key, value).ok());
        }

        auto key_20 = make_key(20);
        REQUIRE(tree.erase(key_20).ok());

        root_page_id = tree.root_page_id();
        REQUIRE(tree.validate().ok());
        REQUIRE(pager.commit_transaction().ok());
        REQUIRE(pager.checkpoint().ok());
        REQUIRE(pager.close().ok());
    }

    auto reopened_result = Pager::open(temp_dir.database_path(), 8);
    REQUIRE(reopened_result.ok());

    Pager& reopened_pager = reopened_result.value();
    auto reopened_tree_result = BTree::open_existing(reopened_pager, root_page_id, KEY_SIZE, SMALL_LEAF_VALUE_SIZE);
    REQUIRE(reopened_tree_result.ok());

    auto& reopened_tree = reopened_tree_result.value();
    REQUIRE(reopened_tree.validate().ok());
    require_found_value(reopened_tree, 10, SMALL_LEAF_VALUE_SIZE, 10);
    require_missing_key(reopened_tree, 20);
    require_found_value(reopened_tree, 30, SMALL_LEAF_VALUE_SIZE, 30);
    require_found_value(reopened_tree, 40, SMALL_LEAF_VALUE_SIZE, 40);
    require_found_value(reopened_tree, 50, SMALL_LEAF_VALUE_SIZE, 50);

    REQUIRE(reopened_pager.close().ok());
}
