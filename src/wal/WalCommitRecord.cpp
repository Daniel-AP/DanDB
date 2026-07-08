#include <dandb/wal/WalCommitRecord.h>
#include <dandb/core/Checksum.h>
#include <dandb/core/Endian.h>

#include <cstddef>
#include <cstdint>

namespace dandb::wal {

    WalCommitRecord::WalCommitRecord(
        std::uint64_t transaction_id,
        std::uint64_t frame_count
    ) :
        transaction_id_(transaction_id),
        frame_count_(frame_count)
    {}

    core::Result<WalCommitRecord> WalCommitRecord::decode(std::span<const std::byte> bytes) {

        if(bytes.size() != WAL_COMMIT_RECORD_SIZE) {
            return core::Status::InvalidArgument("Cannot decode WAL commit record: record size is invalid");
        }

        // Validate record type
        auto stored_record_type_result = core::read_u32_le(bytes, WAL_COMMIT_RECORD_TYPE_OFFSET);
        if(!stored_record_type_result.ok()) {
            return stored_record_type_result.status();
        }

        const auto stored_record_type = stored_record_type_result.value();

        if(stored_record_type != WAL_COMMIT_RECORD_TYPE) {
            return core::Status::Corruption("Cannot decode WAL commit record: record type is invalid");
        }

        // Validate record size
        auto stored_record_size_result = core::read_u32_le(bytes, WAL_COMMIT_RECORD_SIZE_OFFSET);
        if(!stored_record_size_result.ok()) {
            return stored_record_size_result.status();
        }

        const auto stored_record_size = stored_record_size_result.value();

        if(stored_record_size != WAL_COMMIT_RECORD_SIZE) {
            return core::Status::Corruption("Cannot decode WAL commit record: record has unsupported size");
        }

        // Decode transaction id
        auto stored_transaction_id_result = core::read_u64_le(bytes, WAL_COMMIT_RECORD_TRANSACTION_ID_OFFSET);
        if(!stored_transaction_id_result.ok()) {
            return stored_transaction_id_result.status();
        }

        const auto stored_transaction_id = stored_transaction_id_result.value();

        // Decode frame count
        auto stored_frame_count_result = core::read_u64_le(bytes, WAL_COMMIT_RECORD_FRAME_COUNT_OFFSET);
        if(!stored_frame_count_result.ok()) {
            return stored_frame_count_result.status();
        }

        const auto stored_frame_count = stored_frame_count_result.value();

        // Validate checksum
        auto stored_checksum_result = core::read_u64_le(bytes, WAL_COMMIT_RECORD_CHECKSUM_OFFSET);
        if(!stored_checksum_result.ok()) {
            return stored_checksum_result.status();
        }

        const auto stored_checksum = stored_checksum_result.value();
        const auto current_checksum = core::checksum(bytes.first(WAL_COMMIT_RECORD_SIZE-sizeof(std::uint64_t)));

        if(stored_checksum != current_checksum) {
            return core::Status::Corruption("Cannot decode WAL commit record: stored checksum and actual checksum differ");
        }

        return WalCommitRecord{
            stored_transaction_id,
            stored_frame_count
        };

    }

    WalCommitRecord WalCommitRecord::create_new(
        std::uint64_t transaction_id,
        std::uint64_t frame_count
    ) {

        return WalCommitRecord{
            transaction_id,
            frame_count
        };

    }

    core::Status WalCommitRecord::encode_into(std::span<std::byte> out) const {

        if(out.size() != WAL_COMMIT_RECORD_SIZE) {
            return core::Status::InvalidArgument("Cannot encode WAL commit record: record size is invalid");
        }

        for(std::size_t i = 0; i < WAL_COMMIT_RECORD_SIZE; i++) {
            out[i] = std::byte{ 0 };
        }

        // Encode record type
        auto status = core::write_u32_le(out, WAL_COMMIT_RECORD_TYPE_OFFSET, WAL_COMMIT_RECORD_TYPE);
        if(!status.ok()) {
            return status;
        }

        // Encode record size
        status = core::write_u32_le(out, WAL_COMMIT_RECORD_SIZE_OFFSET, WAL_COMMIT_RECORD_SIZE);
        if(!status.ok()) {
            return status;
        }

        // Encode transaction id
        status = core::write_u64_le(out, WAL_COMMIT_RECORD_TRANSACTION_ID_OFFSET, transaction_id_);
        if(!status.ok()) {
            return status;
        }

        // Encode frame count
        status = core::write_u64_le(out, WAL_COMMIT_RECORD_FRAME_COUNT_OFFSET, frame_count_);
        if(!status.ok()) {
            return status;
        }

        // Encode checksum
        const auto current_checksum = core::checksum(out.first(WAL_COMMIT_RECORD_SIZE-sizeof(std::uint64_t)));

        status = core::write_u64_le(out, WAL_COMMIT_RECORD_CHECKSUM_OFFSET, current_checksum);
        if(!status.ok()) {
            return status;
        }

        return core::Status::Ok();

    }

    std::uint64_t WalCommitRecord::transaction_id() const {
        return transaction_id_;
    }

    std::uint64_t WalCommitRecord::frame_count() const {
        return frame_count_;
    }

}
