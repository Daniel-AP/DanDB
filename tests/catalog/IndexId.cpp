#include <catch_amalgamated.hpp>

#include <dandb/catalog/IndexId.h>
#include <dandb/catalog/TableId.h>

#include <cstdint>
#include <limits>
#include <type_traits>

using dandb::catalog::INVALID_INDEX_ID;
using dandb::catalog::IndexId;
using dandb::catalog::TableId;

TEST_CASE("IndexId is a small strong id wrapper", "[catalog][index-id]") {
    static_assert(sizeof(IndexId) == sizeof(std::uint64_t));
    static_assert(!std::is_convertible_v<IndexId, std::uint64_t>);
    static_assert(!std::is_convertible_v<std::uint64_t, IndexId>);
    static_assert(!std::is_convertible_v<TableId, IndexId>);
    static_assert(!std::is_convertible_v<IndexId, TableId>);

    const IndexId index_id{ 42 };

    REQUIRE(index_id.id == 42);
}

TEST_CASE("IndexId invalid constant uses the max uint64 sentinel", "[catalog][index-id]") {
    static_assert(INVALID_INDEX_ID.id == std::numeric_limits<std::uint64_t>::max());

    REQUIRE(INVALID_INDEX_ID.id == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("IndexId validity distinguishes invalid sentinel from real ids", "[catalog][index-id]") {
    REQUIRE_FALSE(INVALID_INDEX_ID.is_valid());
    REQUIRE(IndexId{ 1 }.is_valid());
    REQUIRE(IndexId{ 42 }.is_valid());
}

TEST_CASE("IndexId equality compares wrapped values", "[catalog][index-id]") {
    REQUIRE(IndexId{ 7 } == IndexId{ 7 });
    REQUIRE_FALSE(IndexId{ 7 } == IndexId{ 8 });
}
