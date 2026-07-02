#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>
#include <dandb/platform/FileHandle.h>
#include <dandb/storage/Page.h>

#include <filesystem>
#include <cstdint>
#include <span>

namespace dandb::platform {
    class FileFaultInjector;
}

namespace dandb::wal {

    class WalManager {
        public:
            WalManager(const WalManager&) = delete;
            WalManager& operator=(const WalManager&) = delete;
            WalManager(WalManager&&) noexcept = default;
            WalManager& operator=(WalManager&&) noexcept = default;

            static core::Result<WalManager> open_or_create(std::filesystem::path path, std::uint64_t expected_database_id);
            core::Status commit_transaction(std::uint64_t transaction_id, std::span<const storage::Page> pages);

            core::Status reset();
            core::Result<std::uint64_t> size() const;
            std::uint64_t database_id() const;
            void set_fault_injector(platform::FileFaultInjector* fault_injector);

        private:
            explicit WalManager(
                platform::FileHandle file_handle,
                std::uint64_t append_offset,
                std::uint64_t database_id
            );

            core::Status append_page_frame(std::uint64_t transaction_id, const storage::Page& page);
            core::Status append_commit(std::uint64_t transaction_id, std::uint64_t frame_count);
            core::Status sync();

            platform::FileHandle file_handle_;
            std::uint64_t append_offset_;
            std::uint64_t database_id_;
    };

}
