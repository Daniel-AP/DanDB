#include <dandb/catalog/Catalog.h>

#include "CatalogInitializer.h"

namespace dandb::catalog {

    core::Status Catalog::initialize(storage::Pager& pager) {
        return CatalogInitializer(pager).initialize();
    }

}
