#include <dandb/btree/BTree.h>

#include <dandb/btree/BTreeInternalPage.h>
#include <dandb/btree/BTreeLeafPage.h>
#include <dandb/btree/BTreePage.h>
#include <dandb/btree/BTreeValidator.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/PageHandle.h>
#include <dandb/storage/Pager.h>

#include <cstring>
#include <cstddef>
#include <utility>
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

    core::Status BTree::erase(std::span<const std::byte> key) {

        if(key.size() != key_size_) {
            return core::Status::InvalidArgument("Cannot erase key from B+ tree: key size is invalid");
        }

        auto erase_result = erase_from_subtree(root_page_id_, key);
        if(!erase_result.ok()) {
            return erase_result.status();
        }

        storage::PageId new_root_page_id;

        {
            auto root_page_handle_result = pager_->get_page(root_page_id_);
            if(!root_page_handle_result.ok()) {
                return root_page_handle_result.status();
            }

            auto root_page_result = root_page_handle_result.value().mutable_page();
            if(!root_page_result.ok()) {
                return root_page_result.status();
            }

            auto root_page_view_result = BTreePage<std::byte>::open(root_page_result.value()->data());
            if(!root_page_view_result.ok()) {
                return root_page_view_result.status();
            }

            auto& root_page_view = root_page_view_result.value();

            if(root_page_view.kind() == BTreePageKind::Leaf || root_page_view.key_count() > 0) {
                return core::Status::Ok();
            }

            auto root_internal_page_view_result = BTreeInternalPage<std::byte>::open(root_page_result.value()->data());
            if(!root_internal_page_view_result.ok()) {
                return root_internal_page_view_result.status();
            }

            auto& root_internal_page_view = root_internal_page_view_result.value();
            new_root_page_id = root_internal_page_view.first_child_page_id();
            root_internal_page_view.set_root(false);
        }

        {
            auto new_root_page_handle_result = pager_->get_page(new_root_page_id);
            if(!new_root_page_handle_result.ok()) {
                return new_root_page_handle_result.status();
            }

            auto new_root_page_result = new_root_page_handle_result.value().mutable_page();
            if(!new_root_page_result.ok()) {
                return new_root_page_result.status();
            }

            auto new_root_page_view_result = BTreePage<std::byte>::open(new_root_page_result.value()->data());
            if(!new_root_page_view_result.ok()) {
                return new_root_page_view_result.status();
            }

            auto& new_root_page_view = new_root_page_view_result.value();

            new_root_page_view.set_root(true);
            new_root_page_view.set_parent_page_id(storage::INVALID_PAGE_ID);
        }

        root_page_id_ = new_root_page_id;

        return core::Status::Ok();

    }

    core::Result<BTreeCursor> BTree::scan() const {

        storage::PageId current_page_id = root_page_id_;

        while(true) {

            auto handle_result = pager_->get_page(current_page_id);
            if(!handle_result.ok()) {
                return handle_result.status();
            }

            auto& handle = handle_result.value();
            const auto* page = handle.page();

            auto page_view_result = BTreePage<const std::byte>::open(page->data());
            if(!page_view_result.ok()) {
                return page_view_result.status();
            }

            auto& page_view = page_view_result.value();

            if(page_view.kind() == BTreePageKind::Internal) {
                
                auto internal_page_view_result = BTreeInternalPage<const std::byte>::open(page->data());
                if(!internal_page_view_result.ok()) {
                    return internal_page_view_result.status();
                }

                auto& internal_page_view = internal_page_view_result.value();

                current_page_id = internal_page_view.first_child_page_id();
                continue;

            }

            return BTreeCursor{ *pager_, current_page_id };

        }

    }

    core::Status BTree::validate() const {
        return BTreeValidator::validate(*pager_, root_page_id_, key_size_, value_size_);
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

        BTreePageKind page_kind;

        {
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

            const auto& page_view = page_view_result.value();
            page_kind = page_view.kind();
        }

        if(page_kind == BTreePageKind::Leaf) {
            return insert_into_leaf(page_id, key, value);
        }

        return insert_into_internal(page_id, key, value);

    }

    core::Result<bool> BTree::erase_from_subtree(
        storage::PageId page_id,
        std::span<const std::byte> key
    ) {

        BTreePageKind page_kind;

        {
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

            const auto& page_view = page_view_result.value();
            page_kind = page_view.kind();
        }

        if(page_kind == BTreePageKind::Leaf) {
            return erase_from_leaf(page_id, key);
        }

        return erase_from_internal(page_id, key);

    }

    core::Result<bool> BTree::erase_from_leaf(
        storage::PageId page_id,
        std::span<const std::byte> key
    ) {

        std::uint16_t position;

        // Check key exists
        {
            auto page_handle_result = pager_->get_page(page_id);
            if(!page_handle_result.ok()) {
                return page_handle_result.status();
            }

            auto& page_handle = page_handle_result.value();
            const auto* page = page_handle.page();
            auto page_view_result = BTreeLeafPage<const std::byte>::open(page->data());
            if(!page_view_result.ok()) {
                return page_view_result.status();
            }

            auto& page_view = page_view_result.value();
            auto position_result = page_view.find_insertion_position(key);
            if(!position_result.ok()) {
                return position_result.status();
            }

            position = position_result.value();
            if(position == page_view.key_count()) {
                return core::Status::NotFound("Cannot erase key from B+ tree: key was not found");
            }

            auto stored_key_result = page_view.key_at(position);
            if(!stored_key_result.ok()) {
                return stored_key_result.status();
            }

            const auto stored_key = stored_key_result.value();
            if(std::memcmp(stored_key.data(), key.data(), key_size_) != 0) {
                return core::Status::NotFound("Cannot erase key from B+ tree: key was not found");
            }
        }

        // Erase entry by position
        {
            auto page_handle_result = pager_->get_page(page_id);
            if(!page_handle_result.ok()) {
                return page_handle_result.status();
            }

            auto& page_handle = page_handle_result.value();
            auto page_result = page_handle.mutable_page();
            if(!page_result.ok()) {
                return page_result.status();
            }

            auto& page = page_result.value();
            auto page_view_result = BTreeLeafPage<std::byte>::open(page->data());
            if(!page_view_result.ok()) {
                return page_view_result.status();
            }

            auto& page_view = page_view_result.value();

            auto erase_status = page_view.erase_entry(position);
            if(!erase_status.ok()) {
                return erase_status;
            }
        }

        return page_is_underfull(page_id);

    }

    core::Result<bool> BTree::erase_from_internal(
        storage::PageId page_id,
        std::span<const std::byte> key
    ) {

        std::uint16_t child_index;
        storage::PageId child_page_id;

        {
            auto page_handle_result = pager_->get_page(page_id);
            if(!page_handle_result.ok()) {
                return page_handle_result.status();
            }

            auto& page_handle = page_handle_result.value();
            const auto* page = page_handle.page();
            auto page_view_result = BTreeInternalPage<const std::byte>::open(page->data());
            if(!page_view_result.ok()) {
                return page_view_result.status();
            }

            const auto& page_view = page_view_result.value();
            auto child_index_result = page_view.child_index_for_key(key);
            if(!child_index_result.ok()) {
                return child_index_result.status();
            }

            child_index = child_index_result.value();
            if(child_index == 0) {
                child_page_id = page_view.first_child_page_id();
            } else {
                auto child_page_id_result = page_view.right_child_page_id_at(
                    static_cast<std::uint16_t>(child_index-1)
                );
                if(!child_page_id_result.ok()) {
                    return child_page_id_result.status();
                }

                child_page_id = child_page_id_result.value();
            }
        }

        auto erase_result = erase_from_subtree(child_page_id, key);
        if(!erase_result.ok()) {
            return erase_result.status();
        }

        if(erase_result.value()) {
            auto rebalance_status = rebalance_child_after_erase(page_id, child_index);
            if(!rebalance_status.ok()) {
                return rebalance_status;
            }
        } else if(child_index > 0) {
            auto refresh_status = refresh_child_separator(page_id, child_index);
            if(!refresh_status.ok()) {
                return refresh_status;
            }
        }

        return page_is_underfull(page_id);

    }

    core::Status BTree::rebalance_child_after_erase(
        storage::PageId parent_page_id,
        std::uint16_t child_index
    ) {

        storage::PageId child_page_id;

        {
            auto parent_page_handle_result = pager_->get_page(parent_page_id);
            if(!parent_page_handle_result.ok()) {
                return parent_page_handle_result.status();
            }

            const auto* parent_page = parent_page_handle_result.value().page();
            auto parent_page_view_result = BTreeInternalPage<const std::byte>::open(parent_page->data());
            if(!parent_page_view_result.ok()) {
                return parent_page_view_result.status();
            }

            const auto& parent_page_view = parent_page_view_result.value();
            if(child_index == 0) {
                child_page_id = parent_page_view.first_child_page_id();
            } else {
                auto child_page_id_result = parent_page_view.right_child_page_id_at(
                    static_cast<std::uint16_t>(child_index-1)
                );
                if(!child_page_id_result.ok()) {
                    return child_page_id_result.status();
                }

                child_page_id = child_page_id_result.value();
            }
        }

        BTreePageKind child_page_kind;

        {
            auto child_page_handle_result = pager_->get_page(child_page_id);
            if(!child_page_handle_result.ok()) {
                return child_page_handle_result.status();
            }

            const auto* child_page = child_page_handle_result.value().page();
            auto child_page_view_result = BTreePage<const std::byte>::open(child_page->data());
            if(!child_page_view_result.ok()) {
                return child_page_view_result.status();
            }

            child_page_kind = child_page_view_result.value().kind();
        }

        if(child_page_kind == BTreePageKind::Leaf) {
            return rebalance_leaf_child_after_erase(parent_page_id, child_index);
        }

        return rebalance_internal_child_after_erase(parent_page_id, child_index);

    }

    core::Status BTree::rebalance_leaf_child_after_erase(
        storage::PageId parent_page_id,
        std::uint16_t child_index
    ) {

        storage::PageId child_page_id;
        storage::PageId left_sibling_page_id = storage::INVALID_PAGE_ID;
        storage::PageId right_sibling_page_id = storage::INVALID_PAGE_ID;

        // Read child and sibling locations from the parent
        {
            auto parent_page_handle_result = pager_->get_page(parent_page_id);
            if(!parent_page_handle_result.ok()) {
                return parent_page_handle_result.status();
            }

            const auto* parent_page = parent_page_handle_result.value().page();
            auto parent_page_view_result = BTreeInternalPage<const std::byte>::open(parent_page->data());
            if(!parent_page_view_result.ok()) {
                return parent_page_view_result.status();
            }

            const auto& parent_page_view = parent_page_view_result.value();
            if(child_index == 0) {
                child_page_id = parent_page_view.first_child_page_id();
            } else {
                auto child_page_id_result = parent_page_view.right_child_page_id_at(static_cast<std::uint16_t>(child_index-1));
                if(!child_page_id_result.ok()) {
                    return child_page_id_result.status();
                }

                child_page_id = child_page_id_result.value();
            }

            if(child_index > 0) {
                if(child_index == 1) {
                    left_sibling_page_id = parent_page_view.first_child_page_id();
                } else {
                    auto left_sibling_page_id_result = parent_page_view.right_child_page_id_at(
                        static_cast<std::uint16_t>(child_index-2)
                    );
                    if(!left_sibling_page_id_result.ok()) {
                        return left_sibling_page_id_result.status();
                    }

                    left_sibling_page_id = left_sibling_page_id_result.value();
                }
            }

            if(child_index < parent_page_view.key_count()) {
                auto right_sibling_page_id_result = parent_page_view.right_child_page_id_at(child_index);
                if(!right_sibling_page_id_result.ok()) {
                    return right_sibling_page_id_result.status();
                }

                right_sibling_page_id = right_sibling_page_id_result.value();
            }
        }

        // Try to borrow the left sibling's last entry
        if(left_sibling_page_id.is_valid()) {

            std::vector<std::byte> borrowed_entry;
            std::uint16_t borrowed_entry_index = 0;
            bool left_can_lend = false;

            {
                auto left_sibling_handle_result = pager_->get_page(left_sibling_page_id);
                if(!left_sibling_handle_result.ok()) {
                    return left_sibling_handle_result.status();
                }

                const auto* left_sibling_page = left_sibling_handle_result.value().page();
                auto left_sibling_view_result = BTreeLeafPage<const std::byte>::open(left_sibling_page->data());
                if(!left_sibling_view_result.ok()) {
                    return left_sibling_view_result.status();
                }

                const auto& left_sibling_view = left_sibling_view_result.value();
                const auto minimum_key_count = static_cast<std::uint16_t>((left_sibling_view.capacity()+1)/2);
                if(left_sibling_view.key_count() > minimum_key_count) {
                    borrowed_entry_index = static_cast<std::uint16_t>(left_sibling_view.key_count()-1);

                    auto borrowed_entry_result = left_sibling_view.entry_at(borrowed_entry_index);
                    if(!borrowed_entry_result.ok()) {
                        return borrowed_entry_result.status();
                    }

                    const auto stored_entry = borrowed_entry_result.value();
                    borrowed_entry.assign(stored_entry.begin(), stored_entry.end());
                    left_can_lend = true;
                }
            }

            if(left_can_lend) {

                // Insert the borrowed entry at the start of the child
                {
                    auto child_page_handle_result = pager_->get_page(child_page_id);
                    if(!child_page_handle_result.ok()) {
                        return child_page_handle_result.status();
                    }

                    auto child_page_result = child_page_handle_result.value().mutable_page();
                    if(!child_page_result.ok()) {
                        return child_page_result.status();
                    }

                    auto child_page_view_result = BTreeLeafPage<std::byte>::open(child_page_result.value()->data());
                    if(!child_page_view_result.ok()) {
                        return child_page_view_result.status();
                    }

                    const std::span<const std::byte> entry{ borrowed_entry.data(), borrowed_entry.size() };
                    auto insert_status = child_page_view_result.value().insert_entry(
                        0,
                        entry.subspan(0, key_size_),
                        entry.subspan(key_size_, value_size_)
                    );
                    if(!insert_status.ok()) {
                        return insert_status;
                    }
                }

                // Remove the donated entry from the left sibling
                {
                    auto left_sibling_handle_result = pager_->get_page(left_sibling_page_id);
                    if(!left_sibling_handle_result.ok()) {
                        return left_sibling_handle_result.status();
                    }

                    auto left_sibling_page_result = left_sibling_handle_result.value().mutable_page();
                    if(!left_sibling_page_result.ok()) {
                        return left_sibling_page_result.status();
                    }

                    auto left_sibling_view_result = BTreeLeafPage<std::byte>::open(left_sibling_page_result.value()->data());
                    if(!left_sibling_view_result.ok()) {
                        return left_sibling_view_result.status();
                    }

                    auto erase_status = left_sibling_view_result.value().erase_entry(borrowed_entry_index);
                    if(!erase_status.ok()) {
                        return erase_status;
                    }
                }

                auto refresh_status = refresh_child_separator(parent_page_id, child_index);
                if(!refresh_status.ok()) {
                    return refresh_status;
                }

                return core::Status::Ok();
            }
        }

        // Try to borrow the right sibling's first entry
        if(right_sibling_page_id.is_valid()) {

            std::vector<std::byte> borrowed_entry;
            bool right_can_lend = false;

            {
                auto right_sibling_handle_result = pager_->get_page(right_sibling_page_id);
                if(!right_sibling_handle_result.ok()) {
                    return right_sibling_handle_result.status();
                }

                const auto* right_sibling_page = right_sibling_handle_result.value().page();
                auto right_sibling_view_result = BTreeLeafPage<const std::byte>::open(right_sibling_page->data());
                if(!right_sibling_view_result.ok()) {
                    return right_sibling_view_result.status();
                }

                const auto& right_sibling_view = right_sibling_view_result.value();
                const auto minimum_key_count = static_cast<std::uint16_t>((right_sibling_view.capacity()+1)/2);
                if(right_sibling_view.key_count() > minimum_key_count) {
                    auto borrowed_entry_result = right_sibling_view.entry_at(0);
                    if(!borrowed_entry_result.ok()) {
                        return borrowed_entry_result.status();
                    }

                    const auto stored_entry = borrowed_entry_result.value();
                    borrowed_entry.assign(stored_entry.begin(), stored_entry.end());
                    right_can_lend = true;
                }
            }

            if(right_can_lend) {

                // Append the borrowed entry to the child
                {
                    auto child_page_handle_result = pager_->get_page(child_page_id);
                    if(!child_page_handle_result.ok()) {
                        return child_page_handle_result.status();
                    }

                    auto child_page_result = child_page_handle_result.value().mutable_page();
                    if(!child_page_result.ok()) {
                        return child_page_result.status();
                    }

                    auto child_page_view_result = BTreeLeafPage<std::byte>::open(child_page_result.value()->data());
                    if(!child_page_view_result.ok()) {
                        return child_page_view_result.status();
                    }

                    auto& child_page_view = child_page_view_result.value();
                    const std::span<const std::byte> entry{ borrowed_entry.data(), borrowed_entry.size() };
                    auto insert_status = child_page_view.insert_entry(
                        child_page_view.key_count(),
                        entry.subspan(0, key_size_),
                        entry.subspan(key_size_, value_size_)
                    );
                    if(!insert_status.ok()) {
                        return insert_status;
                    }
                }

                // Remove the donated entry from the right sibling
                {
                    auto right_sibling_handle_result = pager_->get_page(right_sibling_page_id);
                    if(!right_sibling_handle_result.ok()) {
                        return right_sibling_handle_result.status();
                    }

                    auto right_sibling_page_result = right_sibling_handle_result.value().mutable_page();
                    if(!right_sibling_page_result.ok()) {
                        return right_sibling_page_result.status();
                    }

                    auto right_sibling_view_result = BTreeLeafPage<std::byte>::open(right_sibling_page_result.value()->data());
                    if(!right_sibling_view_result.ok()) {
                        return right_sibling_view_result.status();
                    }

                    auto erase_status = right_sibling_view_result.value().erase_entry(0);
                    if(!erase_status.ok()) {
                        return erase_status;
                    }
                }

                auto refresh_status = refresh_child_separator(
                    parent_page_id,
                    static_cast<std::uint16_t>(child_index+1)
                );
                if(!refresh_status.ok()) {
                    return refresh_status;
                }

                return core::Status::Ok();
            }
        }

        // Try to merge the child into the left sibling
        if(left_sibling_page_id.is_valid()) {

            auto merge_status = merge_adjacent_leaves(left_sibling_page_id, child_page_id);
            if(!merge_status.ok()) {
                return merge_status;
            }

            // Remove the entry from the parent pointing to the merged-away right child
            {
                auto parent_page_handle_result = pager_->get_page(parent_page_id);
                if(!parent_page_handle_result.ok()) {
                    return parent_page_handle_result.status();
                }

                auto parent_page_result = parent_page_handle_result.value().mutable_page();
                if(!parent_page_result.ok()) {
                    return parent_page_result.status();
                }

                auto parent_page_view_result = BTreeInternalPage<std::byte>::open(parent_page_result.value()->data());
                if(!parent_page_view_result.ok()) {
                    return parent_page_view_result.status();
                }

                auto erase_status = parent_page_view_result.value().erase_entry(
                    static_cast<std::uint16_t>(child_index-1)
                );
                if(!erase_status.ok()) {
                    return erase_status;
                }
            }

            return core::Status::Ok();
        }

        // Merge the right sibling into the child
        if(right_sibling_page_id.is_valid()) {

            auto merge_status = merge_adjacent_leaves(child_page_id, right_sibling_page_id);
            if(!merge_status.ok()) {
                return merge_status;
            }

            // Remove the entry from the parent pointing to the merged-away right child
            {
                auto parent_page_handle_result = pager_->get_page(parent_page_id);
                if(!parent_page_handle_result.ok()) {
                    return parent_page_handle_result.status();
                }

                auto parent_page_result = parent_page_handle_result.value().mutable_page();
                if(!parent_page_result.ok()) {
                    return parent_page_result.status();
                }

                auto parent_page_view_result = BTreeInternalPage<std::byte>::open(parent_page_result.value()->data());
                if(!parent_page_view_result.ok()) {
                    return parent_page_view_result.status();
                }

                auto erase_status = parent_page_view_result.value().erase_entry(child_index);
                if(!erase_status.ok()) {
                    return erase_status;
                }
            }

            return core::Status::Ok();
        }

        return core::Status::Corruption("Cannot rebalance B+ tree leaf child after erase: child has no siblings");

    }

    core::Status BTree::rebalance_internal_child_after_erase(
        storage::PageId parent_page_id,
        std::uint16_t child_index
    ) {

        storage::PageId child_page_id;
        storage::PageId left_sibling_page_id = storage::INVALID_PAGE_ID;
        storage::PageId right_sibling_page_id = storage::INVALID_PAGE_ID;

        // Read child and sibling locations from the parent
        {
            auto parent_page_handle_result = pager_->get_page(parent_page_id);
            if(!parent_page_handle_result.ok()) {
                return parent_page_handle_result.status();
            }

            const auto* parent_page = parent_page_handle_result.value().page();
            auto parent_page_view_result = BTreeInternalPage<const std::byte>::open(parent_page->data());
            if(!parent_page_view_result.ok()) {
                return parent_page_view_result.status();
            }

            const auto& parent_page_view = parent_page_view_result.value();
            if(child_index == 0) {
                child_page_id = parent_page_view.first_child_page_id();
            } else {
                auto child_page_id_result = parent_page_view.right_child_page_id_at(static_cast<std::uint16_t>(child_index-1));
                if(!child_page_id_result.ok()) {
                    return child_page_id_result.status();
                }

                child_page_id = child_page_id_result.value();
            }

            if(child_index > 0) {
                if(child_index == 1) {
                    left_sibling_page_id = parent_page_view.first_child_page_id();
                } else {
                    auto left_sibling_page_id_result = parent_page_view.right_child_page_id_at(
                        static_cast<std::uint16_t>(child_index-2)
                    );
                    if(!left_sibling_page_id_result.ok()) {
                        return left_sibling_page_id_result.status();
                    }

                    left_sibling_page_id = left_sibling_page_id_result.value();
                }
            }

            if(child_index < parent_page_view.key_count()) {
                auto right_sibling_page_id_result = parent_page_view.right_child_page_id_at(child_index);
                if(!right_sibling_page_id_result.ok()) {
                    return right_sibling_page_id_result.status();
                }

                right_sibling_page_id = right_sibling_page_id_result.value();
            }
        }

        // Try to borrow the left sibling's last child
        if(left_sibling_page_id.is_valid()) {

            storage::PageId borrowed_child_page_id;
            std::uint16_t borrowed_entry_index = 0;
            bool left_can_lend = false;

            {
                auto left_sibling_handle_result = pager_->get_page(left_sibling_page_id);
                if(!left_sibling_handle_result.ok()) {
                    return left_sibling_handle_result.status();
                }

                const auto* left_sibling_page = left_sibling_handle_result.value().page();
                auto left_sibling_view_result = BTreeInternalPage<const std::byte>::open(left_sibling_page->data());
                if(!left_sibling_view_result.ok()) {
                    return left_sibling_view_result.status();
                }

                const auto& left_sibling_view = left_sibling_view_result.value();
                const auto minimum_key_count = static_cast<std::uint16_t>(left_sibling_view.capacity()/2);
                if(left_sibling_view.key_count() > minimum_key_count) {
                    borrowed_entry_index = static_cast<std::uint16_t>(left_sibling_view.key_count()-1);

                    auto borrowed_child_page_id_result = left_sibling_view.right_child_page_id_at(borrowed_entry_index);
                    if(!borrowed_child_page_id_result.ok()) {
                        return borrowed_child_page_id_result.status();
                    }

                    borrowed_child_page_id = borrowed_child_page_id_result.value();
                    left_can_lend = true;
                }
            }

            if(left_can_lend) {

                std::vector<std::byte> parent_separator_key;
                {
                    auto parent_page_handle_result = pager_->get_page(parent_page_id);
                    if(!parent_page_handle_result.ok()) {
                        return parent_page_handle_result.status();
                    }

                    const auto* parent_page = parent_page_handle_result.value().page();
                    auto parent_page_view_result = BTreeInternalPage<const std::byte>::open(parent_page->data());
                    if(!parent_page_view_result.ok()) {
                        return parent_page_view_result.status();
                    }

                    auto parent_separator_key_result = parent_page_view_result.value().key_at(
                        static_cast<std::uint16_t>(child_index-1)
                    );
                    if(!parent_separator_key_result.ok()) {
                        return parent_separator_key_result.status();
                    }

                    const auto parent_separator = parent_separator_key_result.value();
                    parent_separator_key.assign(parent_separator.begin(), parent_separator.end());
                }

                // Insert the parent separator before the child's old first child
                {
                    auto child_page_handle_result = pager_->get_page(child_page_id);
                    if(!child_page_handle_result.ok()) {
                        return child_page_handle_result.status();
                    }

                    auto child_page_result = child_page_handle_result.value().mutable_page();
                    if(!child_page_result.ok()) {
                        return child_page_result.status();
                    }

                    auto child_page_view_result = BTreeInternalPage<std::byte>::open(child_page_result.value()->data());
                    if(!child_page_view_result.ok()) {
                        return child_page_view_result.status();
                    }

                    auto& child_page_view = child_page_view_result.value();
                    const auto old_first_child_page_id = child_page_view.first_child_page_id();
                    const std::span<const std::byte> separator_key{ parent_separator_key.data(), parent_separator_key.size() };
                    auto insert_status = child_page_view.insert_entry(0, separator_key, old_first_child_page_id);
                    if(!insert_status.ok()) {
                        return insert_status;
                    }

                    child_page_view.set_first_child_page_id(borrowed_child_page_id);
                }

                // Remove the donated child from the left sibling
                {
                    auto left_sibling_handle_result = pager_->get_page(left_sibling_page_id);
                    if(!left_sibling_handle_result.ok()) {
                        return left_sibling_handle_result.status();
                    }

                    auto left_sibling_page_result = left_sibling_handle_result.value().mutable_page();
                    if(!left_sibling_page_result.ok()) {
                        return left_sibling_page_result.status();
                    }

                    auto left_sibling_view_result = BTreeInternalPage<std::byte>::open(left_sibling_page_result.value()->data());
                    if(!left_sibling_view_result.ok()) {
                        return left_sibling_view_result.status();
                    }

                    auto erase_status = left_sibling_view_result.value().erase_entry(borrowed_entry_index);
                    if(!erase_status.ok()) {
                        return erase_status;
                    }
                }

                // Update donated child's parent page id
                {
                    auto borrowed_child_handle_result = pager_->get_page(borrowed_child_page_id);
                    if(!borrowed_child_handle_result.ok()) {
                        return borrowed_child_handle_result.status();
                    }

                    auto borrowed_child_page_result = borrowed_child_handle_result.value().mutable_page();
                    if(!borrowed_child_page_result.ok()) {
                        return borrowed_child_page_result.status();
                    }

                    auto borrowed_child_page_view_result = BTreePage<std::byte>::open(
                        borrowed_child_page_result.value()->data()
                    );
                    if(!borrowed_child_page_view_result.ok()) {
                        return borrowed_child_page_view_result.status();
                    }

                    borrowed_child_page_view_result.value().set_parent_page_id(child_page_id);
                }

                auto refresh_status = refresh_child_separator(parent_page_id, child_index);
                if(!refresh_status.ok()) {
                    return refresh_status;
                }

                return core::Status::Ok();
            }
        }

        // Try to borrow the right sibling's first child
        if(right_sibling_page_id.is_valid()) {

            storage::PageId borrowed_child_page_id;
            storage::PageId new_right_first_child_page_id;
            bool right_can_lend = false;

            {
                auto right_sibling_handle_result = pager_->get_page(right_sibling_page_id);
                if(!right_sibling_handle_result.ok()) {
                    return right_sibling_handle_result.status();
                }

                const auto* right_sibling_page = right_sibling_handle_result.value().page();
                auto right_sibling_view_result = BTreeInternalPage<const std::byte>::open(right_sibling_page->data());
                if(!right_sibling_view_result.ok()) {
                    return right_sibling_view_result.status();
                }

                const auto& right_sibling_view = right_sibling_view_result.value();
                const auto minimum_key_count = static_cast<std::uint16_t>(right_sibling_view.capacity()/2);
                if(right_sibling_view.key_count() > minimum_key_count) {
                    borrowed_child_page_id = right_sibling_view.first_child_page_id();

                    auto new_right_first_child_page_id_result = right_sibling_view.right_child_page_id_at(0);
                    if(!new_right_first_child_page_id_result.ok()) {
                        return new_right_first_child_page_id_result.status();
                    }

                    new_right_first_child_page_id = new_right_first_child_page_id_result.value();
                    right_can_lend = true;
                }
            }

            if(right_can_lend) {

                std::vector<std::byte> parent_separator_key;
                {
                    auto parent_page_handle_result = pager_->get_page(parent_page_id);
                    if(!parent_page_handle_result.ok()) {
                        return parent_page_handle_result.status();
                    }

                    const auto* parent_page = parent_page_handle_result.value().page();
                    auto parent_page_view_result = BTreeInternalPage<const std::byte>::open(parent_page->data());
                    if(!parent_page_view_result.ok()) {
                        return parent_page_view_result.status();
                    }

                    auto parent_separator_key_result = parent_page_view_result.value().key_at(child_index);
                    if(!parent_separator_key_result.ok()) {
                        return parent_separator_key_result.status();
                    }

                    const auto parent_separator = parent_separator_key_result.value();
                    parent_separator_key.assign(parent_separator.begin(), parent_separator.end());
                }

                // Append the parent separator and the donated first child
                {
                    auto child_page_handle_result = pager_->get_page(child_page_id);
                    if(!child_page_handle_result.ok()) {
                        return child_page_handle_result.status();
                    }

                    auto child_page_result = child_page_handle_result.value().mutable_page();
                    if(!child_page_result.ok()) {
                        return child_page_result.status();
                    }

                    auto child_page_view_result = BTreeInternalPage<std::byte>::open(child_page_result.value()->data());
                    if(!child_page_view_result.ok()) {
                        return child_page_view_result.status();
                    }

                    auto& child_page_view = child_page_view_result.value();
                    const std::span<const std::byte> separator_key{ parent_separator_key.data(), parent_separator_key.size() };
                    auto insert_status = child_page_view.insert_entry(
                        child_page_view.key_count(),
                        separator_key,
                        borrowed_child_page_id
                    );
                    if(!insert_status.ok()) {
                        return insert_status;
                    }
                }

                // Remove the donated first child from the right sibling
                {
                    auto right_sibling_handle_result = pager_->get_page(right_sibling_page_id);
                    if(!right_sibling_handle_result.ok()) {
                        return right_sibling_handle_result.status();
                    }

                    auto right_sibling_page_result = right_sibling_handle_result.value().mutable_page();
                    if(!right_sibling_page_result.ok()) {
                        return right_sibling_page_result.status();
                    }

                    auto right_sibling_view_result = BTreeInternalPage<std::byte>::open(right_sibling_page_result.value()->data());
                    if(!right_sibling_view_result.ok()) {
                        return right_sibling_view_result.status();
                    }

                    auto& right_sibling_view = right_sibling_view_result.value();
                    right_sibling_view.set_first_child_page_id(new_right_first_child_page_id);

                    auto erase_status = right_sibling_view.erase_entry(0);
                    if(!erase_status.ok()) {
                        return erase_status;
                    }
                }

                // Update donated child's parent page id
                {
                    auto borrowed_child_handle_result = pager_->get_page(borrowed_child_page_id);
                    if(!borrowed_child_handle_result.ok()) {
                        return borrowed_child_handle_result.status();
                    }

                    auto borrowed_child_page_result = borrowed_child_handle_result.value().mutable_page();
                    if(!borrowed_child_page_result.ok()) {
                        return borrowed_child_page_result.status();
                    }

                    auto borrowed_child_page_view_result = BTreePage<std::byte>::open(
                        borrowed_child_page_result.value()->data()
                    );
                    if(!borrowed_child_page_view_result.ok()) {
                        return borrowed_child_page_view_result.status();
                    }

                    borrowed_child_page_view_result.value().set_parent_page_id(child_page_id);
                }

                auto refresh_status = refresh_child_separator(
                    parent_page_id,
                    static_cast<std::uint16_t>(child_index+1)
                );
                if(!refresh_status.ok()) {
                    return refresh_status;
                }

                return core::Status::Ok();
            }
        }

        // Try to merge the child into the left sibling
        if(left_sibling_page_id.is_valid()) {

            std::vector<std::byte> separator_key;

            {
                auto parent_page_handle_result = pager_->get_page(parent_page_id);
                if(!parent_page_handle_result.ok()) {
                    return parent_page_handle_result.status();
                }

                const auto* parent_page = parent_page_handle_result.value().page();
                auto parent_page_view_result = BTreeInternalPage<const std::byte>::open(parent_page->data());
                if(!parent_page_view_result.ok()) {
                    return parent_page_view_result.status();
                }

                auto separator_key_result = parent_page_view_result.value().key_at(
                    static_cast<std::uint16_t>(child_index-1)
                );
                if(!separator_key_result.ok()) {
                    return separator_key_result.status();
                }

                const auto separator = separator_key_result.value();
                separator_key.assign(separator.begin(), separator.end());
            }

            const std::span<const std::byte> separator{ separator_key.data(), separator_key.size() };
            auto merge_status = merge_adjacent_internals(left_sibling_page_id, separator, child_page_id);
            if(!merge_status.ok()) {
                return merge_status;
            }

            // Remove the entry from the parent pointing to the merged-away right child
            {
                auto parent_page_handle_result = pager_->get_page(parent_page_id);
                if(!parent_page_handle_result.ok()) {
                    return parent_page_handle_result.status();
                }

                auto parent_page_result = parent_page_handle_result.value().mutable_page();
                if(!parent_page_result.ok()) {
                    return parent_page_result.status();
                }

                auto parent_page_view_result = BTreeInternalPage<std::byte>::open(parent_page_result.value()->data());
                if(!parent_page_view_result.ok()) {
                    return parent_page_view_result.status();
                }

                auto erase_status = parent_page_view_result.value().erase_entry(
                    static_cast<std::uint16_t>(child_index-1)
                );
                if(!erase_status.ok()) {
                    return erase_status;
                }
            }

            return core::Status::Ok();
        }

        // Merge the right sibling into the child
        if(right_sibling_page_id.is_valid()) {

            std::vector<std::byte> separator_key;
            {
                auto parent_page_handle_result = pager_->get_page(parent_page_id);
                if(!parent_page_handle_result.ok()) {
                    return parent_page_handle_result.status();
                }

                const auto* parent_page = parent_page_handle_result.value().page();
                auto parent_page_view_result = BTreeInternalPage<const std::byte>::open(parent_page->data());
                if(!parent_page_view_result.ok()) {
                    return parent_page_view_result.status();
                }

                auto separator_key_result = parent_page_view_result.value().key_at(child_index);
                if(!separator_key_result.ok()) {
                    return separator_key_result.status();
                }

                const auto separator = separator_key_result.value();
                separator_key.assign(separator.begin(), separator.end());
            }

            const std::span<const std::byte> separator{ separator_key.data(), separator_key.size() };
            auto merge_status = merge_adjacent_internals(child_page_id, separator, right_sibling_page_id);
            if(!merge_status.ok()) {
                return merge_status;
            }

            // Remove the entry from the parent pointing to the merged-away right child
            {
                auto parent_page_handle_result = pager_->get_page(parent_page_id);
                if(!parent_page_handle_result.ok()) {
                    return parent_page_handle_result.status();
                }

                auto parent_page_result = parent_page_handle_result.value().mutable_page();
                if(!parent_page_result.ok()) {
                    return parent_page_result.status();
                }

                auto parent_page_view_result = BTreeInternalPage<std::byte>::open(parent_page_result.value()->data());
                if(!parent_page_view_result.ok()) {
                    return parent_page_view_result.status();
                }

                auto erase_status = parent_page_view_result.value().erase_entry(child_index);
                if(!erase_status.ok()) {
                    return erase_status;
                }
            }

            return core::Status::Ok();
        }

        return core::Status::Corruption("Cannot rebalance B+ tree internal child after erase: child has no siblings");

    }

    core::Status BTree::refresh_child_separator(
        storage::PageId internal_page_id,
        std::uint16_t child_index
    ) {

        if(child_index == 0) {
            return core::Status::Ok();
        }

        storage::PageId current_page_id;
        {
            auto internal_page_handle_result = pager_->get_page(internal_page_id);
            if(!internal_page_handle_result.ok()) {
                return internal_page_handle_result.status();
            }

            const auto* internal_page = internal_page_handle_result.value().page();
            auto internal_page_view_result = BTreeInternalPage<const std::byte>::open(internal_page->data());
            if(!internal_page_view_result.ok()) {
                return internal_page_view_result.status();
            }

            auto child_page_id_result = internal_page_view_result.value().right_child_page_id_at(
                static_cast<std::uint16_t>(child_index-1)
            );
            if(!child_page_id_result.ok()) {
                return child_page_id_result.status();
            }

            current_page_id = child_page_id_result.value();
        }

        std::vector<std::byte> first_key;

        // Get min key in the subtree
        while(true) {

            BTreePageKind page_kind;
            {
                auto page_handle_result = pager_->get_page(current_page_id);
                if(!page_handle_result.ok()) {
                    return page_handle_result.status();
                }

                const auto* page = page_handle_result.value().page();
                auto page_view_result = BTreePage<const std::byte>::open(page->data());
                if(!page_view_result.ok()) {
                    return page_view_result.status();
                }

                page_kind = page_view_result.value().kind();
                if(page_kind == BTreePageKind::Internal) {
                    auto internal_page_view_result = BTreeInternalPage<const std::byte>::open(page->data());
                    if(!internal_page_view_result.ok()) {
                        return internal_page_view_result.status();
                    }

                    current_page_id = internal_page_view_result.value().first_child_page_id();
                } else {
                    auto leaf_page_view_result = BTreeLeafPage<const std::byte>::open(page->data());
                    if(!leaf_page_view_result.ok()) {
                        return leaf_page_view_result.status();
                    }

                    auto first_key_result = leaf_page_view_result.value().key_at(0);
                    if(!first_key_result.ok()) {
                        return first_key_result.status();
                    }

                    const auto key = first_key_result.value();
                    first_key.assign(key.begin(), key.end());
                }
            }

            if(page_kind == BTreePageKind::Leaf) {
                break;
            }
        }

        auto internal_page_handle_result = pager_->get_page(internal_page_id);
        if(!internal_page_handle_result.ok()) {
            return internal_page_handle_result.status();
        }

        auto internal_page_result = internal_page_handle_result.value().mutable_page();
        if(!internal_page_result.ok()) {
            return internal_page_result.status();
        }

        auto internal_page_view_result = BTreeInternalPage<std::byte>::open(internal_page_result.value()->data());
        if(!internal_page_view_result.ok()) {
            return internal_page_view_result.status();
        }

        const std::span<const std::byte> key{ first_key.data(), first_key.size() };

        return internal_page_view_result.value().set_key_at(static_cast<std::uint16_t>(child_index-1), key);

    }

    core::Result<bool> BTree::page_is_underfull(storage::PageId page_id) const {

        auto page_handle_result = pager_->get_page(page_id);
        if(!page_handle_result.ok()) {
            return page_handle_result.status();
        }

        const auto* page = page_handle_result.value().page();
        auto page_view_result = BTreePage<const std::byte>::open(page->data());
        if(!page_view_result.ok()) {
            return page_view_result.status();
        }

        const auto& page_view = page_view_result.value();
        if(page_view.is_root()) {
            return false;
        }

        if(page_view.kind() == BTreePageKind::Leaf) {

            auto leaf_page_view_result = BTreeLeafPage<const std::byte>::open(page->data());
            if(!leaf_page_view_result.ok()) {
                return leaf_page_view_result.status();
            }

            const auto& leaf_page_view = leaf_page_view_result.value();
            const auto minimum_key_count = static_cast<std::uint16_t>((leaf_page_view.capacity()+1)/2);

            return leaf_page_view.key_count() < minimum_key_count;
            
        }

        auto internal_page_view_result = BTreeInternalPage<const std::byte>::open(page->data());
        if(!internal_page_view_result.ok()) {
            return internal_page_view_result.status();
        }

        const auto& internal_page_view = internal_page_view_result.value();
        const auto minimum_key_count = static_cast<std::uint16_t>(internal_page_view.capacity()/2);

        return internal_page_view.key_count() < minimum_key_count;

    }

    core::Status BTree::merge_adjacent_leaves(
        storage::PageId left_page_id,
        storage::PageId right_page_id
    ) {

        storage::PageId next_leaf_page_id;

        // Append the right leaf entries into the left leaf
        {
            auto right_page_handle_result = pager_->get_page(right_page_id);
            if(!right_page_handle_result.ok()) {
                return right_page_handle_result.status();
            }

            const auto* right_page = right_page_handle_result.value().page();
            auto right_page_view_result = BTreeLeafPage<const std::byte>::open(right_page->data());
            if(!right_page_view_result.ok()) {
                return right_page_view_result.status();
            }

            const auto& right_page_view = right_page_view_result.value();
            next_leaf_page_id = right_page_view.next_leaf_page_id();

            auto left_page_handle_result = pager_->get_page(left_page_id);
            if(!left_page_handle_result.ok()) {
                return left_page_handle_result.status();
            }

            auto left_page_result = left_page_handle_result.value().mutable_page();
            if(!left_page_result.ok()) {
                return left_page_result.status();
            }

            auto left_page_view_result = BTreeLeafPage<std::byte>::open(left_page_result.value()->data());
            if(!left_page_view_result.ok()) {
                return left_page_view_result.status();
            }

            auto& left_page_view = left_page_view_result.value();
            const auto left_key_count = left_page_view.key_count();

            for(std::uint16_t entry_index = 0; entry_index < right_page_view.key_count(); entry_index++) {
                auto entry_result = right_page_view.entry_at(entry_index);
                if(!entry_result.ok()) {
                    return entry_result.status();
                }

                const auto entry = entry_result.value();
                auto insert_status = left_page_view.insert_entry(
                    static_cast<std::uint16_t>(left_key_count+entry_index),
                    entry.subspan(0, key_size_),
                    entry.subspan(key_size_, value_size_)
                );
                if(!insert_status.ok()) {
                    return insert_status;
                }
            }

            left_page_view.set_next_leaf_page_id(next_leaf_page_id);
        }

        // Update the next leaf backward link
        if(next_leaf_page_id.is_valid()) {

            auto next_leaf_handle_result = pager_->get_page(next_leaf_page_id);
            if(!next_leaf_handle_result.ok()) {
                return next_leaf_handle_result.status();
            }

            auto next_leaf_page_result = next_leaf_handle_result.value().mutable_page();
            if(!next_leaf_page_result.ok()) {
                return next_leaf_page_result.status();
            }

            auto next_leaf_view_result = BTreeLeafPage<std::byte>::open(next_leaf_page_result.value()->data());
            if(!next_leaf_view_result.ok()) {
                return next_leaf_view_result.status();
            }

            next_leaf_view_result.value().set_previous_leaf_page_id(left_page_id);

        }

        return core::Status::Ok();

    }

    core::Status BTree::merge_adjacent_internals(
        storage::PageId left_page_id,
        std::span<const std::byte> separator_key,
        storage::PageId right_page_id
    ) {

        auto right_page_handle_result = pager_->get_page(right_page_id);
        if(!right_page_handle_result.ok()) {
            return right_page_handle_result.status();
        }

        const auto* right_page = right_page_handle_result.value().page();
        auto right_page_view_result = BTreeInternalPage<const std::byte>::open(right_page->data());
        if(!right_page_view_result.ok()) {
            return right_page_view_result.status();
        }

        const auto& right_page_view = right_page_view_result.value();
        const auto right_first_child_page_id = right_page_view.first_child_page_id();

        // Append the parent separator and the right page's entries into the left page
        {
            auto left_page_handle_result = pager_->get_page(left_page_id);
            if(!left_page_handle_result.ok()) {
                return left_page_handle_result.status();
            }

            auto left_page_result = left_page_handle_result.value().mutable_page();
            if(!left_page_result.ok()) {
                return left_page_result.status();
            }

            auto left_page_view_result = BTreeInternalPage<std::byte>::open(left_page_result.value()->data());
            if(!left_page_view_result.ok()) {
                return left_page_view_result.status();
            }

            auto& left_page_view = left_page_view_result.value();
            const auto left_key_count = left_page_view.key_count();

            auto insert_status = left_page_view.insert_entry(
                left_key_count,
                separator_key,
                right_first_child_page_id
            );
            if(!insert_status.ok()) {
                return insert_status;
            }

            for(std::uint16_t entry_index = 0; entry_index < right_page_view.key_count(); entry_index++) {
                auto key_result = right_page_view.key_at(entry_index);
                if(!key_result.ok()) {
                    return key_result.status();
                }

                auto right_child_page_id_result = right_page_view.right_child_page_id_at(entry_index);
                if(!right_child_page_id_result.ok()) {
                    return right_child_page_id_result.status();
                }

                insert_status = left_page_view.insert_entry(
                    static_cast<std::uint16_t>(left_key_count+1+entry_index),
                    key_result.value(),
                    right_child_page_id_result.value()
                );
                if(!insert_status.ok()) {
                    return insert_status;
                }
            }
            
        }

        const auto update_child_parent_page_id = [&](storage::PageId child_page_id) -> core::Status {

            auto child_page_handle_result = pager_->get_page(child_page_id);
            if(!child_page_handle_result.ok()) {
                return child_page_handle_result.status();
            }

            auto child_page_result = child_page_handle_result.value().mutable_page();
            if(!child_page_result.ok()) {
                return child_page_result.status();
            }

            auto child_page_view_result = BTreePage<std::byte>::open(child_page_result.value()->data());
            if(!child_page_view_result.ok()) {
                return child_page_view_result.status();
            }

            child_page_view_result.value().set_parent_page_id(left_page_id);
            return core::Status::Ok();

        };

        auto update_child_status = update_child_parent_page_id(right_first_child_page_id);
        if(!update_child_status.ok()) {
            return update_child_status;
        }

        for(std::uint16_t entry_index = 0; entry_index < right_page_view.key_count(); entry_index++) {
            auto right_child_page_id_result = right_page_view.right_child_page_id_at(entry_index);
            if(!right_child_page_id_result.ok()) {
                return right_child_page_id_result.status();
            }

            update_child_status = update_child_parent_page_id(right_child_page_id_result.value());
            if(!update_child_status.ok()) {
                return update_child_status;
            }
        }
        
        return core::Status::Ok();

    }

    core::Result<std::optional<SplitResult>> BTree::insert_into_leaf(
        storage::PageId page_id,
        std::span<const std::byte> key,
        std::span<const std::byte> value
    ) {

        std::uint16_t insertion_pos;
        std::uint16_t stored_key_count;

        // Reject duplicate keys
        {
            auto page_handle_result = pager_->get_page(page_id);
            if(!page_handle_result.ok()) {
                return page_handle_result.status();
            }

            auto& page_handle = page_handle_result.value();
            const auto* page = page_handle.page();

            auto page_view_result = BTreeLeafPage<const std::byte>::open(page->data());
            if(!page_view_result.ok()) {
                return page_view_result.status();
            }

            auto& page_view = page_view_result.value();

            auto insertion_pos_result = page_view.find_insertion_position(key);
            if(!insertion_pos_result.ok()) {
                return insertion_pos_result.status();
            }

            insertion_pos = insertion_pos_result.value();
            stored_key_count = page_view.key_count();

            if(insertion_pos < stored_key_count) {
                auto next_key_result = page_view.key_at(insertion_pos);
                if(!next_key_result.ok()) {
                    return next_key_result.status();
                }

                if(std::memcmp(key.data(), next_key_result.value().data(), key_size_) == 0) {
                    return core::Status::ConstraintViolation("Cannot insert key/value into B+ tree: key is duplicated");
                }
            }
        }

        auto page_handle_result = pager_->get_page(page_id);
        if(!page_handle_result.ok()) {
            return page_handle_result.status();
        }

        auto& page_handle = page_handle_result.value();

        const auto page_result = page_handle.mutable_page();
        if(!page_result.ok()) {
            return page_result.status();
        }

        auto& page = page_result.value();

        auto page_view_result = BTreeLeafPage<std::byte>::open(page->data());
        if(!page_view_result.ok()) {
            return page_view_result.status();
        }

        auto& page_view = page_view_result.value();
        
        if(page_view.key_count() < page_view.capacity()) {

            auto insertion_status = page_view.insert_entry(insertion_pos, key, value);
            if(!insertion_status.ok()) {
                return insertion_status;
            }

            return std::optional<SplitResult>{};

        }

        const auto parent_page_id = page_view.parent_page_id();
        const auto old_next_leaf_page_id = page_view.next_leaf_page_id();

        // Gather the old entries plus the new entry in sorted order
        std::vector<std::vector<std::byte>> entries;
        entries.reserve(static_cast<std::size_t>(stored_key_count)+1);

        for(std::uint16_t entry_index = 0; entry_index <= stored_key_count; entry_index++) {

            if(entry_index == insertion_pos) {
                std::vector<std::byte> inserted_entry;
                inserted_entry.reserve(page_view.entry_size());
                inserted_entry.insert(inserted_entry.end(), key.begin(), key.end());
                inserted_entry.insert(inserted_entry.end(), value.begin(), value.end());
                entries.push_back(std::move(inserted_entry));
            }

            if(entry_index == stored_key_count) continue;

            auto entry_result = page_view.entry_at(entry_index);
            if(!entry_result.ok()) {
                return entry_result.status();
            }

            const auto entry = entry_result.value();
            entries.emplace_back(entry.begin(), entry.end());

        }

        // Create the right leaf that will receive the right half
        auto right_leaf_handle_result = pager_->new_page();
        if(!right_leaf_handle_result.ok()) {
            return right_leaf_handle_result.status();
        }

        auto& right_leaf_handle = right_leaf_handle_result.value();

        const auto right_leaf_page_result = right_leaf_handle.mutable_page();
        if(!right_leaf_page_result.ok()) {
            return right_leaf_page_result.status();
        }

        auto& right_leaf_page = right_leaf_page_result.value();
        const auto right_leaf_page_id = right_leaf_page->id();

        const auto init_leaf_status = initialize_leaf(right_leaf_page->data(), key_size_, value_size_);
        if(!init_leaf_status.ok()) {
            return init_leaf_status;
        }

        auto right_leaf_page_view_result = BTreeLeafPage<std::byte>::open(right_leaf_page->data());
        if(!right_leaf_page_view_result.ok()) {
            return right_leaf_page_view_result.status();
        }

        auto& right_leaf_page_view = right_leaf_page_view_result.value();

        const auto total_key_count = static_cast<std::uint16_t>(entries.size());
        const auto left_key_count = static_cast<std::uint16_t>(total_key_count/2);

        // Rebuild the left leaf from the left half
        auto clear_left_status = page_view.set_key_count(0);
        if(!clear_left_status.ok()) {
            return clear_left_status;
        }

        for(std::uint16_t entry_index = 0; entry_index < left_key_count; entry_index++) {
            const std::span<const std::byte> entry{ entries[entry_index].data(), entries[entry_index].size() };
            const auto insert_status = page_view.insert_entry(
                entry_index,
                entry.subspan(0, key_size_),
                entry.subspan(key_size_, value_size_)
            );

            if(!insert_status.ok()) {
                return insert_status;
            }
        }

        // Build the right leaf from the right half
        for(std::uint16_t entry_index = left_key_count; entry_index < total_key_count; entry_index++) {
            const std::span<const std::byte> entry{ entries[entry_index].data(), entries[entry_index].size() };
            const auto right_entry_index = static_cast<std::uint16_t>(entry_index-left_key_count);
            const auto insert_status = right_leaf_page_view.insert_entry(
                right_entry_index,
                entry.subspan(0, key_size_),
                entry.subspan(key_size_, value_size_)
            );

            if(!insert_status.ok()) {
                return insert_status;
            }
        }

        page_view.set_next_leaf_page_id(right_leaf_page_id);

        right_leaf_page_view.set_parent_page_id(parent_page_id);
        right_leaf_page_view.set_previous_leaf_page_id(page_id);
        right_leaf_page_view.set_next_leaf_page_id(old_next_leaf_page_id);

        if(old_next_leaf_page_id.is_valid()) {

            auto next_leaf_handle_result = pager_->get_page(old_next_leaf_page_id);
            if(!next_leaf_handle_result.ok()) {
                return next_leaf_handle_result.status();
            }

            auto& next_leaf_handle = next_leaf_handle_result.value();

            const auto next_leaf_page_result = next_leaf_handle.mutable_page();
            if(!next_leaf_page_result.ok()) {
                return next_leaf_page_result.status();
            }

            auto& next_leaf_page = next_leaf_page_result.value();

            auto next_leaf_page_view_result = BTreeLeafPage<std::byte>::open(next_leaf_page->data());
            if(!next_leaf_page_view_result.ok()) {
                return next_leaf_page_view_result.status();
            }

            auto& next_leaf_page_view = next_leaf_page_view_result.value();

            next_leaf_page_view.set_previous_leaf_page_id(right_leaf_page_id);

        }

        return std::optional<SplitResult>{{
            std::vector<std::byte>{
                entries[left_key_count].begin(),
                entries[left_key_count].begin()+key_size_
            },
            right_leaf_page_id
        }};

    }

    core::Result<std::optional<SplitResult>> BTree::insert_into_internal(
        storage::PageId page_id,
        std::span<const std::byte> key,
        std::span<const std::byte> value
    ) {

        storage::PageId child_page_id;

        // Avoid keeping pages pinned during recursion
        {
            auto page_handle_result = pager_->get_page(page_id);
            if(!page_handle_result.ok()) {
                return page_handle_result.status();
            }

            auto& page_handle = page_handle_result.value();
            const auto* page = page_handle.page();

            auto page_view_result = BTreeInternalPage<const std::byte>::open(page->data());
            if(!page_view_result.ok()) {
                return page_view_result.status();
            }

            auto& page_view = page_view_result.value();

            auto child_page_id_result = page_view.child_page_id_for_key(key);
            if(!child_page_id_result.ok()) {
                return child_page_id_result.status();
            }

            child_page_id = child_page_id_result.value();
        }

        auto child_split_result = insert_into_subtree(child_page_id, key, value);
        if(!child_split_result.ok()) {
            return child_split_result.status();
        }

        const auto& child_split = child_split_result.value();
        if(!child_split.has_value()) {
            return std::optional<SplitResult>{};
        }

        auto page_handle_result = pager_->get_page(page_id);
        if(!page_handle_result.ok()) {
            return page_handle_result.status();
        }

        auto& page_handle = page_handle_result.value();

        const auto page_result = page_handle.mutable_page();
        if(!page_result.ok()) {
            return page_result.status();
        }

        auto& page = page_result.value();

        auto page_view_result = BTreeInternalPage<std::byte>::open(page->data());
        if(!page_view_result.ok()) {
            return page_view_result.status();
        }

        auto& page_view = page_view_result.value();

        if(page_view.key_count() < page_view.capacity()) {

            auto insertion_pos_result = page_view.find_insertion_position(child_split->separator_key);
            if(!insertion_pos_result.ok()) {
                return insertion_pos_result.status();
            }

            const auto insertion_pos = insertion_pos_result.value();

            auto insertion_status = page_view.insert_entry(
                insertion_pos,
                child_split->separator_key,
                child_split->right_child_page_id
            );
            if(!insertion_status.ok()) {
                return insertion_status;
            }

            return std::optional<SplitResult>{};

        }

        auto insertion_pos_result = page_view.find_insertion_position(child_split->separator_key);
        if(!insertion_pos_result.ok()) {
            return insertion_pos_result.status();
        }

        const auto insertion_pos = insertion_pos_result.value();
        const auto stored_key_count = page_view.key_count();
        const auto parent_page_id = page_view.parent_page_id();

        // Gather the old entries plus the child split entry in sorted order
        std::vector<std::pair<std::vector<std::byte>, storage::PageId>> entries;
        entries.reserve(static_cast<std::size_t>(stored_key_count)+1);

        for(std::uint16_t entry_index = 0; entry_index <= stored_key_count; entry_index++) {

            if(entry_index == insertion_pos) {
                entries.push_back(std::pair{
                    child_split->separator_key,
                    child_split->right_child_page_id
                });
            }

            if(entry_index == stored_key_count) continue;

            auto entry_key_result = page_view.key_at(entry_index);
            if(!entry_key_result.ok()) {
                return entry_key_result.status();
            }

            auto entry_right_child_page_id_result = page_view.right_child_page_id_at(entry_index);
            if(!entry_right_child_page_id_result.ok()) {
                return entry_right_child_page_id_result.status();
            }

            const auto entry_key = entry_key_result.value();
            entries.push_back(std::pair{
                std::vector<std::byte>{ entry_key.begin(), entry_key.end() },
                entry_right_child_page_id_result.value()
            });

        }

        // Create the right internal page that will receive entries after the promoted separator
        auto right_internal_handle_result = pager_->new_page();
        if(!right_internal_handle_result.ok()) {
            return right_internal_handle_result.status();
        }

        auto& right_internal_handle = right_internal_handle_result.value();

        const auto right_internal_page_result = right_internal_handle.mutable_page();
        if(!right_internal_page_result.ok()) {
            return right_internal_page_result.status();
        }

        auto& right_internal_page = right_internal_page_result.value();
        const auto right_internal_page_id = right_internal_page->id();

        const auto init_internal_status = initialize_internal(right_internal_page->data(), key_size_, value_size_);
        if(!init_internal_status.ok()) {
            return init_internal_status;
        }

        auto right_internal_page_view_result = BTreeInternalPage<std::byte>::open(right_internal_page->data());
        if(!right_internal_page_view_result.ok()) {
            return right_internal_page_view_result.status();
        }

        auto& right_internal_page_view = right_internal_page_view_result.value();

        const auto total_key_count = static_cast<std::uint16_t>(entries.size());
        const auto middle_index = static_cast<std::uint16_t>(total_key_count/2);
        const auto& promoted_entry = entries[middle_index];

        // Rebuild the left internal page from entries before the promoted separator
        auto clear_left_status = page_view.set_key_count(0);
        if(!clear_left_status.ok()) {
            return clear_left_status;
        }

        for(std::uint16_t entry_index = 0; entry_index < middle_index; entry_index++) {
            const auto insert_status = page_view.insert_entry(
                entry_index,
                entries[entry_index].first,
                entries[entry_index].second
            );

            if(!insert_status.ok()) {
                return insert_status;
            }
        }

        // Build the right internal page from entries after the promoted separator
        right_internal_page_view.set_parent_page_id(parent_page_id);
        right_internal_page_view.set_first_child_page_id(promoted_entry.second);

        for(std::uint16_t entry_index = static_cast<std::uint16_t>(middle_index+1); entry_index < total_key_count; entry_index++) {
            const auto right_entry_index = static_cast<std::uint16_t>(entry_index-middle_index-1);
            const auto insert_status = right_internal_page_view.insert_entry(
                right_entry_index,
                entries[entry_index].first,
                entries[entry_index].second
            );

            if(!insert_status.ok()) {
                return insert_status;
            }
        }

        const auto update_child_parent_page_id = [&](storage::PageId child_page_id) -> core::Status {

            auto child_handle_result = pager_->get_page(child_page_id);
            if(!child_handle_result.ok()) {
                return child_handle_result.status();
            }

            auto& child_handle = child_handle_result.value();

            const auto child_page_result = child_handle.mutable_page();
            if(!child_page_result.ok()) {
                return child_page_result.status();
            }

            auto& child_page = child_page_result.value();

            auto child_page_view_result = BTreePage<std::byte>::open(child_page->data());
            if(!child_page_view_result.ok()) {
                return child_page_view_result.status();
            }

            auto& child_page_view = child_page_view_result.value();

            child_page_view.set_parent_page_id(right_internal_page_id);

            return core::Status::Ok();

        };

        auto update_child_status = update_child_parent_page_id(promoted_entry.second);
        if(!update_child_status.ok()) {
            return update_child_status;
        }

        for(std::uint16_t entry_index = static_cast<std::uint16_t>(middle_index+1); entry_index < total_key_count; entry_index++) {
            update_child_status = update_child_parent_page_id(entries[entry_index].second);
            if(!update_child_status.ok()) {
                return update_child_status;
            }
        }

        return std::optional<SplitResult>{{
            promoted_entry.first,
            right_internal_page_id
        }};

    }

}
