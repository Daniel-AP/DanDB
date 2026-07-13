#pragma once

#include <dandb/catalog/Catalog.h>
#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/IndexId.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/record/Schema.h>

#include <string>
#include <unordered_map>

namespace dandb::storage {
    class DatabaseHeader;
    class Pager;
}

namespace dandb::catalog {

    class CatalogLoader {
        public:
            static core::Result<Catalog> load(storage::Pager& pager);

        private:
            struct LoadState {
                std::unordered_map<TableId, Catalog::TableInfo> table_by_id;
                std::unordered_map<TableId, record::Schema> table_schema_by_id;
                std::unordered_map<std::string, TableId> table_id_by_name;
                std::unordered_map<ColumnId, TableId> table_id_by_column_id;
                std::unordered_map<IndexId, ColumnId> column_id_by_index_id;
            };

            static core::Status load_tables(
                storage::Pager& pager,
                const storage::DatabaseHeader& header,
                LoadState& state
            );

            static core::Status load_columns(
                storage::Pager& pager,
                const storage::DatabaseHeader& header,
                LoadState& state
            );

            static core::Status load_index_columns(
                storage::Pager& pager,
                const storage::DatabaseHeader& header,
                LoadState& state
            );

            static core::Status load_indexes(
                storage::Pager& pager,
                const storage::DatabaseHeader& header,
                LoadState& state
            );

            static core::Status resolve_schemas(LoadState& state);
    };

}
