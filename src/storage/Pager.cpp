#include <dandb/storage/Pager.h>

#include <dandb/core/Status.h>
#include <dandb/platform/FileHandle.h>
#include <dandb/wal/WalPageFrame.h>
#include <dandb/wal/WalScanner.h>

#include <array>
#include <cstdint>
#include <limits>
#include <random>
#include <utility>

namespace {

    std::uint64_t generate_database_id() {

        std::random_device random_device;
        std::mt19937_64 generator(random_device());
        std::uniform_int_distribution<std::uint64_t> distribution(
            1,
            std::numeric_limits<std::uint64_t>::max()
        );

        return distribution(generator);

    }

}

namespace dandb::storage {

    Pager::Pager(
        DiskManager disk_manager,
        wal::WalManager wal_manager,
        buffer::BufferPoolManager bpm,
        platform::DatabasePath path,
        DatabaseHeader db_header,
        std::unordered_map<PageId, Page> recovered_pages
    ) :
        disk_manager_(std::move(disk_manager)),
        wal_manager_(std::move(wal_manager)),
        bpm_(std::move(bpm)),
        path_(std::move(path)),
        db_header_(std::move(db_header)),
        recovered_pages_(std::move(recovered_pages))
    {}

    core::Result<Pager> Pager::create(std::filesystem::path path, std::size_t bpm_capacity) {

        if(bpm_capacity == 0) {
            return core::Status::InvalidArgument("Cannot create database: buffer pool manager capacity cannot be zero");
        }

        platform::DatabasePath db_path(path);

        std::uint64_t database_id = generate_database_id();
        DatabaseHeader db_header = DatabaseHeader::create_new(database_id);

        auto disk_manager_result = DiskManager::create_new(db_path.main_path(), db_header);
        if(!disk_manager_result.ok()) {
            return disk_manager_result.status();
        }

        DiskManager disk_manager = std::move(disk_manager_result.value());

        auto wal_manager_result = wal::WalManager::open_or_create(db_path.wal_path(), database_id);
        if(!wal_manager_result.ok()) {
            return wal_manager_result.status();
        }

        wal::WalManager wal_manager = std::move(wal_manager_result.value());

        buffer::BufferPoolManager bpm(bpm_capacity);

        return Pager{
            std::move(disk_manager),
            std::move(wal_manager),
            std::move(bpm),
            std::move(db_path),
            std::move(db_header),
            std::unordered_map<PageId, Page>{}
        };

    }

    core::Result<Pager> Pager::open(std::filesystem::path path, std::size_t bpm_capacity) {

        if(bpm_capacity == 0) {
            return core::Status::InvalidArgument("Cannot open database: buffer pool manager capacity cannot be zero");
        }

        platform::DatabasePath db_path(path);

        auto disk_manager_result = DiskManager::open_existing(db_path.main_path());
        if(!disk_manager_result.ok()) {
            return disk_manager_result.status();
        }

        DiskManager disk_manager = std::move(disk_manager_result.value());

        auto db_header_result = disk_manager.read_header();
        if(!db_header_result.ok()) {
            return db_header_result.status();
        }

        DatabaseHeader db_header = std::move(db_header_result.value());

        auto wal_manager_result = wal::WalManager::open_or_create(db_path.wal_path(), db_header.database_id());
        if(!wal_manager_result.ok()) {
            return wal_manager_result.status();
        }

        wal::WalManager wal_manager = std::move(wal_manager_result.value());

        // Scan WAL

        auto wal_scan_result = wal::WalScanner::scan(db_path.wal_path(), db_header.database_id());
        if(!wal_scan_result.ok()) {
            return wal_scan_result.status();
        }

        std::unordered_map<PageId, Page> recovered_pages;
        const auto& latest_frame_offsets = wal_scan_result.value().latest_committed_frame_offsets;

        // Get page bytes from each offset in the scan result

        if(!latest_frame_offsets.empty()) {

            auto wal_file_result = platform::FileHandle::open_existing(db_path.wal_path());
            if(!wal_file_result.ok()) {
                return wal_file_result.status();
            }

            platform::FileHandle wal_file = std::move(wal_file_result.value());

            for(auto it = latest_frame_offsets.begin(); it != latest_frame_offsets.end(); it++) {

                std::array<std::byte, wal::WAL_PAGE_FRAME_RECORD_SIZE> page_frame_bytes{};

                auto read_page_frame_status = wal_file.read_at(it->second, page_frame_bytes);
                if(!read_page_frame_status.ok()) {
                    return read_page_frame_status;
                }

                auto wal_page_frame_result = wal::WalPageFrame::decode(page_frame_bytes);
                if(!wal_page_frame_result.ok()) {
                    return wal_page_frame_result.status();
                }

                wal::WalPageFrame wal_page_frame = std::move(wal_page_frame_result.value());
                if(wal_page_frame.page_id() != it->first) {
                    return core::Status::Corruption("Cannot recover WAL page frame: page id does not match scan result");
                }

                Page recovered_page(wal_page_frame.page_id());
                recovered_page.data() = wal_page_frame.page_image();
                recovered_pages[recovered_page.id()] = recovered_page;

            }

        }

        buffer::BufferPoolManager bpm(bpm_capacity);

        return Pager{
            std::move(disk_manager),
            std::move(wal_manager),
            std::move(bpm),
            std::move(db_path),
            std::move(db_header),
            std::move(recovered_pages)
        }; 

    }

