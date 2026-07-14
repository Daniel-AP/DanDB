#include <dandb/catalog/IndexNames.h>

#include <dandb/catalog/CatalogNames.h>

#include <string>

namespace dandb::catalog {

    std::string internal_primary_index_name(TableId table_id) {
        return std::string{ RESERVED_CATALOG_PREFIX }+"internal_pk_"+std::to_string(table_id.id);
    }

    std::string internal_unique_index_name(TableId table_id, ColumnId column_id) {
        return std::string{ RESERVED_CATALOG_PREFIX }+"internal_unique_"+std::to_string(table_id.id)+"_"+std::to_string(column_id.id);
    }

}
