#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>
#include <dandb/core/Constants.h>
#include <dandb/storage/DatabaseHeader.h>
#include <dandb/storage/PageId.h>
#include <dandb/storage/Page.h>
#include <dandb/platform/FileHandle.h>

#include <filesystem>
#include <array>
#include <cstddef>
#include <cstdint>

namespace dandb::storage {

    class DiskManager {
        public:
            DiskManager(const DiskManager&) = delete;
            DiskManager& operator=(const DiskManager&) = delete;
            DiskManager(DiskManager&&) noexcept = default;
            DiskManager& operator=(DiskManager&&) noexcept = default;

            [[nodiscard]] static core::Result<DiskManager> create_new(std::filesystem::path path, const DatabaseHeader& db_header);
            [[nodiscard]] static core::Result<DiskManager> open_existing(std::filesystem::path path);

            [[nodiscard]] core::Result<DatabaseHeader> read_header();
            [[nodiscard]] core::Status write_header(const DatabaseHeader& db_header);
            [[nodiscard]] core::Result<Page> read_page(PageId page_id);
            [[nodiscard]] core::Status write_page(const Page& page);
            [[nodiscard]] core::Status resize_to_page_count(std::uint64_t page_count);

            [[nodiscard]] core::Status sync();
            [[nodiscard]] core::Status close();
            [[nodiscard]] core::Result<std::uint64_t> size() const;
            void set_fault_injector(platform::FileFaultInjector* fault_injector);
        private:
            explicit DiskManager(platform::FileHandle file_handle);

            platform::FileHandle file_handle_;

    };

}
