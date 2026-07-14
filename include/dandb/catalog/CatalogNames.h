#pragma once

#include <string_view>

namespace dandb::catalog {

    inline constexpr std::string_view RESERVED_CATALOG_PREFIX = "dandb_";

    constexpr bool has_reserved_catalog_prefix(std::string_view name) {
        return name.starts_with(RESERVED_CATALOG_PREFIX);
    }

}
