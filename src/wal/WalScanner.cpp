#include <dandb/wal/WalScanner.h>

#include <dandb/wal/WalScanResult.h>
#include <dandb/platform/FileHandle.h>
#include <dandb/wal/WalHeader.h>
#include <dandb/core/Status.h>
#include <dandb/core/Result.h>
#include <dandb/wal/PendingFrame.h>
#include <dandb/core/Endian.h>
#include <dandb/wal/WalPageFrame.h>
#include <dandb/wal/WalCommitRecord.h>

#include <cstdint>
#include <array>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace dandb::wal {

    core::Result<WalScanResult> WalScanner::scan(std::filesystem::path path, std::uint64_t expected_database_id) {

        auto file_handle_result = platform::FileHandle::open_existing(path);
        if(!file_handle_result.ok()) {
            return file_handle_result.status();
        }

        platform::FileHandle file_handle = std::move(file_handle_result.value());

        auto file_size_result = file_handle.size();
        if(!file_size_result.ok()) {
            return file_size_result.status();
        }

        // Validate WAL header

        std::uint64_t file_size = file_size_result.value();

        if(file_size < WAL_HEADER_SIZE) {
            return core::Status::Corruption("Cannot scan WAL file: file is smaller than the WAL header");
        }

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
            return core::Status::Corruption("Cannot scan WAL file: database id does not match");
        }

        // Scan

        std::vector<PendingFrame> pending_frames;
        std::uint64_t pending_transaction_id = 0;
        bool has_pending_transaction = false;
        WalScanResult wal_scan_result{};

        wal_scan_result.valid_wal_end_offset = WAL_HEADER_SIZE;
        wal_scan_result.ignored_trailing_bytes = false;

        std::uint64_t offset = WAL_HEADER_SIZE;

        while(offset < file_size) {

            std::uint64_t remaining = file_size-offset;
            if(remaining < sizeof(std::uint32_t)) break;

            // Read record type

            std::array<std::byte, sizeof(std::uint32_t)> record_type_bytes{};

            auto read_record_type_status = file_handle.read_at(offset, record_type_bytes);
            if(!read_record_type_status.ok()) {
                return read_record_type_status;
            }

            auto stored_record_type_result = core::read_u32_le(record_type_bytes, 0);
            if(!stored_record_type_result.ok()) {
                return stored_record_type_result.status();
            }

            const auto stored_record_type = stored_record_type_result.value();

            // Read WalPageFrame/WalCommitRecord

            if(stored_record_type == WAL_PAGE_FRAME_RECORD_TYPE) {

                if(remaining < WAL_PAGE_FRAME_RECORD_SIZE) break;

                std::array<std::byte, WAL_PAGE_FRAME_RECORD_SIZE> page_frame_bytes{};

                auto read_page_frame_bytes_status = file_handle.read_at(offset, page_frame_bytes);
                if(!read_page_frame_bytes_status.ok()) {
                    return read_page_frame_bytes_status;
                }

                auto decode_page_frame_result = WalPageFrame::decode(page_frame_bytes);
                if(!decode_page_frame_result.ok()) {
                    return decode_page_frame_result.status();
                }

                WalPageFrame wal_page_frame = std::move(decode_page_frame_result.value());

                if(!has_pending_transaction) {
                    pending_transaction_id = wal_page_frame.transaction_id();
                    has_pending_transaction = true;
                } else if(pending_transaction_id != wal_page_frame.transaction_id()) {
                    return core::Status::Corruption("Cannot scan WAL file: interleaved transactions are unsupported");
                }

                pending_frames.push_back({ offset, wal_page_frame.page_id() });

                offset += WAL_PAGE_FRAME_RECORD_SIZE;

            } else if(stored_record_type == WAL_COMMIT_RECORD_TYPE) {

                if(remaining < WAL_COMMIT_RECORD_SIZE) break;

                std::array<std::byte, WAL_COMMIT_RECORD_SIZE> commit_bytes{};

                auto read_commit_bytes_status = file_handle.read_at(offset, commit_bytes);
                if(!read_commit_bytes_status.ok()) {
                    return read_commit_bytes_status;
                }

                auto decode_commit_result = WalCommitRecord::decode(commit_bytes);
                if(!decode_commit_result.ok()) {
                    return decode_commit_result.status();
                }

                WalCommitRecord wal_commit = std::move(decode_commit_result.value());

                if(!has_pending_transaction && wal_commit.frame_count() == 0) {
                    offset += WAL_COMMIT_RECORD_SIZE;
                    wal_scan_result.valid_wal_end_offset = offset;
                    continue;
                }

                if(
                    !has_pending_transaction ||
                    wal_commit.transaction_id() != pending_transaction_id ||
                    wal_commit.frame_count() != pending_frames.size()
                ) {
                    return core::Status::Corruption("Cannot scan WAL file: invalid frame count in commit record");
                }

                for(const PendingFrame& pending_frame: pending_frames) {
                    wal_scan_result.latest_committed_frame_offsets[pending_frame.page_id] = pending_frame.offset;
                }

                pending_frames.clear();
                has_pending_transaction = false;
                pending_transaction_id = 0;

                offset += WAL_COMMIT_RECORD_SIZE;
                wal_scan_result.valid_wal_end_offset = offset;

            } else {
                return core::Status::Corruption("Cannot scan WAL file: unsupported record type");
            }
            
        }

        wal_scan_result.ignored_trailing_bytes = (wal_scan_result.valid_wal_end_offset != file_size);

        return wal_scan_result;

    }

}
