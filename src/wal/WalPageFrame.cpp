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

        // Validate record type
        auto stored_record_type_result = core::read_u32_le(bytes, WAL_PAGE_FRAME_RECORD_TYPE_OFFSET);
        if(!stored_record_type_result.ok()) {
            return stored_record_type_result.status();
        }

        const auto stored_record_type = stored_record_type_result.value();

        if(stored_record_type != WAL_PAGE_FRAME_RECORD_TYPE) {
            return core::Status::Corruption("Cannot decode WAL page frame: record type is invalid");
        }

        // Validate record size
        auto stored_record_size_result = core::read_u32_le(bytes, WAL_PAGE_FRAME_RECORD_SIZE_OFFSET);
        if(!stored_record_size_result.ok()) {
            return stored_record_size_result.status();
        }

        const auto stored_record_size = stored_record_size_result.value();

        if(stored_record_size != WAL_PAGE_FRAME_RECORD_SIZE) {
            return core::Status::Corruption("Cannot decode WAL page frame: record has unsupported size");
        }

        // Decode transaction id
        auto stored_transaction_id_result = core::read_u64_le(bytes, WAL_PAGE_FRAME_TRANSACTION_ID_OFFSET);
        if(!stored_transaction_id_result.ok()) {
            return stored_transaction_id_result.status();
        }

        const auto stored_transaction_id = stored_transaction_id_result.value();

        // Decode page id
        auto stored_page_id_result = core::read_u64_le(bytes, WAL_PAGE_FRAME_PAGE_ID_OFFSET);
        if(!stored_page_id_result.ok()) {
            return stored_page_id_result.status();
        }

        const auto stored_page_id = storage::PageId{ stored_page_id_result.value() };

        // Validate page image size
        auto stored_page_image_size_result = core::read_u32_le(bytes, WAL_PAGE_FRAME_IMAGE_SIZE_OFFSET);
        if(!stored_page_image_size_result.ok()) {
            return stored_page_image_size_result.status();
        }

        const auto stored_page_image_size = static_cast<std::size_t>(stored_page_image_size_result.value());

        if(stored_page_image_size != core::PAGE_SIZE) {
            return core::Status::Corruption("Cannot decode WAL page frame: page image size is invalid");
        }

        // Validate reserved bytes
        if(!core::bytes_are_zero(bytes.subspan(WAL_PAGE_FRAME_RESERVED_OFFSET, sizeof(std::uint32_t)))) {
            return core::Status::Corruption("Cannot decode WAL page frame: reserved bytes are non-zero");
        }

        // Decode page image
        std::array<std::byte, core::PAGE_SIZE> stored_page_image{};

        for(std::size_t i = 0; i < core::PAGE_SIZE; i++) {
            stored_page_image[i] = bytes[WAL_PAGE_FRAME_IMAGE_OFFSET+i];
        }

        // Validate checksum
        auto stored_checksum_result = core::read_u64_le(bytes, WAL_PAGE_FRAME_CHECKSUM_OFFSET);
        if(!stored_checksum_result.ok()) {
            return stored_checksum_result.status();
        }

        const auto stored_checksum = stored_checksum_result.value();
        const auto current_checksum = core::checksum(bytes.first(WAL_PAGE_FRAME_RECORD_SIZE-sizeof(std::uint64_t)));

        if(stored_checksum != current_checksum) {
            return core::Status::Corruption("Cannot decode WAL page frame: stored checksum and actual checksum differ");
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

        // Encode record type
        auto status = core::write_u32_le(out, WAL_PAGE_FRAME_RECORD_TYPE_OFFSET, WAL_PAGE_FRAME_RECORD_TYPE);
        if(!status.ok()) {
            return status;
        }

        // Encode record size
        status = core::write_u32_le(out, WAL_PAGE_FRAME_RECORD_SIZE_OFFSET, WAL_PAGE_FRAME_RECORD_SIZE);
        if(!status.ok()) {
            return status;
        }

        // Encode transaction id
        status = core::write_u64_le(out, WAL_PAGE_FRAME_TRANSACTION_ID_OFFSET, transaction_id_);
        if(!status.ok()) {
            return status;
        }

        // Encode page id
        status = core::write_u64_le(out, WAL_PAGE_FRAME_PAGE_ID_OFFSET, page_id_.id);
        if(!status.ok()) {
            return status;
        }

        // Encode page image size
        status = core::write_u32_le(out, WAL_PAGE_FRAME_IMAGE_SIZE_OFFSET, static_cast<std::uint32_t>(core::PAGE_SIZE));
        if(!status.ok()) {
            return status;
        }

        // Encode page image
        for(std::size_t i = 0; i < core::PAGE_SIZE; i++) {
            out[WAL_PAGE_FRAME_IMAGE_OFFSET+i] = page_image_[i];
        }

        // Encode checksum
        const auto current_checksum = core::checksum(out.first(WAL_PAGE_FRAME_RECORD_SIZE-sizeof(std::uint64_t)));

        status = core::write_u64_le(out, WAL_PAGE_FRAME_CHECKSUM_OFFSET, current_checksum);
        if(!status.ok()) {
            return status;
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
