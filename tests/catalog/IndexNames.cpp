#include <catch_amalgamated.hpp>

#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/IndexNames.h>
#include <dandb/catalog/TableId.h>

using dandb::catalog::ColumnId;
using dandb::catalog::TableId;
using dandb::catalog::internal_primary_index_name;
using dandb::catalog::internal_unique_index_name;

TEST_CASE("Internal primary index names derive from table ids", "[catalog][index-names]") {
    REQUIRE(internal_primary_index_name(TableId{ 1 }) == "dandb_internal_pk_1");
    REQUIRE(internal_primary_index_name(TableId{ 42 }) == "dandb_internal_pk_42");
}

TEST_CASE("Internal unique index names derive from table and column ids", "[catalog][index-names]") {
    REQUIRE(internal_unique_index_name(TableId{ 5 }, ColumnId{ 23 }) == "dandb_internal_unique_5_23");
    REQUIRE(internal_unique_index_name(TableId{ 42 }, ColumnId{ 99 }) == "dandb_internal_unique_42_99");
}
