#pragma once

#include <dandb/catalog/ColumnDescriptor.h>
#include <dandb/catalog/IndexDescriptor.h>
#include <dandb/catalog/TableDescriptor.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/record/Schema.h>

#include <span>
#include <optional>
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

            core::Status create_table(std::string name, const record::Schema& schema);
            core::Status on_transaction_committed();
            core::Status on_transaction_rolled_back();

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

            struct CatalogState {
                TableId next_table_id_;
                ColumnId next_column_id_;
                IndexId next_index_id_;
                std::unordered_map<TableId, TableInfo> table_by_id_;
                std::unordered_map<TableId, record::Schema> table_schema_by_id_;
                std::unordered_map<std::string, TableId> table_id_by_name_;
            };

            Catalog(
                storage::Pager& pager,
                std::unordered_map<TableId, TableInfo> table_by_id,
                std::unordered_map<TableId, record::Schema> table_schema_by_id,
                std::unordered_map<std::string, TableId> table_id_by_name
            );

            const CatalogState& visible_state() const;
            core::Status handle_mutation_failure(core::Status failure_status, bool owns_transaction);

            storage::Pager* pager_;
            CatalogState committed_state_;
            std::optional<CatalogState> staged_state_;
    };

}
