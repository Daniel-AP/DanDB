#include <dandb/catalog/Catalog.h>

#include "CatalogInitializer.h"
#include "CatalogLoader.h"

#include <cstdint>
#include <limits>
#include <utility>

namespace dandb::catalog {

    core::Status Catalog::initialize(storage::Pager& pager) {
        return CatalogInitializer(pager).initialize();
    }

    core::Result<Catalog> Catalog::load(storage::Pager& pager) {
        return CatalogLoader::load(pager);
    }

    const TableDescriptor* Catalog::find_table(std::string_view name) const {

        const auto table_id_it = table_id_by_name_.find(std::string(name));
        if(table_id_it == table_id_by_name_.end()) return nullptr;

        return find_table(table_id_it->second);

    }

    const TableDescriptor* Catalog::find_table(TableId table_id) const {

        const auto table_it = table_by_id_.find(table_id);
        if(table_it == table_by_id_.end()) return nullptr;

        return &table_it->second.table_descriptor;

    }

    const record::Schema* Catalog::schema_for_table(TableId table_id) const {

        const auto schema_it = table_schema_by_id_.find(table_id);
        if(schema_it == table_schema_by_id_.end()) return nullptr;

        return &schema_it->second;

    }

    const ColumnDescriptor* Catalog::find_column(TableId table_id, std::string_view name) const {

        const auto table_it = table_by_id_.find(table_id);
        if(table_it == table_by_id_.end()) return nullptr;

        for(const auto& column: table_it->second.columns) {
            if(column.name() == name) return &column;
        }

        return nullptr;

    }

    std::span<const IndexDescriptor> Catalog::indexes_for_table(TableId table_id) const {

        const auto table_it = table_by_id_.find(table_id);
        if(table_it == table_by_id_.end()) return {};

        return table_it->second.indexes;
        
    }

    Catalog::Catalog(
        storage::Pager& pager,
        std::unordered_map<TableId, TableInfo> table_by_id,
        std::unordered_map<TableId, record::Schema> table_schema_by_id,
        std::unordered_map<std::string, TableId> table_id_by_name
    ) :
        pager_(&pager),
        next_table_id_(INVALID_TABLE_ID),
        next_column_id_(INVALID_COLUMN_ID),
        next_index_id_(INVALID_INDEX_ID),
        table_by_id_(std::move(table_by_id)),
        table_schema_by_id_(std::move(table_schema_by_id)),
        table_id_by_name_(std::move(table_id_by_name))
    {

        std::uint64_t maximum_table_id = 0;
        std::uint64_t maximum_column_id = 0;
        std::uint64_t maximum_index_id = 0;

        for(const auto& [table_id, table_info]: table_by_id_) {

            if(table_id.id > maximum_table_id) {
                maximum_table_id = table_id.id;
            }

            for(const auto& column: table_info.columns) {
                if(column.column_id().id > maximum_column_id) {
                    maximum_column_id = column.column_id().id;
                }
            }

            for(const auto& index: table_info.indexes) {
                if(index.index_id().id > maximum_index_id) {
                    maximum_index_id = index.index_id().id;
                }
            }

        }

        next_table_id_ = TableId{ maximum_table_id+1 };
        next_column_id_ = ColumnId{ maximum_column_id+1 };
        next_index_id_ = IndexId{ maximum_index_id+1 };

    }

}
