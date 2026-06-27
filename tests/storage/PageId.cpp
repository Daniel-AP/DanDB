#include <catch_amalgamated.hpp>

#include <dandb/storage/PageId.h>

#include <cstdint>
#include <limits>
#include <type_traits>

using dandb::storage::FIRST_ALLOCATABLE_PAGE_ID;
using dandb::storage::HEADER_PAGE_ID;
using dandb::storage::INVALID_PAGE_ID;
using dandb::storage::PageId;

TEST_CASE("PageId is a small strong id wrapper", "[storage][page-id]") {
    static_assert(sizeof(PageId) == sizeof(std::uint64_t));
    static_assert(!std::is_convertible_v<PageId, std::uint64_t>);
    static_assert(!std::is_convertible_v<std::uint64_t, PageId>);

    const PageId page_id{ 42 };

    REQUIRE(page_id.id == 42);
}

TEST_CASE("PageId constants reserve header page and invalid page", "[storage][page-id]") {
    static_assert(INVALID_PAGE_ID.id == std::numeric_limits<std::uint64_t>::max());
    static_assert(HEADER_PAGE_ID.id == 0);
    static_assert(FIRST_ALLOCATABLE_PAGE_ID.id == 1);

    REQUIRE(INVALID_PAGE_ID.id == std::numeric_limits<std::uint64_t>::max());
    REQUIRE(HEADER_PAGE_ID.id == 0);
    REQUIRE(FIRST_ALLOCATABLE_PAGE_ID.id == 1);
}

TEST_CASE("PageId validity distinguishes invalid sentinel from real pages", "[storage][page-id]") {
    REQUIRE_FALSE(INVALID_PAGE_ID.is_valid());
    REQUIRE(HEADER_PAGE_ID.is_valid());
    REQUIRE(FIRST_ALLOCATABLE_PAGE_ID.is_valid());
    REQUIRE(PageId{ 42 }.is_valid());
}

TEST_CASE("PageId equality compares wrapped values", "[storage][page-id]") {
    REQUIRE(PageId{ 7 } == PageId{ 7 });
    REQUIRE_FALSE(PageId{ 7 } == PageId{ 8 });
}
