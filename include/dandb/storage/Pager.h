#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>
#include <dandb/storage/DiskManager.h>
#include <dandb/wal/WalManager.h>
#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/storage/PageHandle.h>
#include <dandb/storage/PageId.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/DatabaseHeader.h>
#include <dandb/platform/DatabasePath.h>
#include <dandb/transaction/TransactionState.h>

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace dandb::storage {

    class Pager {
        public:
            Pager(const Pager&) = delete;
            Pager& operator=(const Pager&) = delete;
            Pager(Pager&&) noexcept = default;
            Pager& operator=(Pager&&) noexcept = default;

            static core::Result<Pager> create(std::filesystem::path path, std::size_t bpm_capacity);
            static core::Result<Pager> open(std::filesystem::path path, std::size_t bpm_capacity);

            core::Result<PageHandle> get_page(PageId page_id);
            core::Result<PageHandle> new_page();

            core::Status begin_transaction();
            core::Status commit_transaction();
            core::Status rollback_transaction();
            bool in_transaction() const;
            core::Status mark_transaction_failed();

            core::Status checkpoint();
            core::Status close();

        private:
            friend class PageHandle;

            Pager(
                DiskManager disk_manager,
                wal::WalManager wal_manager,
                buffer::BufferPoolManager bpm,
                platform::DatabasePath path,
                DatabaseHeader db_header,
                std::unordered_map<PageId, Page> recovered_pages
            );

            core::Status mark_dirty(PageId page_id);

            DiskManager disk_manager_;
            wal::WalManager wal_manager_;
            buffer::BufferPoolManager bpm_;
            platform::DatabasePath path_;
            DatabaseHeader db_header_;
            std::unordered_map<PageId, Page> recovered_pages_;
            transaction::TransactionState transaction_state_;
            std::uint64_t next_transaction_id_ = 1;
            bool closed_ = false;
    };

}
