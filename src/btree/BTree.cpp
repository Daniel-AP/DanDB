#include <dandb/btree/BTree.h>

#include <dandb/btree/BTreeInternalPage.h>
#include <dandb/btree/BTreeLeafPage.h>
#include <dandb/btree/BTreePage.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/PageHandle.h>
#include <dandb/storage/Pager.h>

#include <cstring>
#include <cstddef>
#include <vector>

namespace dandb::btree {

    BTree::BTree(
        storage::Pager& pager,
        storage::PageId root_page_id,
        std::uint16_t key_size,
        std::uint16_t value_size
    ) :
        pager_(&pager),
        root_page_id_(root_page_id),
        key_size_(key_size),
        value_size_(value_size)
    {}

    core::Result<BTree> BTree::create_new(
        storage::Pager& pager,
        std::uint16_t key_size,
        std::uint16_t value_size
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
            value_size
        };

    }

    core::Result<BTree> BTree::open_existing(
        storage::Pager& pager,
        storage::PageId root_page_id,
        std::uint16_t key_size,
        std::uint16_t value_size
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
            value_size
        };

    }

    core::Result<std::vector<std::byte>> BTree::find(std::span<const std::byte> key) const {

        if(key.size() != key_size_) {
            return core::Status::InvalidArgument("Cannot find B+ tree key: key size is invalid");
        }

        auto current_page_id = root_page_id_;

        while(true) {

            auto page_handle_result = pager_->get_page(current_page_id);
            if(!page_handle_result.ok()) {
                return page_handle_result.status();
            }

            const auto* page = page_handle_result.value().page();
            auto btree_page_result = BTreePage<const std::byte>::open(page->data());
            if(!btree_page_result.ok()) {
                return btree_page_result.status();
            }

            const auto& btree_page = btree_page_result.value();

            if(btree_page.kind() == BTreePageKind::Leaf) {

                auto leaf_page_result = BTreeLeafPage<const std::byte>::open(page->data());
                if(!leaf_page_result.ok()) {
                    return leaf_page_result.status();
                }

                const auto& leaf_page = leaf_page_result.value();
                auto position_result = leaf_page.find_insertion_position(key);
                if(!position_result.ok()) {
                    return position_result.status();
                }

                const auto position = position_result.value();
                if(position == leaf_page.key_count()) {
                    return core::Status::NotFound("Cannot find B+ tree key: key was not found");
                }

                auto stored_key_result = leaf_page.key_at(position);
                if(!stored_key_result.ok()) {
                    return stored_key_result.status();
                }

                const auto stored_key = stored_key_result.value();
                if(std::memcmp(stored_key.data(), key.data(), key_size_) != 0) {
                    return core::Status::NotFound("Cannot find B+ tree key: key was not found");
                }

                auto stored_value_result = leaf_page.value_at(position);
                if(!stored_value_result.ok()) {
                    return stored_value_result.status();
                }

                const auto stored_value = stored_value_result.value();
                return std::vector<std::byte>{ stored_value.begin(), stored_value.end() };

            }

            auto internal_page_result = BTreeInternalPage<const std::byte>::open(page->data());
            if(!internal_page_result.ok()) {
                return internal_page_result.status();
            }

            const auto& internal_page = internal_page_result.value();
            auto child_page_id_result = internal_page.child_page_id_for_key(key);
            if(!child_page_id_result.ok()) {
                return child_page_id_result.status();
            }

            current_page_id = child_page_id_result.value();

        }

    }

    core::Status BTree::insert(std::span<const std::byte> key, std::span<const std::byte> value) {

        if(key.size() != key_size_) {
            return core::Status::InvalidArgument("Cannot insert key/value into B+ tree: key size is invalid");
        }

        if(value.size() != value_size_) {
            return core::Status::InvalidArgument("Cannot insert key/value into B+ tree: value size is invalid");
        }

        const auto split_result = insert_into_subtree(root_page_id_, key, value);
        if(!split_result.ok()) {
            return split_result.status();
        }

        const auto& split = split_result.value();

        if(!split.has_value()) {
            return core::Status::Ok();
        }

        const auto old_root_page_id = root_page_id_;
        const auto right_child_page_id = split->right_child_page_id;
        storage::PageId new_root_page_id;

        // Create the new internal root.
        {
            auto new_root_handle_result = pager_->new_page();
            if(!new_root_handle_result.ok()) {
                return new_root_handle_result.status();
            }

            auto& new_root_handle = new_root_handle_result.value();

            const auto new_root_page_result = new_root_handle.mutable_page();
            if(!new_root_page_result.ok()) {
                return new_root_page_result.status();
            }

            auto& new_root_page = new_root_page_result.value();
            new_root_page_id = new_root_page->id();

            const auto init_internal_status = initialize_internal(new_root_page->data(), key_size_, value_size_);
            if(!init_internal_status.ok()) {
                return init_internal_status;
            }

            auto new_root_page_view_result = BTreeInternalPage<std::byte>::open(new_root_page->data());
            if(!new_root_page_view_result.ok()) {
                return new_root_page_view_result.status();
            }

            auto& new_root_page_view = new_root_page_view_result.value();

            new_root_page_view.set_root(true);
            new_root_page_view.set_first_child_page_id(old_root_page_id);

            auto insert_status = new_root_page_view.insert_entry(0, split->separator_key, right_child_page_id);
            if(!insert_status.ok()) {
                return insert_status;
            }
        }

        // The old root becomes the left child.
        {
            auto old_root_handle_result = pager_->get_page(old_root_page_id);
            if(!old_root_handle_result.ok()) {
                return old_root_handle_result.status();
            }

            auto& old_root_handle = old_root_handle_result.value();

            const auto old_root_page_result = old_root_handle.mutable_page();
            if(!old_root_page_result.ok()) {
                return old_root_page_result.status();
            }

            auto& old_root_page = old_root_page_result.value();

            auto old_root_page_view_result = BTreePage<std::byte>::open(old_root_page->data());
            if(!old_root_page_view_result.ok()) {
                return old_root_page_view_result.status();
            }

            auto& old_root_page_view = old_root_page_view_result.value();

            old_root_page_view.set_root(false);
            old_root_page_view.set_parent_page_id(new_root_page_id);
        }

        // The split page becomes the right child.
        {
            auto right_child_handle_result = pager_->get_page(right_child_page_id);
            if(!right_child_handle_result.ok()) {
                return right_child_handle_result.status();
            }

            auto& right_child_handle = right_child_handle_result.value();

            const auto right_child_page_result = right_child_handle.mutable_page();
            if(!right_child_page_result.ok()) {
                return right_child_page_result.status();
            }

            auto& right_child_page = right_child_page_result.value();

            auto right_child_page_view_result = BTreePage<std::byte>::open(right_child_page->data());
            if(!right_child_page_view_result.ok()) {
                return right_child_page_view_result.status();
            }

            auto& right_child_page_view = right_child_page_view_result.value();

            right_child_page_view.set_root(false);
            right_child_page_view.set_parent_page_id(new_root_page_id);
        }

        root_page_id_ = new_root_page_id;

        return core::Status::Ok();

    }

    storage::PageId BTree::root_page_id() const {
        return root_page_id_;
    }

    std::uint16_t BTree::key_size() const {
        return key_size_;
    }

    std::uint16_t BTree::value_size() const {
        return value_size_;
    }

    core::Result<std::optional<SplitResult>> BTree::insert_into_subtree(
        storage::PageId page_id,
        std::span<const std::byte> key,
        std::span<const std::byte> value
    ) {

        auto page_handle_result = pager_->get_page(page_id);
        if(!page_handle_result.ok()) {
            return page_handle_result.status();
        }

        auto& page_handle = page_handle_result.value();
        const auto* page = page_handle.page();

        auto page_view_result = BTreePage<const std::byte>::open(page->data());
        if(!page_view_result.ok()) {
            return page_view_result.status();
        }

        auto& page_view = page_view_result.value();

        if(page_view.kind() == BTreePageKind::Leaf) {
            return insert_into_leaf(page_id, key, value);
        }

        return insert_into_internal(page_id, key, value);

    }

}
