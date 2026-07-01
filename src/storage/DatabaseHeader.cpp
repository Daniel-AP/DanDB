#include <dandb/storage/DatabaseHeader.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Endian.h>
#include <dandb/core/Checksum.h>
#include <dandb/core/Bytes.h>

#include <array>
#include <cstdint>
#include <cstddef>

namespace {

    bool root_page_id_is_valid(dandb::storage::PageId page_id, std::uint64_t page_count) {
        return page_id == dandb::storage::INVALID_PAGE_ID || (page_id.id >= dandb::storage::FIRST_ALLOCATABLE_PAGE_ID.id && page_id.id < page_count);
    }

}

namespace dandb::storage {

    DatabaseHeader::DatabaseHeader(
        std::uint64_t database_id,
        std::uint64_t page_count,
        PageId catalog_root_page_id,
        PageId system_tables_root_page_id,
        PageId system_columns_root_page_id,
        PageId system_indexes_root_page_id,
        PageId system_index_columns_root_page_id
    ) :
        database_id_(database_id),
        page_count_(page_count),
        catalog_root_page_id_(catalog_root_page_id),
        system_tables_root_page_id_(system_tables_root_page_id),
        system_columns_root_page_id_(system_columns_root_page_id),
        system_indexes_root_page_id_(system_indexes_root_page_id),
        system_index_columns_root_page_id_(system_index_columns_root_page_id)
    {}

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

        if(!root_page_id_is_valid(stored_catalog_root_page_id, stored_page_count)) {
            return core::Status::Corruption("Cannot decode database header: catalog root page id is outside database page range");
        }

        #pragma endregion

        // Decode system tables root page id
        #pragma region

        auto stored_system_tables_root_page_id_result = core::read_u64_le(page, offset);
        if(!stored_system_tables_root_page_id_result.ok()) {
            return stored_system_tables_root_page_id_result.status();
        }

        const auto stored_system_tables_root_page_id = PageId{ stored_system_tables_root_page_id_result.value() };

        offset += sizeof(std::uint64_t);

        if(!root_page_id_is_valid(stored_system_tables_root_page_id, stored_page_count)) {
            return core::Status::Corruption("Cannot decode database header: system tables root page id is outside database page range");
        }

        #pragma endregion
        
        // Decode system columns root page id
        #pragma region

        auto stored_system_columns_root_page_id_result = core::read_u64_le(page, offset);
        if(!stored_system_columns_root_page_id_result.ok()) {
            return stored_system_columns_root_page_id_result.status();
        }

        const auto stored_system_columns_root_page_id = PageId{ stored_system_columns_root_page_id_result.value() };

        offset += sizeof(std::uint64_t);

        if(!root_page_id_is_valid(stored_system_columns_root_page_id, stored_page_count)) {
            return core::Status::Corruption("Cannot decode database header: system columns root page id is outside database page range");
        }

        #pragma endregion

        // Decode system indexes root page id
        #pragma region

        auto stored_system_indexes_root_page_id_result = core::read_u64_le(page, offset);
        if(!stored_system_indexes_root_page_id_result.ok()) {
            return stored_system_indexes_root_page_id_result.status();
        }

        const auto stored_system_indexes_root_page_id = PageId{ stored_system_indexes_root_page_id_result.value() };

        offset += sizeof(std::uint64_t);

        if(!root_page_id_is_valid(stored_system_indexes_root_page_id, stored_page_count)) {
            return core::Status::Corruption("Cannot decode database header: system indexes root page id is outside database page range");
        }

        #pragma endregion

        // Decode system index columns root page id
        #pragma region

        auto stored_system_index_columns_root_page_id_result = core::read_u64_le(page, offset);
        if(!stored_system_index_columns_root_page_id_result.ok()) {
            return stored_system_index_columns_root_page_id_result.status();
        }

        const auto stored_system_index_columns_root_page_id = PageId{ stored_system_index_columns_root_page_id_result.value() };

        offset += sizeof(std::uint64_t);

        if(!root_page_id_is_valid(stored_system_index_columns_root_page_id, stored_page_count)) {
            return core::Status::Corruption("Cannot decode database header: system index columns root page id is outside database page range");
        }

        #pragma endregion

        // Validate reserved bytes
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

    DatabaseHeader DatabaseHeader::create_new(std::uint64_t database_id) {

        return DatabaseHeader{
            database_id,
            INITIAL_DATABASE_PAGE_COUNT,
            INVALID_PAGE_ID,
            INVALID_PAGE_ID,
            INVALID_PAGE_ID,
            INVALID_PAGE_ID,
            INVALID_PAGE_ID
        };

    }

