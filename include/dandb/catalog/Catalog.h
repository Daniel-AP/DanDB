#pragma once

#include <dandb/catalog/ColumnDescriptor.h>
#include <dandb/catalog/IndexDescriptor.h>
#include <dandb/catalog/TableDescriptor.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/record/Schema.h>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dandb::storage {
    class Pager;
}

namespace dandb::catalog {

    class CatalogLoader;

    class Catalog {
        public:
            static core::Status initialize(storage::Pager& pager);
            static core::Result<Catalog> load(storage::Pager& pager);

            const TableDescriptor* find_table(std::string_view name) const;
            const TableDescriptor* find_table(TableId table_id) const;
            const record::Schema* schema_for_table(TableId table_id) const;
            const ColumnDescriptor* find_column(TableId table_id, std::string_view name) const;
            std::span<const IndexDescriptor> indexes_for_table(TableId table_id) const;

        private:
            friend class CatalogLoader;

            struct TableInfo {
                TableDescriptor table_descriptor;
                std::vector<ColumnDescriptor> columns;
                std::vector<IndexDescriptor> indexes;
            };

            Catalog(
                storage::Pager& pager,
                std::unordered_map<TableId, TableInfo> table_by_id,
                std::unordered_map<TableId, record::Schema> table_schema_by_id,
                std::unordered_map<std::string, TableId> table_id_by_name
            );

            storage::Pager* pager_;
            std::unordered_map<TableId, TableInfo> table_by_id_;
            std::unordered_map<TableId, record::Schema> table_schema_by_id_;
            std::unordered_map<std::string, TableId> table_id_by_name_;
    };

}
