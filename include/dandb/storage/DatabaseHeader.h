#pragma once

#include <dandb/storage/PageId.h>
#include <dandb/core/Status.h>
#include <dandb/core/Result.h>

#include <array>
#include <cstdint>
#include <span>
#include <cstddef>

namespace dandb::storage {

    inline constexpr std::array<std::byte, 4> DATABASE_MAGIC_BYTES{
        std::byte{ 'D' },
        std::byte{ 'D' },
        std::byte{ 'B' },
        std::byte{ '1' },
    };

    inline constexpr std::uint32_t DATABASE_FORMAT_VERSION = 1;
    inline constexpr std::uint32_t DATABASE_HEADER_SIZE = 128;
    inline constexpr std::size_t DATABASE_HEADER_RESERVED_BYTES_SIZE = 48;
    inline constexpr std::uint64_t INITIAL_DATABASE_PAGE_COUNT = 1;

    inline constexpr std::size_t DATABASE_MAGIC_BYTES_OFFSET = 0;
    inline constexpr std::size_t DATABASE_FORMAT_VERSION_OFFSET = DATABASE_MAGIC_BYTES_OFFSET+DATABASE_MAGIC_BYTES.size();
    inline constexpr std::size_t DATABASE_PAGE_SIZE_OFFSET = DATABASE_FORMAT_VERSION_OFFSET+sizeof(std::uint32_t);
    inline constexpr std::size_t DATABASE_HEADER_SIZE_OFFSET = DATABASE_PAGE_SIZE_OFFSET+sizeof(std::uint32_t);
    inline constexpr std::size_t DATABASE_ID_OFFSET = DATABASE_HEADER_SIZE_OFFSET+sizeof(std::uint32_t);
    inline constexpr std::size_t DATABASE_PAGE_COUNT_OFFSET = DATABASE_ID_OFFSET+sizeof(std::uint64_t);
    inline constexpr std::size_t DATABASE_CATALOG_ROOT_PAGE_ID_OFFSET = DATABASE_PAGE_COUNT_OFFSET+sizeof(std::uint64_t);
    inline constexpr std::size_t DATABASE_SYSTEM_TABLES_ROOT_PAGE_ID_OFFSET = DATABASE_CATALOG_ROOT_PAGE_ID_OFFSET+sizeof(std::uint64_t);
    inline constexpr std::size_t DATABASE_SYSTEM_COLUMNS_ROOT_PAGE_ID_OFFSET = DATABASE_SYSTEM_TABLES_ROOT_PAGE_ID_OFFSET+sizeof(std::uint64_t);
    inline constexpr std::size_t DATABASE_SYSTEM_INDEXES_ROOT_PAGE_ID_OFFSET = DATABASE_SYSTEM_COLUMNS_ROOT_PAGE_ID_OFFSET+sizeof(std::uint64_t);
    inline constexpr std::size_t DATABASE_SYSTEM_INDEX_COLUMNS_ROOT_PAGE_ID_OFFSET = DATABASE_SYSTEM_INDEXES_ROOT_PAGE_ID_OFFSET+sizeof(std::uint64_t);
    inline constexpr std::size_t DATABASE_RESERVED_BYTES_OFFSET = DATABASE_SYSTEM_INDEX_COLUMNS_ROOT_PAGE_ID_OFFSET+sizeof(std::uint64_t);
    inline constexpr std::size_t DATABASE_CHECKSUM_OFFSET = DATABASE_RESERVED_BYTES_OFFSET+DATABASE_HEADER_RESERVED_BYTES_SIZE;

    class DatabaseHeader {
        public:
            static core::Result<DatabaseHeader> decode(std::span<const std::byte> page);
            static DatabaseHeader create_new(std::uint64_t database_id);

            core::Status encode_into(std::span<std::byte> page) const;

            std::uint64_t database_id() const;
            std::uint64_t page_count() const;
            PageId catalog_root_page_id() const;
            PageId system_tables_root_page_id() const;
            PageId system_columns_root_page_id() const;
            PageId system_indexes_root_page_id() const;
            PageId system_index_columns_root_page_id() const;

            void set_page_count(std::uint64_t page_count);
            void set_catalog_root_page_id(PageId page_id);
            void set_system_tables_root_page_id(PageId page_id);
            void set_system_columns_root_page_id(PageId page_id);
            void set_system_indexes_root_page_id(PageId page_id);
            void set_system_index_columns_root_page_id(PageId page_id);

        private:
            DatabaseHeader(
                std::uint64_t database_id,
                std::uint64_t page_count,
                PageId catalog_root_page_id,
                PageId system_tables_root_page_id,
                PageId system_columns_root_page_id,
                PageId system_indexes_root_page_id,
                PageId system_index_columns_root_page_id
            );

            std::uint64_t database_id_;
            std::uint64_t page_count_;
            PageId catalog_root_page_id_;
            PageId system_tables_root_page_id_;
            PageId system_columns_root_page_id_;
            PageId system_indexes_root_page_id_;
            PageId system_index_columns_root_page_id_;
    };

}
