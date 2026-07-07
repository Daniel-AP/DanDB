#include <dandb/wal/WalHeader.h>
#include <dandb/core/Bytes.h>
#include <dandb/core/Checksum.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Endian.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace dandb::wal {

    WalHeader::WalHeader(std::uint64_t database_id) :
        database_id_(database_id)
    {}

    core::Result<WalHeader> WalHeader::decode(std::span<const std::byte> bytes) {

        if(bytes.size() != WAL_HEADER_SIZE) {
            return core::Status::InvalidArgument("Cannot decode WAL header: header size is invalid");
        }

        // Validate magic bytes
        #pragma region

        std::array<std::byte, WAL_MAGIC_BYTES.size()> stored_magic{};
        for(std::size_t i = 0; i < WAL_MAGIC_BYTES.size(); i++) stored_magic[i] = bytes[WAL_MAGIC_BYTES_OFFSET+i];

        if(stored_magic != WAL_MAGIC_BYTES) {
            return core::Status::Corruption("Cannot decode WAL header: magic bytes are invalid");
        }

        #pragma endregion

        // Validate WAL format version
        #pragma region

        auto stored_wal_format_version_result = core::read_u32_le(bytes, WAL_FORMAT_VERSION_OFFSET);
        if(!stored_wal_format_version_result.ok()) {
            return stored_wal_format_version_result.status();
        }

        std::uint32_t stored_wal_format_version = stored_wal_format_version_result.value();

        if(stored_wal_format_version != WAL_FORMAT_VERSION) {
            return core::Status::Corruption("Cannot decode WAL header: unsupported WAL format version");
        }

        #pragma endregion

        // Validate page size
        #pragma region

        auto stored_page_size_result = core::read_u32_le(bytes, WAL_PAGE_SIZE_OFFSET);
        if(!stored_page_size_result.ok()) {
            return stored_page_size_result.status();
        }

        const auto stored_page_size = static_cast<std::size_t>(stored_page_size_result.value());

        if(stored_page_size != core::PAGE_SIZE) {
            return core::Status::Corruption("Cannot decode WAL header: WAL header has unsupported page size");
        }

        #pragma endregion

        // Validate header size
        #pragma region

        auto stored_header_size_result = core::read_u32_le(bytes, WAL_HEADER_SIZE_OFFSET);
        if(!stored_header_size_result.ok()) {
            return stored_header_size_result.status();
        }

        const auto stored_header_size = static_cast<std::size_t>(stored_header_size_result.value());

        if(stored_header_size != WAL_HEADER_SIZE) {
            return core::Status::Corruption("Cannot decode WAL header: WAL header has unsupported size");
        }

        #pragma endregion

        // Decode database id
        #pragma region

        auto stored_database_id_result = core::read_u64_le(bytes, WAL_DATABASE_ID_OFFSET);
        if(!stored_database_id_result.ok()) {
            return stored_database_id_result.status();
        }

        const auto stored_database_id = stored_database_id_result.value();

        #pragma endregion

        // Validate reserved bytes
        #pragma region

        if(!core::bytes_are_zero(bytes.subspan(WAL_RESERVED_BYTES_OFFSET, WAL_HEADER_RESERVED_BYTES_SIZE))) {
            return core::Status::Corruption("Cannot decode WAL header: some reserved bytes are non-zero");
        }

        #pragma endregion

        // Validate checksum
        #pragma region

        auto stored_checksum_result = core::read_u64_le(bytes, WAL_CHECKSUM_OFFSET);
        if(!stored_checksum_result.ok()) {
            return stored_checksum_result.status();
        }

        const auto stored_checksum = stored_checksum_result.value();
        const auto current_checksum = core::checksum(bytes.first(WAL_CHECKSUM_OFFSET));

        if(stored_checksum != current_checksum) {
            return core::Status::Corruption("Cannot decode WAL header: stored checksum and actual checksum differ");
        }

        #pragma endregion

        return WalHeader{
            stored_database_id
        };

    }

    WalHeader WalHeader::create_new(std::uint64_t database_id) {

        return WalHeader{database_id};

    }

    core::Status WalHeader::encode_into(std::span<std::byte> out) const {

        if(out.size() != WAL_HEADER_SIZE) {
            return core::Status::InvalidArgument("Cannot encode WAL header: header size is invalid");
        }

        for(std::size_t i = 0; i < WAL_HEADER_SIZE; i++) {
            out[i] = std::byte{ 0 };
        }

        // Encode magic bytes
        #pragma region

        for(std::size_t i = 0; i < WAL_MAGIC_BYTES.size(); i++) {
            out[WAL_MAGIC_BYTES_OFFSET+i] = WAL_MAGIC_BYTES[i];
        }

        #pragma endregion

        // Encode WAL format version
        #pragma region

        auto status = core::write_u32_le(out, WAL_FORMAT_VERSION_OFFSET, WAL_FORMAT_VERSION);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode page size
        #pragma region

        status = core::write_u32_le(out, WAL_PAGE_SIZE_OFFSET, static_cast<std::uint32_t>(core::PAGE_SIZE));
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode header size
        #pragma region

        status = core::write_u32_le(out, WAL_HEADER_SIZE_OFFSET, WAL_HEADER_SIZE);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode database id
        #pragma region

        status = core::write_u64_le(out, WAL_DATABASE_ID_OFFSET, database_id_);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode checksum
        #pragma region

        const auto current_checksum = core::checksum(out.first(WAL_CHECKSUM_OFFSET));

        status = core::write_u64_le(out, WAL_CHECKSUM_OFFSET, current_checksum);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        return core::Status::Ok();

    }

    std::uint64_t WalHeader::database_id() const {
        return database_id_;
    }

}
