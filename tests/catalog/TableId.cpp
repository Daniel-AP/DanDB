#include <catch_amalgamated.hpp>

#include <dandb/catalog/TableId.h>

#include <cstdint>
#include <limits>
#include <type_traits>

using dandb::catalog::INVALID_TABLE_ID;
using dandb::catalog::TableId;

TEST_CASE("TableId is a small strong id wrapper", "[catalog][table-id]") {
    static_assert(sizeof(TableId) == sizeof(std::uint64_t));
    static_assert(!std::is_convertible_v<TableId, std::uint64_t>);
    static_assert(!std::is_convertible_v<std::uint64_t, TableId>);

    const TableId table_id{ 42 };

    REQUIRE(table_id.id == 42);
}

TEST_CASE("TableId invalid constant uses the max uint64 sentinel", "[catalog][table-id]") {
    static_assert(INVALID_TABLE_ID.id == std::numeric_limits<std::uint64_t>::max());

    REQUIRE(INVALID_TABLE_ID.id == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("TableId validity distinguishes invalid sentinel from real ids", "[catalog][table-id]") {
    REQUIRE_FALSE(INVALID_TABLE_ID.is_valid());
    REQUIRE(TableId{ 1 }.is_valid());
    REQUIRE(TableId{ 42 }.is_valid());
}

TEST_CASE("TableId equality compares wrapped values", "[catalog][table-id]") {
    REQUIRE(TableId{ 7 } == TableId{ 7 });
    REQUIRE_FALSE(TableId{ 7 } == TableId{ 8 });
}
