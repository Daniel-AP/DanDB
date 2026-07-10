#pragma once

#include <dandb/core/Result.h>
#include <dandb/record/Schema.h>

#include <cstddef>

namespace dandb::catalog {

    inline constexpr const char* DANDB_TABLES_NAME = "dandb_tables";
    inline constexpr const char* DANDB_COLUMNS_NAME = "dandb_columns";
    inline constexpr const char* DANDB_INDEXES_NAME = "dandb_indexes";
    inline constexpr const char* DANDB_INDEX_COLUMNS_NAME = "dandb_index_columns";

    inline constexpr std::size_t CATALOG_NAME_CAPACITY = 256;

    class SystemTables {
        public:
            static core::Result<record::Schema> tables_schema();
            static core::Result<record::Schema> columns_schema();
            static core::Result<record::Schema> indexes_schema();
            static core::Result<record::Schema> index_columns_schema();
    };

}
