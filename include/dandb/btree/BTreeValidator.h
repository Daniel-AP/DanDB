#pragma once

#include <dandb/core/Status.h>
#include <dandb/storage/PageId.h>

#include <cstdint>

namespace dandb::storage {
    class Pager;
}

namespace dandb::btree {

    class BTreeValidator {
        public:
            static core::Status validate(
                storage::Pager& pager,
                storage::PageId root_page_id,
                std::uint16_t key_size,
                std::uint16_t value_size
            );
    };

}
