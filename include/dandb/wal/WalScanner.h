#pragma once

#include <dandb/wal/WalScanResult.h>
#include <dandb/core/Result.h>

#include <filesystem>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dandb::wal {

    class WalScanner {
        public:
            static core::Result<WalScanResult> scan(std::filesystem::path path, std::uint64_t expected_database_id);
    };

}