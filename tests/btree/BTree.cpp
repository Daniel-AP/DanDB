#include <catch_amalgamated.hpp>

#include <dandb/btree/BTree.h>
#include <dandb/btree/BTreeLeafPage.h>
#include <dandb/btree/BTreePage.h>
#include <dandb/core/Status.h>
#include <dandb/storage/PageId.h>
#include <dandb/storage/Pager.h>
#include <testutil/TempDir.h>

#include <cstddef>
#include <cstdint>

using dandb::btree::BTree;
using dandb::btree::BTreeLeafPage;
using dandb::btree::initialize_leaf;
using dandb::core::StatusCode;
using dandb::storage::Pager;
using dandb::testutil::TempDir;

namespace {

    constexpr std::uint16_t KEY_SIZE = 8;
    constexpr std::uint16_t VALUE_SIZE = 32;

}

TEST_CASE("BTree create_new initializes an empty leaf root", "[btree][tree]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), 3);
    REQUIRE(pager_result.ok());

    Pager& pager = pager_result.value();
    REQUIRE(pager.begin_transaction().ok());

    auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE, true);
    REQUIRE(tree_result.ok());

    const auto& tree = tree_result.value();
    REQUIRE(tree.root_page_id().is_valid());
    REQUIRE(tree.key_size() == KEY_SIZE);
    REQUIRE(tree.value_size() == VALUE_SIZE);
    REQUIRE(tree.uniqueness());

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

    auto created_tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE, false);
    REQUIRE(created_tree_result.ok());

    const auto root_page_id = created_tree_result.value().root_page_id();
    auto opened_tree_result = BTree::open_existing(pager, root_page_id, KEY_SIZE, VALUE_SIZE, false);
    REQUIRE(opened_tree_result.ok());

    const auto& opened_tree = opened_tree_result.value();
    REQUIRE(opened_tree.root_page_id() == root_page_id);
    REQUIRE(opened_tree.key_size() == KEY_SIZE);
    REQUIRE(opened_tree.value_size() == VALUE_SIZE);
    REQUIRE_FALSE(opened_tree.uniqueness());

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

    auto opened_tree_result = BTree::open_existing(pager, non_root_page_id, KEY_SIZE, VALUE_SIZE, false);
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

    auto created_tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE, true);
    REQUIRE(created_tree_result.ok());

    const auto root_page_id = created_tree_result.value().root_page_id();

    SECTION("key size does not match") {
        auto opened_tree_result = BTree::open_existing(pager, root_page_id, KEY_SIZE+1, VALUE_SIZE, true);
        REQUIRE_FALSE(opened_tree_result.ok());
        REQUIRE(opened_tree_result.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("value size does not match") {
        auto opened_tree_result = BTree::open_existing(pager, root_page_id, KEY_SIZE, VALUE_SIZE+1, true);
        REQUIRE_FALSE(opened_tree_result.ok());
        REQUIRE(opened_tree_result.status().code() == StatusCode::InvalidArgument);
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}
