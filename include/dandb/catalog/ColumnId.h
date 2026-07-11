#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

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

namespace std {

    template<>
    struct hash<dandb::catalog::ColumnId> {
        std::size_t operator()(const dandb::catalog::ColumnId& column_id) const noexcept {
            return std::hash<std::uint64_t>{}(column_id.id);
        }
    };

}
