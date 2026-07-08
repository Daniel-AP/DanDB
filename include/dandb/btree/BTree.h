#pragma once

#include <dandb/core/Result.h>
#include <dandb/storage/PageId.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dandb::storage {
    class Pager;
}

namespace dandb::btree {

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

            storage::Pager* pager_;
            storage::PageId root_page_id_;
            std::uint16_t key_size_;
            std::uint16_t value_size_;
    };

}
