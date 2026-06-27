#pragma once

#include <limits>
#include <cstdint>

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