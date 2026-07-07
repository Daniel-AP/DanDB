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

        // Validate magic bytes
        #pragma region

        std::array<std::byte, DATABASE_MAGIC_BYTES.size()> stored_magic{};
        for(std::size_t i = 0; i < DATABASE_MAGIC_BYTES.size(); i++) stored_magic[i] = page[DATABASE_MAGIC_BYTES_OFFSET+i];

        if(stored_magic != DATABASE_MAGIC_BYTES) {
            return core::Status::Corruption("Cannot decode database header: magic bytes are invalid");
        }

        #pragma endregion

        // Validate db format version
        #pragma region

        auto stored_db_format_version_result = core::read_u32_le(page, DATABASE_FORMAT_VERSION_OFFSET);
        if(!stored_db_format_version_result.ok()) {
            return stored_db_format_version_result.status();
        }

        std::uint32_t stored_db_format_version = stored_db_format_version_result.value();

        if(stored_db_format_version != DATABASE_FORMAT_VERSION) {
            return core::Status::Corruption("Cannot decode database header: unsupported database format version");
        }

        #pragma endregion

        // Validate page size
        #pragma region

        auto stored_page_size_result = core::read_u32_le(page, DATABASE_PAGE_SIZE_OFFSET);
        if(!stored_page_size_result.ok()) {
            return stored_page_size_result.status();
        }

        const auto stored_page_size = static_cast<std::size_t>(stored_page_size_result.value());

        if(stored_page_size != core::PAGE_SIZE) {
            return core::Status::Corruption("Cannot decode database header: database header has unsupported page size");
        }

        #pragma endregion

        // Validate header size
        #pragma region

        auto stored_header_size_result = core::read_u32_le(page, DATABASE_HEADER_SIZE_OFFSET);
        if(!stored_header_size_result.ok()) {
            return stored_header_size_result.status();
        }

        const auto stored_header_size = static_cast<std::size_t>(stored_header_size_result.value());

        if(stored_header_size != DATABASE_HEADER_SIZE) {
            return core::Status::Corruption("Cannot decode database header: database header has unsupported size");
        }

        #pragma endregion

        // Decode database id
        #pragma region

        auto stored_database_id_result = core::read_u64_le(page, DATABASE_ID_OFFSET);
        if(!stored_database_id_result.ok()) {
            return stored_database_id_result.status();
        }

        const auto stored_database_id = stored_database_id_result.value();

        #pragma endregion

        // Decode and validate page count
        #pragma region

        auto stored_page_count_result = core::read_u64_le(page, DATABASE_PAGE_COUNT_OFFSET);
        if(!stored_page_count_result.ok()) {
            return stored_page_count_result.status();
        }

        const auto stored_page_count = stored_page_count_result.value();

        if(stored_page_count == 0) {
            return core::Status::Corruption("Cannot decode database header: page count cannot be zero");
        }

        #pragma endregion

        // Decode catalog root page id
        #pragma region

        auto stored_catalog_root_page_id_result = core::read_u64_le(page, DATABASE_CATALOG_ROOT_PAGE_ID_OFFSET);
        if(!stored_catalog_root_page_id_result.ok()) {
            return stored_catalog_root_page_id_result.status();
        }

        const auto stored_catalog_root_page_id = PageId{ stored_catalog_root_page_id_result.value() };

        if(!root_page_id_is_valid(stored_catalog_root_page_id, stored_page_count)) {
            return core::Status::Corruption("Cannot decode database header: catalog root page id is outside database page range");
        }

        #pragma endregion

        // Decode system tables root page id
        #pragma region

        auto stored_system_tables_root_page_id_result = core::read_u64_le(page, DATABASE_SYSTEM_TABLES_ROOT_PAGE_ID_OFFSET);
        if(!stored_system_tables_root_page_id_result.ok()) {
            return stored_system_tables_root_page_id_result.status();
        }

        const auto stored_system_tables_root_page_id = PageId{ stored_system_tables_root_page_id_result.value() };

        if(!root_page_id_is_valid(stored_system_tables_root_page_id, stored_page_count)) {
            return core::Status::Corruption("Cannot decode database header: system tables root page id is outside database page range");
        }

        #pragma endregion
        
        // Decode system columns root page id
        #pragma region

        auto stored_system_columns_root_page_id_result = core::read_u64_le(page, DATABASE_SYSTEM_COLUMNS_ROOT_PAGE_ID_OFFSET);
        if(!stored_system_columns_root_page_id_result.ok()) {
            return stored_system_columns_root_page_id_result.status();
        }

        const auto stored_system_columns_root_page_id = PageId{ stored_system_columns_root_page_id_result.value() };

        if(!root_page_id_is_valid(stored_system_columns_root_page_id, stored_page_count)) {
            return core::Status::Corruption("Cannot decode database header: system columns root page id is outside database page range");
        }

        #pragma endregion

        // Decode system indexes root page id
        #pragma region

        auto stored_system_indexes_root_page_id_result = core::read_u64_le(page, DATABASE_SYSTEM_INDEXES_ROOT_PAGE_ID_OFFSET);
        if(!stored_system_indexes_root_page_id_result.ok()) {
            return stored_system_indexes_root_page_id_result.status();
        }

        const auto stored_system_indexes_root_page_id = PageId{ stored_system_indexes_root_page_id_result.value() };

        if(!root_page_id_is_valid(stored_system_indexes_root_page_id, stored_page_count)) {
            return core::Status::Corruption("Cannot decode database header: system indexes root page id is outside database page range");
        }

        #pragma endregion

        // Decode system index columns root page id
        #pragma region

        auto stored_system_index_columns_root_page_id_result = core::read_u64_le(page, DATABASE_SYSTEM_INDEX_COLUMNS_ROOT_PAGE_ID_OFFSET);
        if(!stored_system_index_columns_root_page_id_result.ok()) {
            return stored_system_index_columns_root_page_id_result.status();
        }

        const auto stored_system_index_columns_root_page_id = PageId{ stored_system_index_columns_root_page_id_result.value() };

        if(!root_page_id_is_valid(stored_system_index_columns_root_page_id, stored_page_count)) {
            return core::Status::Corruption("Cannot decode database header: system index columns root page id is outside database page range");
        }

        #pragma endregion

        // Validate reserved bytes
        #pragma region

        if(!core::bytes_are_zero(page.subspan(DATABASE_RESERVED_BYTES_OFFSET, DATABASE_HEADER_RESERVED_BYTES_SIZE))) {
            return core::Status::Corruption("Cannot decode database header: some reserved bytes are non-zero");
        }

        #pragma endregion

        // Validate checksum
        #pragma region

        auto stored_checksum_result = core::read_u64_le(page, DATABASE_CHECKSUM_OFFSET);
        if(!stored_checksum_result.ok()) {
            return stored_checksum_result.status();
        }

        const auto stored_checksum = stored_checksum_result.value();
        const auto current_checksum = core::checksum(page.first(DATABASE_CHECKSUM_OFFSET));

        if(stored_checksum != current_checksum) {
            return core::Status::Corruption("Cannot decode database header: stored checksum and actual checksum differ");
        }
        
        #pragma endregion

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

        // Encode magic bytes
        #pragma region

        for(std::size_t i = 0; i < DATABASE_MAGIC_BYTES.size(); i++) {
            page[DATABASE_MAGIC_BYTES_OFFSET+i] = DATABASE_MAGIC_BYTES[i];
        }

        #pragma endregion

        // Encode database format version
        #pragma region

        auto status = core::write_u32_le(page, DATABASE_FORMAT_VERSION_OFFSET, DATABASE_FORMAT_VERSION);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode page size
        #pragma region

        status = core::write_u32_le(page, DATABASE_PAGE_SIZE_OFFSET, static_cast<std::uint32_t>(core::PAGE_SIZE));
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode header size
        #pragma region

        status = core::write_u32_le(page, DATABASE_HEADER_SIZE_OFFSET, DATABASE_HEADER_SIZE);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode database id
        #pragma region

        status = core::write_u64_le(page, DATABASE_ID_OFFSET, database_id_);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode page count
        #pragma region

        status = core::write_u64_le(page, DATABASE_PAGE_COUNT_OFFSET, page_count_);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode catalog root page id
        #pragma region

        if(!root_page_id_is_valid(catalog_root_page_id_, page_count_)) {
            return core::Status::InvalidArgument("Cannot encode database header: catalog root page id is outside database page range");
        }

        status = core::write_u64_le(page, DATABASE_CATALOG_ROOT_PAGE_ID_OFFSET, catalog_root_page_id_.id);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode system tables root page id
        #pragma region

        if(!root_page_id_is_valid(system_tables_root_page_id_, page_count_)) {
            return core::Status::InvalidArgument("Cannot encode database header: system tables root page id is outside database page range");
        }

        status = core::write_u64_le(page, DATABASE_SYSTEM_TABLES_ROOT_PAGE_ID_OFFSET, system_tables_root_page_id_.id);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode system columns root page id
        #pragma region

        if(!root_page_id_is_valid(system_columns_root_page_id_, page_count_)) {
            return core::Status::InvalidArgument("Cannot encode database header: system columns root page id is outside database page range");
        }

        status = core::write_u64_le(page, DATABASE_SYSTEM_COLUMNS_ROOT_PAGE_ID_OFFSET, system_columns_root_page_id_.id);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode system indexes root page id
        #pragma region

        if(!root_page_id_is_valid(system_indexes_root_page_id_, page_count_)) {
            return core::Status::InvalidArgument("Cannot encode database header: system indexes root page id is outside database page range");
        }

        status = core::write_u64_le(page, DATABASE_SYSTEM_INDEXES_ROOT_PAGE_ID_OFFSET, system_indexes_root_page_id_.id);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Encode system index columns root page id
        #pragma region

        if(!root_page_id_is_valid(system_index_columns_root_page_id_, page_count_)) {
            return core::Status::InvalidArgument("Cannot encode database header: system index columns root page id is outside database page range");
        }

        status = core::write_u64_le(page, DATABASE_SYSTEM_INDEX_COLUMNS_ROOT_PAGE_ID_OFFSET, system_index_columns_root_page_id_.id);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

        // Reserved bytes are already zero because the page was cleared above.

        // Encode checksum
        #pragma region

        const auto current_checksum = core::checksum(page.first(DATABASE_CHECKSUM_OFFSET));

        status = core::write_u64_le(page, DATABASE_CHECKSUM_OFFSET, current_checksum);
        if(!status.ok()) {
            return status;
        }

        #pragma endregion

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
