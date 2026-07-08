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

    storage::PageId BTree::root_page_id() const {
        return root_page_id_;
    }

    std::uint16_t BTree::key_size() const {
        return key_size_;
    }

    std::uint16_t BTree::value_size() const {
        return value_size_;
    }

    core::Status BTree::insert(std::span<const std::byte> key, std::span<const std::byte> value) {

        if(key.size() != key_size_) {
            return core::Status::InvalidArgument("Cannot insert B+ tree key: key size is invalid");
        }

        if(value.size() != value_size_) {
            return core::Status::InvalidArgument("Cannot insert B+ tree key: value size is invalid");
        }

        auto current_page_id = root_page_id_;

        while(true) {

            auto page_handle_result = pager_->get_page(current_page_id);
            if(!page_handle_result.ok()) {
                return page_handle_result.status();
            }

            auto& page_handle = page_handle_result.value();
            const auto* page = page_handle.page();
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
                if(position < leaf_page.key_count()) {

                    auto stored_key_result = leaf_page.key_at(position);
                    if(!stored_key_result.ok()) {
                        return stored_key_result.status();
                    }

                    const auto stored_key = stored_key_result.value();
                    if(std::memcmp(stored_key.data(), key.data(), key_size_) == 0) {
                        return core::Status::ConstraintViolation("Cannot insert B+ tree key: key already exists");
                    }

                }

                if(leaf_page.key_count() >= leaf_page.capacity()) {
                    // TODO: handle leaf split
                    return core::Status::InvalidArgument("Cannot insert B+ tree key: leaf split is not implemented");
                }

                auto mutable_page_result = page_handle.mutable_page();
                if(!mutable_page_result.ok()) {
                    return mutable_page_result.status();
                }

                auto mutable_leaf_page_result = BTreeLeafPage<std::byte>::open(mutable_page_result.value()->data());
                if(!mutable_leaf_page_result.ok()) {
                    return mutable_leaf_page_result.status();
                }

                return mutable_leaf_page_result.value().insert_entry(position, key, value);

            }

            auto internal_page_result = BTreeInternalPage<const std::byte>::open(page->data());
            if(!internal_page_result.ok()) {
                return internal_page_result.status();
            }

            const auto& internal_page = internal_page_result.value();
            std::uint16_t left = 0;
            std::uint16_t right = internal_page.key_count();

            while(left < right) {

                const auto mid = static_cast<std::uint16_t>(left+(right-left)/2);
                auto separator_key_result = internal_page.key_at(mid);
                if(!separator_key_result.ok()) {
                    return separator_key_result.status();
                }

                const auto separator_key = separator_key_result.value();
                if(std::memcmp(separator_key.data(), key.data(), key_size_) <= 0) {
                    left = static_cast<std::uint16_t>(mid+1);
                } else {
                    right = mid;
                }

            }

            if(left == 0) {
                current_page_id = internal_page.first_child_page_id();
            } else {

                auto child_page_id_result = internal_page.right_child_page_id_at(static_cast<std::uint16_t>(left-1));
                if(!child_page_id_result.ok()) {
                    return child_page_id_result.status();
                }

                current_page_id = child_page_id_result.value();

            }

        }

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

            // It's an internal leaf

            auto internal_page_result = BTreeInternalPage<const std::byte>::open(page->data());
            if(!internal_page_result.ok()) {
                return internal_page_result.status();
            }

            const auto& internal_page = internal_page_result.value();
            std::uint16_t left = 0;
            std::uint16_t right = internal_page.key_count();

            while(left < right) {

                const auto mid = static_cast<std::uint16_t>(left+(right-left)/2);
                auto separator_key_result = internal_page.key_at(mid);
                if(!separator_key_result.ok()) {
                    return separator_key_result.status();
                }

                const auto separator_key = separator_key_result.value();
                if(std::memcmp(separator_key.data(), key.data(), key_size_) <= 0) {
                    left = static_cast<std::uint16_t>(mid+1);
                } else {
                    right = mid;
                }

            }

            if(left == 0) {
                current_page_id = internal_page.first_child_page_id();
            } else {

                auto child_page_id_result = internal_page.right_child_page_id_at(static_cast<std::uint16_t>(left-1));
                if(!child_page_id_result.ok()) {
                    return child_page_id_result.status();
                }

                current_page_id = child_page_id_result.value();

            }

        }

    }

}
