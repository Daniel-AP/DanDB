#pragma once

#include <cstddef>
#include <vector>

namespace dandb::btree {

    struct BTreeEntry {
        std::vector<std::byte> key;
        std::vector<std::byte> value;
    };

}
