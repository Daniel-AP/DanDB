#include <dandb/catalog/Catalog.h>

#include "CatalogInitializer.h"
#include "CatalogLoader.h"

#include <utility>

namespace dandb::catalog {

    core::Status Catalog::initialize(storage::Pager& pager) {
        return CatalogInitializer(pager).initialize();
    }

    core::Result<Catalog> Catalog::load(storage::Pager& pager) {
        return CatalogLoader::load(pager);
    }

    Catalog::Catalog(
        std::unordered_map<TableId, TableInfo> table_by_id,
        std::unordered_map<TableId, record::Schema> table_schema_by_id,
        std::unordered_map<std::string, TableId> table_id_by_name
    ) :
        table_by_id_(std::move(table_by_id)),
        table_schema_by_id_(std::move(table_schema_by_id)),
        table_id_by_name_(std::move(table_id_by_name))
    {}

}
