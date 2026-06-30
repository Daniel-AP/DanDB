#include <dandb/storage/DatabaseHeader.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Endian.h>
#include <dandb/core/Checksum.h>
#include <dandb/core/Bytes.h>

#include <array>

namespace dandb::storage {

    core::Result<DatabaseHeader> DatabaseHeader::decode(std::span<const std::byte> page) {

        if(page.size() != core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot decode database header: page size is invalid");
        }

        std::size_t offset = 0;

        // Validate magic bytes
        #pragma region

        std::array<std::byte, 4> stored_magic{};
        for(std::size_t i = 0; i < 4; i++) stored_magic[i] = page[i];

        if(stored_magic != DATABASE_MAGIC_BYTES) {
            return core::Status::Corruption("Cannot decode database header: magic bytes are invalid");
        }

        offset += DATABASE_MAGIC_BYTES.size();
        #pragma endregion

        // Validate db format version
        #pragma region

        auto stored_db_format_version_result = core::read_u32_le(page, offset);
        if(!stored_db_format_version_result.ok()) {
            return stored_db_format_version_result.status();
        }

        std::uint32_t stored_db_format_version = stored_db_format_version_result.value();

        if(stored_db_format_version != DATABASE_FORMAT_VERSION) {
            return core::Status::Corruption("Cannot decode database header: unsupported database format version");
        }

        offset += sizeof(std::uint32_t);
        #pragma endregion

        // Validate page size
        #pragma region

        auto stored_page_size_result = core::read_u32_le(page, offset);
        if(!stored_page_size_result.ok()) {
            return stored_page_size_result.status();
        }

        const auto stored_page_size = static_cast<std::size_t>(stored_page_size_result.value());

        if(stored_page_size != core::PAGE_SIZE) {
            return core::Status::Corruption("Cannot decode database header: database header has unsupported page size");
        }

        offset += sizeof(std::uint32_t);
        #pragma endregion

        // Validate header size
        #pragma region

        auto stored_header_size_result = core::read_u32_le(page, offset);
        if(!stored_header_size_result.ok()) {
            return stored_header_size_result.status();
        }

        const auto stored_header_size = static_cast<std::size_t>(stored_header_size_result.value());

        if(stored_header_size != DATABASE_HEADER_SIZE) {
            return core::Status::Corruption("Cannot decode database header: database header has unsupported size");
        }

        offset += sizeof(std::uint32_t);
        #pragma endregion

        // Decode database id
        #pragma region

        auto stored_database_id_result = core::read_u64_le(page, offset);
        if(!stored_database_id_result.ok()) {
            return stored_database_id_result.status();
        }

        const auto stored_database_id = stored_database_id_result.value();

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Decode and validate page count
        #pragma region

        auto stored_page_count_result = core::read_u64_le(page, offset);
        if(!stored_page_count_result.ok()) {
            return stored_page_count_result.status();
        }

        const auto stored_page_count = stored_page_count_result.value();

        if(stored_page_count == 0) {
            return core::Status::Corruption("Cannot decode database header: page count cannot be zero");
        }

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Decode catalog root page id
        #pragma region

        auto stored_catalog_root_page_id_result = core::read_u64_le(page, offset);
        if(!stored_catalog_root_page_id_result.ok()) {
            return stored_catalog_root_page_id_result.status();
        }

        const auto stored_catalog_root_page_id = PageId{ stored_catalog_root_page_id_result.value() };

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Decode system tables root page id
        #pragma region

        auto stored_system_tables_root_page_id_result = core::read_u64_le(page, offset);
        if(!stored_system_tables_root_page_id_result.ok()) {
            return stored_system_tables_root_page_id_result.status();
        }

        const auto stored_system_tables_root_page_id = PageId{ stored_system_tables_root_page_id_result.value() };

        offset += sizeof(std::uint64_t);
        #pragma endregion
        
        // Decode system columns root page id
        #pragma region

        auto stored_system_columns_root_page_id_result = core::read_u64_le(page, offset);
        if(!stored_system_columns_root_page_id_result.ok()) {
            return stored_system_columns_root_page_id_result.status();
        }

        const auto stored_system_columns_root_page_id = PageId{ stored_system_columns_root_page_id_result.value() };

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Decode system indexes root page id
        #pragma region

        auto stored_system_indexes_root_page_id_result = core::read_u64_le(page, offset);
        if(!stored_system_indexes_root_page_id_result.ok()) {
            return stored_system_indexes_root_page_id_result.status();
        }

        const auto stored_system_indexes_root_page_id = PageId{ stored_system_indexes_root_page_id_result.value() };

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Decode system index columns root page id
        #pragma region

        auto stored_system_index_columns_root_page_id_result = core::read_u64_le(page, offset);
        if(!stored_system_index_columns_root_page_id_result.ok()) {
            return stored_system_index_columns_root_page_id_result.status();
        }

        const auto stored_system_index_columns_root_page_id = PageId{ stored_system_index_columns_root_page_id_result.value() };

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Validate 
        #pragma region

        if(!core::bytes_are_zero(page.subspan(offset, 48))) {
            return core::Status::Corruption("Cannot decode database header: some reserved bytes are non-zero");
        }

        offset += 48;
        #pragma endregion

        // Validate checksum
        #pragma region

        auto stored_checksum_result = core::read_u64_le(page, offset);
        if(!stored_checksum_result.ok()) {
            return stored_checksum_result.status();
        }

        const auto stored_checksum = stored_checksum_result.value();
        const auto current_checksum = core::checksum(page.first(DATABASE_HEADER_SIZE-sizeof(std::uint64_t)));

        if(stored_checksum != current_checksum) {
            return core::Status::Corruption("Cannot decode database header: stored checksum and actual checksum differ");
        }
        
        offset += sizeof(std::uint64_t);
        #pragma endregion

        if(offset != DATABASE_HEADER_SIZE) {
            return core::Status::InternalError("Database header decode ended at an unexpected offset");
        }

        return DatabaseHeader{
            stored_database_id,
            stored_page_count,
            stored_catalog_root_page_id,
            stored_system_tables_root_page_id,
            stored_system_columns_root_page_id,
            stored_system_indexes_root_page_id,
            stored_system_index_columns_root_page_id
        };

    }

}