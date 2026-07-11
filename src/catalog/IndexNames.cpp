#include <dandb/catalog/IndexNames.h>

#include <string>

namespace dandb::catalog {

    std::string internal_primary_index_name(TableId table_id) {
        return "dandb_internal_pk_"+std::to_string(table_id.id);
    }

}
