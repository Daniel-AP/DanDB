#pragma once

#include <limits>
#include <cstdint>

namespace dandb::catalog {

    struct IndexId {
        std::uint64_t id;
        constexpr bool is_valid() const {
            return id != std::numeric_limits<std::uint64_t>::max();
        }
        
        bool operator==(const IndexId&) const = default;
    };

    inline constexpr IndexId INVALID_INDEX_ID{ std::numeric_limits<std::uint64_t>::max() };

}