    core::Status DatabaseHeader::encode_into(std::span<std::byte> page) const {

        if(page.size() != core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot encode database header: page size is invalid");
        }

        if(page_count_ == 0) {
            return core::Status::InvalidArgument("Cannot encode database header: page count cannot be zero");
        }

        for(std::size_t i = 0; i < core::PAGE_SIZE; i++) {
            page[i] = std::byte{ 0 };
        }

        std::size_t offset = 0;

        // Encode magic bytes
        #pragma region

        for(std::size_t i = 0; i < DATABASE_MAGIC_BYTES.size(); i++) {
            page[offset+i] = DATABASE_MAGIC_BYTES[i];
        }

        offset += DATABASE_MAGIC_BYTES.size();
        #pragma endregion

        // Encode database format version
        #pragma region

        auto status = core::write_u32_le(page, offset, DATABASE_FORMAT_VERSION);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint32_t);
        #pragma endregion

        // Encode page size
        #pragma region

        status = core::write_u32_le(page, offset, static_cast<std::uint32_t>(core::PAGE_SIZE));
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint32_t);
        #pragma endregion

        // Encode header size
        #pragma region

        status = core::write_u32_le(page, offset, DATABASE_HEADER_SIZE);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint32_t);
        #pragma endregion

        // Encode database id
        #pragma region

        status = core::write_u64_le(page, offset, database_id_);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Encode page count
        #pragma region

        status = core::write_u64_le(page, offset, page_count_);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Encode catalog root page id
        #pragma region

        if(!root_page_id_is_valid(catalog_root_page_id_, page_count_)) {
            return core::Status::InvalidArgument("Cannot encode database header: catalog root page id is outside database page range");
        }

        status = core::write_u64_le(page, offset, catalog_root_page_id_.id);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Encode system tables root page id
        #pragma region

        if(!root_page_id_is_valid(system_tables_root_page_id_, page_count_)) {
            return core::Status::InvalidArgument("Cannot encode database header: system tables root page id is outside database page range");
        }

        status = core::write_u64_le(page, offset, system_tables_root_page_id_.id);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Encode system columns root page id
        #pragma region

        if(!root_page_id_is_valid(system_columns_root_page_id_, page_count_)) {
            return core::Status::InvalidArgument("Cannot encode database header: system columns root page id is outside database page range");
        }

        status = core::write_u64_le(page, offset, system_columns_root_page_id_.id);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Encode system indexes root page id
        #pragma region

        if(!root_page_id_is_valid(system_indexes_root_page_id_, page_count_)) {
            return core::Status::InvalidArgument("Cannot encode database header: system indexes root page id is outside database page range");
        }

        status = core::write_u64_le(page, offset, system_indexes_root_page_id_.id);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Encode system index columns root page id
        #pragma region

        if(!root_page_id_is_valid(system_index_columns_root_page_id_, page_count_)) {
            return core::Status::InvalidArgument("Cannot encode database header: system index columns root page id is outside database page range");
        }

        status = core::write_u64_le(page, offset, system_index_columns_root_page_id_.id);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);
        #pragma endregion

        // Reserved bytes
        offset += 48;

        // Encode checksum
        #pragma region

        const auto current_checksum = core::checksum(page.first(DATABASE_HEADER_SIZE-sizeof(std::uint64_t)));

        status = core::write_u64_le(page, offset, current_checksum);
        if(!status.ok()) {
            return status;
        }

        offset += sizeof(std::uint64_t);
        #pragma endregion

        if(offset != DATABASE_HEADER_SIZE) {
            return core::Status::InternalError("Database header encode ended at an unexpected offset");
        }

        return core::Status::Ok();

    }

    std::uint64_t DatabaseHeader::database_id() const {
        return database_id_;
    }

    std::uint64_t DatabaseHeader::page_count() const {
        return page_count_;
    }

    PageId DatabaseHeader::catalog_root_page_id() const {
        return catalog_root_page_id_;
    }

    PageId DatabaseHeader::system_tables_root_page_id() const {
        return system_tables_root_page_id_;
    }

    PageId DatabaseHeader::system_columns_root_page_id() const {
        return system_columns_root_page_id_;
    }

    PageId DatabaseHeader::system_indexes_root_page_id() const {
        return system_indexes_root_page_id_;
    }

    PageId DatabaseHeader::system_index_columns_root_page_id() const {
        return system_index_columns_root_page_id_;
    }

    void DatabaseHeader::set_page_count(std::uint64_t page_count) {
        page_count_ = page_count;
    }

    void DatabaseHeader::set_catalog_root_page_id(PageId page_id) {
        catalog_root_page_id_ = page_id;
    }

    void DatabaseHeader::set_system_tables_root_page_id(PageId page_id) {
        system_tables_root_page_id_ = page_id;
    }

    void DatabaseHeader::set_system_columns_root_page_id(PageId page_id) {
        system_columns_root_page_id_ = page_id;
    }

    void DatabaseHeader::set_system_indexes_root_page_id(PageId page_id) {
        system_indexes_root_page_id_ = page_id;
    }

    void DatabaseHeader::set_system_index_columns_root_page_id(PageId page_id) {
        system_index_columns_root_page_id_ = page_id;
    }

}
