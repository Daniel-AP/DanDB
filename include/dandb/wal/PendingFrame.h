#pragma once

#include <dandb/storage/PageId.h>

#include <cstdint>

namespace dandb::wal {

    struct PendingFrame {
        std::uint64_t offset;
        storage::PageId page_id;
    };

}