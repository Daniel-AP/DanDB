#include <dandb/wal/WalPageFrame.h>
#include <dandb/core/Bytes.h>
#include <dandb/core/Checksum.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Endian.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace dandb::wal {

    WalPageFrame::WalPageFrame(
        std::uint64_t transaction_id,
        storage::PageId page_id,
        std::array<std::byte, core::PAGE_SIZE> page_image
    ) :
        transaction_id_(transaction_id),
        page_id_(page_id),
        page_image_(std::move(page_image))
    {}

    core::Result<WalPageFrame> WalPageFrame::decode(std::span<const std::byte> bytes) {

        if(bytes.size() != WAL_PAGE_FRAME_RECORD_SIZE) {
            return core::Status::InvalidArgument("Cannot decode WAL page frame: record size is invalid");
        }

        std::size_t offset = 0;

        // Validate record type
        auto stored_record_type_result = core::read_u32_le(bytes, offset);
        if(!stored_record_type_result.ok()) {
            return stored_record_type_result.status();
        }

        const auto stored_record_type = stored_record_type_result.value();

        if(stored_record_type != WAL_PAGE_FRAME_RECORD_TYPE) {
            return core::Status::Corruption("Cannot decode WAL page frame: record type is invalid");
        }

        offset += sizeof(std::uint32_t);

        // Validate record size
        auto stored_record_size_result = core::read_u32_le(bytes, offset);
        if(!stored_record_size_result.ok()) {
            return stored_record_size_result.status();
        }

        const auto stored_record_size = stored_record_size_result.value();

        if(stored_record_size != WAL_PAGE_FRAME_RECORD_SIZE) {
            return core::Status::Corruption("Cannot decode WAL page frame: record has unsupported size");
        }

        offset += sizeof(std::uint32_t);

        // Decode transaction id
        auto stored_transaction_id_result = core::read_u64_le(bytes, offset);
        if(!stored_transaction_id_result.ok()) {
            return stored_transaction_id_result.status();
        }

        const auto stored_transaction_id = stored_transaction_id_result.value();

        offset += sizeof(std::uint64_t);

        // Decode page id
        auto stored_page_id_result = core::read_u64_le(bytes, offset);
        if(!stored_page_id_result.ok()) {
            return stored_page_id_result.status();
        }

        const auto stored_page_id = storage::PageId{ stored_page_id_result.value() };

        offset += sizeof(std::uint64_t);

        // Validate page image size
        auto stored_page_image_size_result = core::read_u32_le(bytes, offset);
        if(!stored_page_image_size_result.ok()) {
            return stored_page_image_size_result.status();
        }

        const auto stored_page_image_size = static_cast<std::size_t>(stored_page_image_size_result.value());

        if(stored_page_image_size != core::PAGE_SIZE) {
            return core::Status::Corruption("Cannot decode WAL page frame: page image size is invalid");
        }

        offset += sizeof(std::uint32_t);

        // Validate reserved bytes
        if(!core::bytes_are_zero(bytes.subspan(offset, sizeof(std::uint32_t)))) {
            return core::Status::Corruption("Cannot decode WAL page frame: reserved bytes are non-zero");
        }

        offset += sizeof(std::uint32_t);

        // Decode page image
        std::array<std::byte, core::PAGE_SIZE> stored_page_image{};

        for(std::size_t i = 0; i < core::PAGE_SIZE; i++) {
            stored_page_image[i] = bytes[offset+i];
        }

        offset += core::PAGE_SIZE;

        // Validate checksum
        auto stored_checksum_result = core::read_u64_le(bytes, offset);
        if(!stored_checksum_result.ok()) {
            return stored_checksum_result.status();
        }

        const auto stored_checksum = stored_checksum_result.value();
        const auto current_checksum = core::checksum(bytes.first(WAL_PAGE_FRAME_RECORD_SIZE-sizeof(std::uint64_t)));

        if(stored_checksum != current_checksum) {
            return core::Status::Corruption("Cannot decode WAL page frame: stored checksum and actual checksum differ");
        }

        offset += sizeof(std::uint64_t);

        if(offset != WAL_PAGE_FRAME_RECORD_SIZE) {
            return core::Status::InternalError("WAL page frame decode ended at an unexpected offset");
        }

        return WalPageFrame{
            stored_transaction_id,
            stored_page_id,
            std::move(stored_page_image)
        };

    }

    WalPageFrame WalPageFrame::create_new(
        std::uint64_t transaction_id,
        storage::PageId page_id,
        const std::array<std::byte, core::PAGE_SIZE>& page_image
    ) {

        return WalPageFrame{
            transaction_id,
            page_id,
            page_image
        };

    }

    core::Status WalPageFrame::encode_into(std::span<std::byte> out) const {

        if(out.size() != WAL_PAGE_FRAME_RECORD_SIZE) {
            return core::Status::InvalidArgument("Cannot encode WAL page frame: record size is invalid");
        }

        for(std::size_t i = 0; i < WAL_PAGE_FRAME_RECORD_SIZE; i++) {
            out[i] = std::byte{ 0 };
        }

        std::size_t offset = 0;

        // Encode record type
        auto status = core::write_u32_le(out, offset, WAL_PAGE_FRAME_RECORD_TYPE);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint32_t);

        // Encode record size
        status = core::write_u32_le(out, offset, WAL_PAGE_FRAME_RECORD_SIZE);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint32_t);

        // Encode transaction id
        status = core::write_u64_le(out, offset, transaction_id_);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);

        // Encode page id
        status = core::write_u64_le(out, offset, page_id_.id);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);

        // Encode page image size
        status = core::write_u32_le(out, offset, static_cast<std::uint32_t>(core::PAGE_SIZE));
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint32_t);

        // Reserved bytes
        offset += sizeof(std::uint32_t);

        // Encode page image
        for(std::size_t i = 0; i < core::PAGE_SIZE; i++) {
            out[offset+i] = page_image_[i];
        }

        offset += core::PAGE_SIZE;

        // Encode checksum
        const auto current_checksum = core::checksum(out.first(WAL_PAGE_FRAME_RECORD_SIZE-sizeof(std::uint64_t)));

        status = core::write_u64_le(out, offset, current_checksum);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);

        if(offset != WAL_PAGE_FRAME_RECORD_SIZE) {
            return core::Status::InternalError("WAL page frame encode ended at an unexpected offset");
        }

        return core::Status::Ok();

    }

    std::uint64_t WalPageFrame::transaction_id() const {
        return transaction_id_;
    }

    storage::PageId WalPageFrame::page_id() const {
        return page_id_;
    }

    const std::array<std::byte, core::PAGE_SIZE>& WalPageFrame::page_image() const {
        return page_image_;
    }

}
