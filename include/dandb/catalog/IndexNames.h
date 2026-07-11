#pragma once

#include <dandb/catalog/TableId.h>

#include <string>

namespace dandb::catalog {

    std::string internal_primary_index_name(TableId table_id);

}
