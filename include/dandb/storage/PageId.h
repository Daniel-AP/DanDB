#pragma once

#include <limits>
#include <cstdint>

namespace dandb::storage {

    struct PageId {
        std::uint64_t id;
        constexpr bool is_valid() const {
            return id != std::numeric_limits<std::uint64_t>::max();
        }
        
        bool operator==(const PageId&) const = default;
    };

    inline constexpr PageId INVALID_PAGE_ID{ std::numeric_limits<std::uint64_t>::max() };
    inline constexpr PageId HEADER_PAGE_ID{ 0 };
    inline constexpr PageId FIRST_ALLOCATABLE_PAGE_ID{ 1 };

}