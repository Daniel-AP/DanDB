#include <dandb/wal/WalManager.h>

#include <dandb/platform/FileHandle.h>
#include <dandb/wal/WalHeader.h>
#include <dandb/wal/WalPageFrame.h>
#include <dandb/wal/WalCommitRecord.h>

#include <utility>
#include <array>
#include <cstdint>
#include <cstddef>

namespace dandb::wal {

    WalManager::WalManager(platform::FileHandle file_handle, std::uint64_t append_offset, std::uint64_t database_id) :
        file_handle_(std::move(file_handle)), append_offset_(append_offset), database_id_(database_id)
    {}

    core::Result<WalManager> WalManager::open_or_create(std::filesystem::path path, std::uint64_t expected_database_id) {

        auto file_handle_result = platform::FileHandle::open_or_create(path);
        if(!file_handle_result.ok()) {
            return file_handle_result.status();
        }

        platform::FileHandle file_handle = std::move(file_handle_result.value());

        auto file_size_result = file_handle.size();
        if(!file_size_result.ok()) {
            return file_size_result.status();
        }

        std::uint64_t file_size = file_size_result.value();

        if(file_size == 0) {

            std::array<std::byte, WAL_HEADER_SIZE> wal_header_bytes{};
            WalHeader wal_header = WalHeader::create_new(expected_database_id);

            auto encode_wal_header_status = wal_header.encode_into(wal_header_bytes);
            if(!encode_wal_header_status.ok()) {
                return encode_wal_header_status;
            }

            auto file_write_wal_header_status = file_handle.write_at(0, wal_header_bytes);
            if(!file_write_wal_header_status.ok()) {
                return file_write_wal_header_status;
            }
            
            return WalManager{std::move(file_handle), WAL_HEADER_SIZE, expected_database_id};

        } else if(file_size < WAL_HEADER_SIZE) {

            return core::Status::Corruption("Cannot open WAL file: file is smaller than the WAL header");

        } else {

            std::array<std::byte, WAL_HEADER_SIZE> wal_header_bytes{};

            auto read_wal_header_bytes_status = file_handle.read_at(0, wal_header_bytes);
            if(!read_wal_header_bytes_status.ok()) {
                return read_wal_header_bytes_status;
            }

            auto decode_wal_header_result = WalHeader::decode(wal_header_bytes);
            if(!decode_wal_header_result.ok()) {
                return decode_wal_header_result.status();
            }

            const WalHeader wal_header = std::move(decode_wal_header_result.value());
            if(wal_header.database_id() != expected_database_id) {
                return core::Status::Corruption("Cannot open WAL file: database id does not match");
            }

            return WalManager{std::move(file_handle), file_size, expected_database_id};

        }

    }

    core::Status WalManager::commit_transaction(std::uint64_t transaction_id, std::span<const storage::Page> pages) {

        for(const auto& page: pages) {
            const auto status = append_page_frame(transaction_id, page);
            if(!status.ok()) {
                return status;
            }
        }

        const auto append_commit_status = append_commit(transaction_id, pages.size());
        if(!append_commit_status.ok()) {
            return append_commit_status;
        }

        return sync();

    }

    core::Status WalManager::append_page_frame(std::uint64_t transaction_id, const storage::Page& page) {

        if(!page.id().is_valid()) {
            return core::Status::InvalidArgument("Cannot append WAL page frame: page id is invalid");
        }

        WalPageFrame wal_page_frame = WalPageFrame::create_new(
            transaction_id,
            page.id(),
            page.data()
        );

        std::array<std::byte, WAL_PAGE_FRAME_RECORD_SIZE> wal_page_frame_bytes{};

        auto encode_wal_page_frame_status = wal_page_frame.encode_into(wal_page_frame_bytes);
        if(!encode_wal_page_frame_status.ok()) {
            return encode_wal_page_frame_status;
        }

        auto write_page_frame_status = file_handle_.write_at(append_offset_, wal_page_frame_bytes);
        if(!write_page_frame_status.ok()) {
            return write_page_frame_status;
        }

        append_offset_ += WAL_PAGE_FRAME_RECORD_SIZE;

        return core::Status::Ok();

    }

    core::Status WalManager::append_commit(std::uint64_t transaction_id, std::uint64_t frame_count) {

        WalCommitRecord wal_commit = WalCommitRecord::create_new(transaction_id, frame_count);

        std::array<std::byte, WAL_COMMIT_RECORD_SIZE> wal_commit_bytes{};

        auto encode_wal_commit_status = wal_commit.encode_into(wal_commit_bytes);
        if(!encode_wal_commit_status.ok()) {
            return encode_wal_commit_status;
        }

        auto write_commit_status = file_handle_.write_at(append_offset_, wal_commit_bytes);
        if(!write_commit_status.ok()) {
            return write_commit_status;
        }

        append_offset_ += WAL_COMMIT_RECORD_SIZE;

        return core::Status::Ok();

    }

    core::Status WalManager::sync() {

        return file_handle_.sync();

    }

    core::Status WalManager::reset() {
        return core::Status::Ok();
    }

    core::Result<std::uint64_t> WalManager::size() const {
        return file_handle_.size();
    }

    std::uint64_t WalManager::database_id() const {
        return database_id_;
    }

    void WalManager::set_fault_injector(platform::FileFaultInjector* fault_injector) {
        file_handle_.set_fault_injector(fault_injector);
    }

}
