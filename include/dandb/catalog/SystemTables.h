#pragma once

#include <dandb/catalog/IndexId.h>
#include <dandb/catalog/TableId.h>
#include <dandb/core/Result.h>
#include <dandb/record/Schema.h>

#include <cstddef>

namespace dandb::catalog {

    inline constexpr const char* DANDB_TABLES_NAME = "dandb_tables";
    inline constexpr const char* DANDB_COLUMNS_NAME = "dandb_columns";
    inline constexpr const char* DANDB_INDEXES_NAME = "dandb_indexes";
    inline constexpr const char* DANDB_INDEX_COLUMNS_NAME = "dandb_index_columns";

    inline constexpr TableId DANDB_TABLES_ID{ 1 };
    inline constexpr TableId DANDB_COLUMNS_ID{ 2 };
    inline constexpr TableId DANDB_INDEXES_ID{ 3 };
    inline constexpr TableId DANDB_INDEX_COLUMNS_ID{ 4 };

    inline constexpr IndexId DANDB_TABLES_PRIMARY_INDEX_ID{ 1 };
    inline constexpr IndexId DANDB_COLUMNS_PRIMARY_INDEX_ID{ 2 };
    inline constexpr IndexId DANDB_INDEXES_PRIMARY_INDEX_ID{ 3 };
    inline constexpr IndexId DANDB_INDEX_COLUMNS_PRIMARY_INDEX_ID{ 4 };

    inline constexpr std::size_t CATALOG_NAME_CAPACITY = 256;

    class SystemTables {
        public:
            static core::Result<record::Schema> tables_schema();
            static core::Result<record::Schema> columns_schema();
            static core::Result<record::Schema> indexes_schema();
            static core::Result<record::Schema> index_columns_schema();
    };

}
