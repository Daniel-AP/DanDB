#pragma once

#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/TableId.h>

#include <string>

namespace dandb::catalog {

    std::string internal_primary_index_name(TableId table_id);
    std::string internal_unique_index_name(TableId table_id, ColumnId column_id);

}
