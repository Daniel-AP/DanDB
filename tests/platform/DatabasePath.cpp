#include <catch_amalgamated.hpp>

#include <dandb/platform/DatabasePath.h>

#include <filesystem>

TEST_CASE("DatabasePath keeps the main database path", "[platform][database_path]") {
    const std::filesystem::path main_path = std::filesystem::path{"data"} / "demo.dandb";
    const dandb::platform::DatabasePath database_path(main_path);

    REQUIRE(database_path.main_path() == main_path);
}

TEST_CASE("DatabasePath appends wal suffix to a dandb file", "[platform][database_path]") {
    const dandb::platform::DatabasePath database_path("demo.dandb");

    REQUIRE(database_path.wal_path() == std::filesystem::path{"demo.dandb.wal"});
}

TEST_CASE("DatabasePath appends wal suffix to a path without dandb extension", "[platform][database_path]") {
    const dandb::platform::DatabasePath database_path("demo");

    REQUIRE(database_path.wal_path() == std::filesystem::path{"demo.wal"});
}

TEST_CASE("DatabasePath appends wal suffix after the full database path", "[platform][database_path]") {
    const std::filesystem::path main_path = std::filesystem::path{"data"} / "demo.dandb";
    const dandb::platform::DatabasePath database_path(main_path);

    REQUIRE(database_path.wal_path() == std::filesystem::path{"data"} / "demo.dandb.wal");
}

TEST_CASE("DatabasePath display name uses the generic path string", "[platform][database_path]") {
    const std::filesystem::path main_path = std::filesystem::path{"data"} / "demo.dandb";
    const dandb::platform::DatabasePath database_path(main_path);

    REQUIRE(database_path.display_name() == "data/demo.dandb");
}
