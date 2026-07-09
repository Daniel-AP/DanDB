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
