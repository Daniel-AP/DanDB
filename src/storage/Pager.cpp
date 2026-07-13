#include <dandb/storage/Pager.h>

#include <dandb/catalog/Catalog.h>
#include <dandb/core/Status.h>
#include <dandb/platform/FileHandle.h>
#include <dandb/wal/WalPageFrame.h>
#include <dandb/wal/WalScanner.h>
#include <dandb/core/Constants.h>
#include <dandb/buffer/PagePin.h>

#include <array>
#include <cstdint>
#include <limits>
#include <random>
#include <utility>
#include <vector>

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
        platform::FileLock file_lock,
        DiskManager disk_manager,
        wal::WalManager wal_manager,
        buffer::BufferPoolManager bpm,
        platform::DatabasePath path,
        DatabaseHeader db_header,
        std::unordered_map<PageId, Page> recovered_pages
    ) :
        file_lock_(std::move(file_lock)),
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

        auto acquire_exclusive_result = platform::FileLock::acquire_exclusive(db_path.main_path());
        if(!acquire_exclusive_result.ok()) {
            return acquire_exclusive_result.status();
        }

        platform::FileLock file_lock = std::move(acquire_exclusive_result.value());

        auto wal_manager_result = wal::WalManager::open_or_create(db_path.wal_path(), database_id);
        if(!wal_manager_result.ok()) {
            return wal_manager_result.status();
        }

        wal::WalManager wal_manager = std::move(wal_manager_result.value());

        buffer::BufferPoolManager bpm(bpm_capacity);

        Pager pager{
            std::move(file_lock),
            std::move(disk_manager),
            std::move(wal_manager),
            std::move(bpm),
            std::move(db_path),
            std::move(db_header),
            std::unordered_map<PageId, Page>{}
        };

        auto initialize_catalog_status = catalog::Catalog::initialize(pager);
        if(!initialize_catalog_status.ok()) {
            auto close_status = pager.close();
            if(!close_status.ok()) {
                return close_status;
            }

            return initialize_catalog_status;
        }

        return pager;

    }

    core::Result<Pager> Pager::open(std::filesystem::path path, std::size_t bpm_capacity) {

        if(bpm_capacity == 0) {
            return core::Status::InvalidArgument("Cannot open database: buffer pool manager capacity cannot be zero");
        }

        platform::DatabasePath db_path(path);

        auto acquire_exclusive_result = platform::FileLock::acquire_exclusive(db_path.main_path());
        if(!acquire_exclusive_result.ok()) {
            return acquire_exclusive_result.status();
        }

        platform::FileLock file_lock = std::move(acquire_exclusive_result.value());

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

                if(recovered_page.id() == HEADER_PAGE_ID) {
                    auto recovered_header_result = DatabaseHeader::decode(recovered_page.data());
                    if(!recovered_header_result.ok()) {
                        return recovered_header_result.status();
                    }

                    db_header = std::move(recovered_header_result.value());
                } else {
                    recovered_pages[recovered_page.id()] = recovered_page;
                }

            }

        }

        buffer::BufferPoolManager bpm(bpm_capacity);

        return Pager{
            std::move(file_lock),
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

        if(!transaction_state_.in_transaction()) {
            return core::Status::TransactionError("Cannot allocate new page: no transaction is active");
        }

        if(transaction_state_.is_failed()) {
            return core::Status::TransactionError("Cannot allocate new page: transaction is failed");
        }

        if(transaction_state_.is_unresolved()) {
            return core::Status::TransactionError("Cannot allocate new page: transaction is unresolved");
        }

        const PageId page_id{ db_header_.page_count() };
        Page page(page_id);

        auto bpm_cache_page_result = bpm_.cache_page(page);
        if(!bpm_cache_page_result.ok()) {
            return bpm_cache_page_result.status();
        }

        auto mark_header_dirty_status = mark_dirty(HEADER_PAGE_ID);
        if(!mark_header_dirty_status.ok()) {
            return mark_header_dirty_status;
        }

        db_header_.set_page_count(db_header_.page_count()+1);

        buffer::PagePin page_pin = std::move(bpm_cache_page_result.value());

        transaction_state_.new_page_ids.insert(page_id);
        transaction_state_.dirty_page_ids.insert(page_id);
        page_pin.mark_dirty();

        return PageHandle{
            this,
            std::move(page_pin)
        };

    }

    const DatabaseHeader& Pager::database_header() const {
        return db_header_;
    }

    core::Status Pager::begin_transaction() {

        if(closed_) {
            return core::Status::InvalidArgument("Cannot begin transaction: pager is closed");
        }

        if(transaction_state_.in_transaction()) {
            return core::Status::TransactionError("Cannot begin transaction: transaction is already active");
        }

        transaction_state_.status = transaction::TransactionStatus::Active;
        transaction_state_.transaction_id = next_transaction_id_;
        next_transaction_id_++;

        return core::Status::Ok();

    }

    core::Status Pager::commit_transaction() {

        if(closed_) {
            return core::Status::InvalidArgument("Cannot commit transaction: pager is closed");
        }

        if(!transaction_state_.in_transaction()) {
            return core::Status::TransactionError("Cannot commit transaction: no transaction is active");
        }

        if(transaction_state_.is_failed()) {
            return core::Status::TransactionError("Cannot commit transaction: transaction is failed");
        }

        if(transaction_state_.is_unresolved()) {
            return core::Status::TransactionError("Cannot commit transaction: transaction is unresolved");
        }

        for(const auto& dirty_page_id: transaction_state_.dirty_page_ids) {

            if(dirty_page_id == HEADER_PAGE_ID) continue;

            auto require_unpinned_result = bpm_.require_unpinned(dirty_page_id);
            if(!require_unpinned_result.ok()) {
                return require_unpinned_result.status();
            }
            
            if(!require_unpinned_result.value()) {
                return core::Status::TransactionError("Cannot commit transaction: dirty page is still pinned");
            }

        }

        std::size_t dirty_pages_amount = transaction_state_.dirty_page_ids.size();
        std::vector<Page> dirty_pages;
        dirty_pages.reserve(dirty_pages_amount);

        for(const auto& dirty_page_id: transaction_state_.dirty_page_ids) {

            auto bpm_get_dirty_page_result = dirty_page_snapshot(dirty_page_id);
            if(!bpm_get_dirty_page_result.ok()) {
                return bpm_get_dirty_page_result.status();
            }

            dirty_pages.push_back(bpm_get_dirty_page_result.value());

        }

        auto wal_commit_status = wal_manager_.commit_transaction(transaction_state_.transaction_id, dirty_pages);
        if(!wal_commit_status.ok()) {
            transaction_state_.status = transaction::TransactionStatus::Unresolved;
            return wal_commit_status;
        }

        for(const auto& dirty_page: dirty_pages) {

            if(dirty_page.id() == HEADER_PAGE_ID) continue; 
            recovered_pages_[dirty_page.id()] = dirty_page;

            auto clear_status = bpm_.clear_dirty(dirty_page.id());
            if(!clear_status.ok()) {
                return clear_status;
            }

        }

        transaction_state_.clear();

        return core::Status::Ok();

    }

    core::Status Pager::rollback_transaction() {

        if(closed_) {
            return core::Status::InvalidArgument("Cannot rollback transaction: pager is closed");
        }

        if(!transaction_state_.in_transaction()) {
            return core::Status::TransactionError("Cannot rollback transaction: no transaction is active");
        }

        if(transaction_state_.is_unresolved()) {
            return core::Status::TransactionError("Cannot rollback transaction: transaction is unresolved");
        }

        for(const auto& [page_id, original_page]: transaction_state_.original_pages) {

            if(page_id != original_page.id()) {
                return core::Status::InternalError("Cannot rollback transaction: original page id does not match map key");
            }

            if(transaction_state_.new_page_ids.find(page_id) != transaction_state_.new_page_ids.end()) {
                return core::Status::InternalError("Cannot rollback transaction: page cannot be both original and newly allocated");
            }

            if(page_id == HEADER_PAGE_ID) {
                auto header_result = DatabaseHeader::decode(original_page.data());
                if(!header_result.ok()) {
                    return header_result.status();
                }

                continue;
            }

            auto can_restore_status = bpm_.can_restore_page(original_page);
            if(!can_restore_status.ok()) {
                return can_restore_status;
            }

        }

        for(const auto& new_page_id: transaction_state_.new_page_ids) {

            auto can_discard_status = bpm_.can_discard_page(new_page_id);
            if(!can_discard_status.ok()) {
                return can_discard_status;
            }

        }

        for(const auto& [page_id, original_page]: transaction_state_.original_pages) {

            if(page_id == HEADER_PAGE_ID) {
                auto header_result = DatabaseHeader::decode(original_page.data());
                if(!header_result.ok()) {
                    return header_result.status();
                }

                db_header_ = std::move(header_result.value());
                continue;
            }

            auto restore_status = bpm_.restore_page(original_page);
            if(!restore_status.ok()) {
                return restore_status;
            }

        }

        for(const auto& new_page_id: transaction_state_.new_page_ids) {

            auto discard_status = bpm_.discard_page(new_page_id);
            if(!discard_status.ok()) {
                return discard_status;
            }

        }

        transaction_state_.clear();

        return core::Status::Ok();

    }

    bool Pager::in_transaction() const {
        return transaction_state_.in_transaction();
    }

    core::Status Pager::mark_transaction_failed() {

        if(closed_) {
            return core::Status::InvalidArgument("Cannot mark transaction failed: pager is closed");
        }

        if(!transaction_state_.in_transaction()) {
            return core::Status::TransactionError("Cannot mark transaction failed: no transaction is active");
        }

        if(transaction_state_.is_unresolved()) {
            return core::Status::TransactionError("Cannot mark transaction failed: transaction is unresolved");
        }

        transaction_state_.status = transaction::TransactionStatus::Failed;
        
        return core::Status::Ok();

    }

    core::Status Pager::checkpoint() {
        
        if(transaction_state_.in_transaction()) {
            return core::Status::TransactionError("Cannot checkpoint: a transaction is active");
        }

        auto resize_status = disk_manager_.resize_to_page_count(db_header_.page_count());
        if(!resize_status.ok()) {
            return resize_status;
        }

        for(const auto& [page_id, page]: recovered_pages_) {
            auto write_status = disk_manager_.write_page(page);
            if(!write_status.ok()) {
                return write_status;
            } 
        }

        auto write_header_status = disk_manager_.write_header(db_header_);
        if(!write_header_status.ok()) {
            return write_header_status;
        }

        auto sync_status = disk_manager_.sync();
        if(!sync_status.ok()) {
            return sync_status;
        }

        auto wal_reset_status = wal_manager_.reset();
        if(!wal_reset_status.ok()) {
            return wal_reset_status;
        }

        recovered_pages_.clear();

        return core::Status::Ok();

    }

    core::Status Pager::mark_dirty(PageId page_id) {

        if(closed_) {
            return core::Status::InvalidArgument("Cannot mark page dirty: pager is closed");
        }

        if(page_id == INVALID_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot mark page dirty: invalid page id");
        }

        if(transaction_state_.is_failed()) {
            return core::Status::TransactionError("Cannot mark page dirty: transaction is failed");
        }

        if(transaction_state_.is_unresolved()) {
            return core::Status::TransactionError("Cannot mark page dirty: transaction is unresolved");
        }

        if(!transaction_state_.in_transaction()) {
            return core::Status::TransactionError("Cannot mark page dirty: no transaction is active");
        }

        if(transaction_state_.original_pages.find(page_id) != transaction_state_.original_pages.end()) {
            return core::Status::Ok();
        }

        if(transaction_state_.new_page_ids.find(page_id) != transaction_state_.new_page_ids.end()) {
            return core::Status::Ok();
        }

        if(page_id == HEADER_PAGE_ID) {

            std::array<std::byte, core::PAGE_SIZE> header_page_bytes{};

            auto encode_header_status = db_header_.encode_into(header_page_bytes);
            if(!encode_header_status.ok()) {
                return encode_header_status;
            }

            Page page(page_id);
            page.data() = std::move(header_page_bytes);

            transaction_state_.original_pages[page_id] = std::move(page);

        } else {

            auto bpm_get_page_result = bpm_.get_page(page_id);
            if(!bpm_get_page_result.ok()) {
                return bpm_get_page_result.status();
            }

            buffer::PagePin page_pin = std::move(bpm_get_page_result.value());
            transaction_state_.original_pages[page_id] = *page_pin.page();

        }

        transaction_state_.dirty_page_ids.insert(page_id);

        return core::Status::Ok();

    }

    core::Result<Page> Pager::dirty_page_snapshot(PageId page_id) {

        if(page_id == HEADER_PAGE_ID) {
            Page header_page(HEADER_PAGE_ID);
            auto status = db_header_.encode_into(header_page.data());
            if(!status.ok()) {
                return status;
            }
            return header_page;
        }

        auto page_result = bpm_.get_page(page_id);
        if(!page_result.ok()) {
            return page_result.status();
        }

        return *page_result.value().page();

    }

    core::Status Pager::close() {

        if(closed_) {
            return core::Status::Ok();
        }

        auto wal_close_status = wal_manager_.close();
        auto disk_close_status = disk_manager_.close();
        auto lock_close_status = file_lock_.close();

        if(!wal_close_status.ok()) {
            return wal_close_status;
        }

        if(!disk_close_status.ok()) {
            return disk_close_status;
        }

        if(!lock_close_status.ok()) {
            return lock_close_status;
        }

        closed_ = true;
        return core::Status::Ok();
        
    }

    void Pager::set_wal_fault_injector(platform::FileFaultInjector* fault_injector) {
        wal_manager_.set_fault_injector(fault_injector);
    }

    void Pager::set_disk_fault_injector(platform::FileFaultInjector* fault_injector) {
        disk_manager_.set_fault_injector(fault_injector);
    }

}
