#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

namespace dandb::catalog {

    struct TableId {
        std::uint64_t id;
        constexpr bool is_valid() const {
            return id != std::numeric_limits<std::uint64_t>::max();
        }
        
        bool operator==(const TableId&) const = default;
    };

    inline constexpr TableId INVALID_TABLE_ID{ std::numeric_limits<std::uint64_t>::max() };

}

namespace std {

    template<>
    struct hash<dandb::catalog::TableId> {
        std::size_t operator()(const dandb::catalog::TableId& table_id) const noexcept {
            return std::hash<std::uint64_t>{}(table_id.id);
        }
    };

}
