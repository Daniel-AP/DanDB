#pragma once

#include <limits>
#include <cstdint>

namespace dandb::catalog {

    struct ColumnId {
        std::uint64_t id;
        constexpr bool is_valid() const {
            return id != std::numeric_limits<std::uint64_t>::max();
        }
        
        bool operator==(const ColumnId&) const = default;
    };

    inline constexpr ColumnId INVALID_COLUMN_ID{ std::numeric_limits<std::uint64_t>::max() };

}