    core::Result<PageHandle> Pager::get_page(PageId page_id) {

        if(closed_) {
            return core::Status::InvalidArgument("Cannot get page: pager is closed");
        }

        if(page_id == INVALID_PAGE_ID || page_id == HEADER_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot get page: invalid page id");
        }

        // Try to get page from bpm

        auto bpm_get_page_result = bpm_.get_page(page_id);

        if(bpm_get_page_result.ok()) {

            buffer::PagePin page_pin = std::move(bpm_get_page_result.value());

            return PageHandle{
                this,
                std::move(page_pin)
            };
        }
        
        if(bpm_get_page_result.status().code() != core::StatusCode::NotFound) {
            return bpm_get_page_result.status();
        }

        // Try to get page from recovered pages from WAL
        // Otherwise, try to read it from disk

        Page page;
        auto recovered_page_it = recovered_pages_.find(page_id);

        if(recovered_page_it != recovered_pages_.end()) {
            page = recovered_page_it->second;
        } else {

            auto disk_get_page_result = disk_manager_.read_page(page_id);
            if(!disk_get_page_result.ok()) {
                return disk_get_page_result.status();
            }

            page = std::move(disk_get_page_result.value());

        }

        auto bpm_cache_page_result = bpm_.cache_page(page);
        if(!bpm_cache_page_result.ok()) {
            return bpm_cache_page_result.status();
        }

        buffer::PagePin page_pin = std::move(bpm_cache_page_result.value());

        return PageHandle{
            this,
            std::move(page_pin)
        };

    }

    core::Result<PageHandle> Pager::new_page() {
        if(closed_) {
            return core::Status::InvalidArgument("Cannot allocate new page: pager is closed");
        }

        const PageId page_id{ db_header_.page_count() };
        Page page(page_id);

        auto bpm_cache_page_result = bpm_.cache_page(page);
        if(!bpm_cache_page_result.ok()) {
            return bpm_cache_page_result.status();
        }

        db_header_.set_page_count(db_header_.page_count()+1);

        auto mark_header_dirty_status = mark_dirty(HEADER_PAGE_ID);
        if(!mark_header_dirty_status.ok()) {
            return mark_header_dirty_status;
        }

        auto mark_page_dirty_status = mark_dirty(page_id);
        if(!mark_page_dirty_status.ok()) {
            return mark_page_dirty_status;
        }

        buffer::PagePin page_pin = std::move(bpm_cache_page_result.value());
        page_pin.mark_dirty();

        return PageHandle{
            this,
            std::move(page_pin)
        };
    }

    core::Status Pager::begin_transaction() {
        return core::Status::InternalError("Cannot begin transaction: transaction state is not implemented yet");
    }

    core::Status Pager::commit_transaction() {
        return core::Status::InternalError("Cannot commit transaction: transaction commit is not implemented yet");
    }

    core::Status Pager::rollback_transaction() {
        return core::Status::InternalError("Cannot rollback transaction: transaction rollback is not implemented yet");
    }

    core::Status Pager::checkpoint() {
        return core::Status::InternalError("Cannot checkpoint database: checkpoint is not implemented yet");
    }

    core::Status Pager::mark_dirty(PageId page_id) {

        if(closed_) {
            return core::Status::InvalidArgument("Cannot mark page dirty: pager is closed");
        }

        if(page_id == INVALID_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot mark page dirty: invalid page id");
        }

        return core::Status::Ok();

    }

    core::Status Pager::close() {

        if(closed_) {
            return core::Status::Ok();
        }

        auto wal_close_status = wal_manager_.close();
        auto disk_close_status = disk_manager_.close();

        if(!wal_close_status.ok()) {
            return wal_close_status;
        }

        if(!disk_close_status.ok()) {
            return disk_close_status;
        }

        closed_ = true;
        return core::Status::Ok();
        
    }

}
