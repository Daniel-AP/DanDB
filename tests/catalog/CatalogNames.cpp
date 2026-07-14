#include <catch_amalgamated.hpp>

#include <dandb/catalog/CatalogNames.h>

TEST_CASE("Catalog names expose the reserved prefix", "[catalog][catalog-names]") {
    REQUIRE(dandb::catalog::RESERVED_CATALOG_PREFIX == "dandb_");
}

TEST_CASE("Catalog names identify names using the reserved prefix", "[catalog][catalog-names]") {
    REQUIRE(dandb::catalog::has_reserved_catalog_prefix("dandb_tables"));
    REQUIRE_FALSE(dandb::catalog::has_reserved_catalog_prefix("dandb"));
    REQUIRE_FALSE(dandb::catalog::has_reserved_catalog_prefix("users"));
}
