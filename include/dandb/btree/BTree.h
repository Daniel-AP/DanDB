#pragma once

#include <dandb/core/Result.h>
#include <dandb/storage/PageId.h>

#include <cstdint>

namespace dandb::storage {
    class Pager;
}

namespace dandb::btree {

    class BTree {
        public:
            static core::Result<BTree> create_new(
                storage::Pager& pager,
                std::uint16_t key_size,
                std::uint16_t value_size,
                bool uniqueness
            );

            static core::Result<BTree> open_existing(
                storage::Pager& pager,
                storage::PageId root_page_id,
                std::uint16_t key_size,
                std::uint16_t value_size,
                bool uniqueness
            );

            storage::PageId root_page_id() const;
            std::uint16_t key_size() const;
            std::uint16_t value_size() const;
            bool uniqueness() const;

        private:
            explicit BTree(
                storage::Pager& pager,
                storage::PageId root_page_id,
                std::uint16_t key_size,
                std::uint16_t value_size,
                bool uniqueness
            );

            storage::Pager* pager_;
            storage::PageId root_page_id_;
            std::uint16_t key_size_;
            std::uint16_t value_size_;
            bool uniqueness_;
    };

}
