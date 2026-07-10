#pragma once

#include <dandb/btree/BTreeEntry.h>
#include <dandb/core/Result.h>
#include <dandb/storage/PageId.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dandb::storage {
    class Pager;
}

namespace dandb::btree {

    class BTree;

    class BTreeCursor {
        public:
            core::Result<std::optional<BTreeEntry>> next();

        private:
            friend class BTree;

            BTreeCursor(
                storage::Pager& pager,
                storage::PageId current_leaf_page_id,
                std::uint16_t entry_index,
                std::optional<std::span<const std::byte>> upper_bound
            );

            storage::Pager* pager_;
            storage::PageId current_leaf_page_id_;
            std::uint16_t entry_index_;
            std::optional<std::vector<std::byte>> upper_bound_;
    };

}
