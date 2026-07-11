#include <catch_amalgamated.hpp>

#include <dandb/catalog/IndexNames.h>
#include <dandb/catalog/TableId.h>

using dandb::catalog::TableId;
using dandb::catalog::internal_primary_index_name;

TEST_CASE("Internal primary index names derive from table ids", "[catalog][index-names]") {
    REQUIRE(internal_primary_index_name(TableId{ 1 }) == "dandb_internal_pk_1");
    REQUIRE(internal_primary_index_name(TableId{ 42 }) == "dandb_internal_pk_42");
}
