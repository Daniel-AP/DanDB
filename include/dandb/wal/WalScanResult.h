#pragma once

#include <dandb/storage/PageId.h>

#include <unordered_map>
#include <vector>
#include <cstdint>

namespace dandb::wal {

    struct WalScanResult {
        std::unordered_map<storage::PageId, std::uint64_t> latest_committed_frame_offsets;
        std::uint64_t valid_wal_end_offset;
        bool ignored_trailing_bytes;
    };

}