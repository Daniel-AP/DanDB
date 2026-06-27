#include <catch_amalgamated.hpp>

#include <dandb/core/Constants.h>

TEST_CASE("core constants define the database page size", "[core][constants]") {
    static_assert(dandb::core::PAGE_SIZE == 4096);

    REQUIRE(dandb::core::PAGE_SIZE == 4096);
}