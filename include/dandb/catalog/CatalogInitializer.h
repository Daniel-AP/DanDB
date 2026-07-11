#pragma once

#include <dandb/core/Status.h>

namespace dandb::storage {
    class Pager;
}

namespace dandb::catalog {

    class CatalogInitializer {
        public:
            static core::Status initialize(storage::Pager& pager);
    };

}
