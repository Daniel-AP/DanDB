#pragma once

#include <dandb/btree/BTreeCursor.h>
#include <dandb/core/Result.h>
#include <dandb/storage/PageId.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dandb::storage {
    class Pager;
}

namespace dandb::btree {

    struct SplitResult {
        std::vector<std::byte> separator_key;
        storage::PageId right_child_page_id;
    };

    class BTree {
        public:
            static core::Result<BTree> create_new(
                storage::Pager& pager,
                std::uint16_t key_size,
                std::uint16_t value_size
            );

            static core::Result<BTree> open_existing(
                storage::Pager& pager,
                storage::PageId root_page_id,
                std::uint16_t key_size,
                std::uint16_t value_size
            );

            core::Result<std::vector<std::byte>> find(std::span<const std::byte> key) const;
            core::Status insert(std::span<const std::byte> key, std::span<const std::byte> value);
            core::Status erase(std::span<const std::byte> key);
            core::Result<BTreeCursor> scan() const;
            core::Status validate() const;

            storage::PageId root_page_id() const;
            std::uint16_t key_size() const;
            std::uint16_t value_size() const;

        private:
            explicit BTree(
                storage::Pager& pager,
                storage::PageId root_page_id,
                std::uint16_t key_size,
                std::uint16_t value_size
            );

            core::Result<std::optional<SplitResult>> insert_into_subtree(
                storage::PageId page_id,
                std::span<const std::byte> key,
                std::span<const std::byte> value
            );

            core::Result<std::optional<SplitResult>> insert_into_leaf(
                storage::PageId page_id,
                std::span<const std::byte> key,
                std::span<const std::byte> value
            );

            core::Result<std::optional<SplitResult>> insert_into_internal(
                storage::PageId page_id,
                std::span<const std::byte> key,
                std::span<const std::byte> value
            );

            core::Result<bool> erase_from_subtree(
                storage::PageId page_id,
                std::span<const std::byte> key
            );

            core::Result<bool> erase_from_leaf(
                storage::PageId page_id,
                std::span<const std::byte> key
            );

            core::Result<bool> erase_from_internal(
                storage::PageId page_id,
                std::span<const std::byte> key
            );

            core::Status rebalance_child_after_erase(
                storage::PageId parent_page_id,
                std::uint16_t child_index
            );

            core::Status rebalance_leaf_child_after_erase(
                storage::PageId parent_page_id,
                std::uint16_t child_index
            );

            core::Status rebalance_internal_child_after_erase(
                storage::PageId parent_page_id,
                std::uint16_t child_index
            );

            core::Status shrink_root_after_erase();

            core::Result<storage::PageId> child_page_id_at(
                storage::PageId internal_page_id,
                std::uint16_t child_index
            ) const;

            core::Status refresh_child_separator(
                storage::PageId internal_page_id,
                std::uint16_t child_index
            );

            core::Result<std::vector<std::byte>> first_key_in_subtree(
                storage::PageId page_id
            ) const;

            core::Result<bool> page_is_underfull(
                storage::PageId page_id
            ) const;

            storage::Pager* pager_;
            storage::PageId root_page_id_;
            std::uint16_t key_size_;
            std::uint16_t value_size_;
    };

}
