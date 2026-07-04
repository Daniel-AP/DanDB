#include <dandb/storage/Pager.h>

#include <dandb/core/Status.h>

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
        DatabaseHeader db_header
    ) :
        disk_manager_(std::move(disk_manager)),
        wal_manager_(std::move(wal_manager)),
        bpm_(std::move(bpm)),
        path_(std::move(path)),
        db_header_(std::move(db_header))
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
            std::move(db_header)
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

        // D05-T05 will scan WAL here and prepare recovered page images.

        buffer::BufferPoolManager bpm(bpm_capacity);

        return Pager{
            std::move(disk_manager),
            std::move(wal_manager),
            std::move(bpm),
            std::move(db_path),
            std::move(db_header)
        }; 

    }

    core::Result<PageHandle> Pager::get_page(PageId page_id) {

        if(closed_) {
            return core::Status::InvalidArgument("Cannot get page: pager is closed");
        }

        if(page_id == INVALID_PAGE_ID || page_id == HEADER_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot get page: invalid page id");
        }

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

        auto disk_get_page_result = disk_manager_.read_page(page_id);
        if(!disk_get_page_result.ok()) {
            return disk_get_page_result.status();
        }

        Page page = std::move(disk_get_page_result.value());

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
        return core::Status::InternalError("Cannot allocate new page: page allocation is not implemented yet");
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

        if(page_id == INVALID_PAGE_ID || page_id == HEADER_PAGE_ID) {
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
