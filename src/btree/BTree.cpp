#include <dandb/btree/BTree.h>

#include <dandb/btree/BTreeLeafPage.h>
#include <dandb/btree/BTreePage.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/PageHandle.h>
#include <dandb/storage/Pager.h>

#include <cstddef>

namespace dandb::btree {

    BTree::BTree(
        storage::Pager& pager,
        storage::PageId root_page_id,
        std::uint16_t key_size,
        std::uint16_t value_size,
        bool uniqueness
    ) :
        pager_(&pager),
        root_page_id_(root_page_id),
        key_size_(key_size),
        value_size_(value_size),
        uniqueness_(uniqueness)
    {}

    core::Result<BTree> BTree::create_new(
        storage::Pager& pager,
        std::uint16_t key_size,
        std::uint16_t value_size,
        bool uniqueness
    ) {

        auto page_handle_result = pager.new_page();
        if(!page_handle_result.ok()) {
            return page_handle_result.status();
        }

        auto& page_handle = page_handle_result.value();
        const auto root_page_id = page_handle.page()->id();

        auto mutable_page_result = page_handle.mutable_page();
        if(!mutable_page_result.ok()) {
            return mutable_page_result.status();
        }

        auto& page = *mutable_page_result.value();

        auto status = initialize_leaf(page.data(), key_size, value_size);
        if(!status.ok()) {
            return status;
        }

        auto leaf_page_result = BTreeLeafPage<std::byte>::open(page.data());
        if(!leaf_page_result.ok()) {
            return leaf_page_result.status();
        }

        leaf_page_result.value().set_root(true);

        return BTree{
            pager,
            root_page_id,
            key_size,
            value_size,
            uniqueness
        };

    }

    core::Result<BTree> BTree::open_existing(
        storage::Pager& pager,
        storage::PageId root_page_id,
        std::uint16_t key_size,
        std::uint16_t value_size,
        bool uniqueness
    ) {

        auto page_handle_result = pager.get_page(root_page_id);
        if(!page_handle_result.ok()) {
            return page_handle_result.status();
        }

        const auto* page = page_handle_result.value().page();
        auto btree_page_result = BTreePage<const std::byte>::open(page->data());
        if(!btree_page_result.ok()) {
            return btree_page_result.status();
        }

        const auto& btree_page = btree_page_result.value();
        if(!btree_page.is_root()) {
            return core::Status::InvalidArgument("Cannot open existing B+ tree: root page is not marked as root");
        }

        if(btree_page.key_size() != key_size) {
            return core::Status::InvalidArgument("Cannot open existing B+ tree: key size does not match root page");
        }

        if(btree_page.value_size() != value_size) {
            return core::Status::InvalidArgument("Cannot open existing B+ tree: value size does not match root page");
        }

        return BTree{
            pager,
            root_page_id,
            key_size,
            value_size,
            uniqueness
        };

    }

}
