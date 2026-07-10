#include <catch_amalgamated.hpp>

#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/IndexId.h>
#include <dandb/catalog/TableId.h>

#include <cstdint>
#include <limits>
#include <type_traits>

using dandb::catalog::ColumnId;
using dandb::catalog::IndexId;
using dandb::catalog::INVALID_COLUMN_ID;
using dandb::catalog::TableId;

TEST_CASE("ColumnId is a small strong id wrapper", "[catalog][column-id]") {
    static_assert(sizeof(ColumnId) == sizeof(std::uint64_t));
    static_assert(!std::is_convertible_v<ColumnId, std::uint64_t>);
    static_assert(!std::is_convertible_v<std::uint64_t, ColumnId>);
    static_assert(!std::is_convertible_v<TableId, ColumnId>);
    static_assert(!std::is_convertible_v<ColumnId, TableId>);
    static_assert(!std::is_convertible_v<IndexId, ColumnId>);
    static_assert(!std::is_convertible_v<ColumnId, IndexId>);

    const ColumnId column_id{ 42 };

    REQUIRE(column_id.id == 42);
}

TEST_CASE("ColumnId invalid constant uses the max uint64 sentinel", "[catalog][column-id]") {
    static_assert(INVALID_COLUMN_ID.id == std::numeric_limits<std::uint64_t>::max());

    REQUIRE(INVALID_COLUMN_ID.id == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("ColumnId validity distinguishes invalid sentinel from real ids", "[catalog][column-id]") {
    REQUIRE_FALSE(INVALID_COLUMN_ID.is_valid());
    REQUIRE(ColumnId{ 1 }.is_valid());
    REQUIRE(ColumnId{ 42 }.is_valid());
}

TEST_CASE("ColumnId equality compares wrapped values", "[catalog][column-id]") {
    REQUIRE(ColumnId{ 7 } == ColumnId{ 7 });
    REQUIRE_FALSE(ColumnId{ 7 } == ColumnId{ 8 });
}
