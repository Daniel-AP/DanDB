#include <catch_amalgamated.hpp>

#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/execution/Database.h>
#include <testutil/TempDir.h>

#include <filesystem>
#include <fstream>
#include <string_view>
#include <type_traits>
#include <utility>

using dandb::execution::Database;
using dandb::testutil::TempDir;

TEST_CASE("Database declares the lifecycle facade API", "[execution][database]") {
    STATIC_REQUIRE(std::is_same_v<
        decltype(Database::open_or_create(std::declval<std::filesystem::path>())),
        dandb::core::Result<Database>
    >);
    STATIC_REQUIRE(std::is_same_v<
        decltype(std::declval<Database&>().execute(std::declval<std::string_view>())),
        dandb::core::Status
    >);
    STATIC_REQUIRE(std::is_same_v<
        decltype(std::declval<Database&>().close()),
        dandb::core::Status
    >);
}

TEST_CASE("Database opens a new database", "[execution][database]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(database_result.ok());
    REQUIRE(std::filesystem::exists(temp_dir.database_path()));
    REQUIRE(std::filesystem::exists(temp_dir.wal_path()));
    REQUIRE(database_result.value().close().ok());
}

TEST_CASE("Database reopens an existing database", "[execution][database]") {
    const TempDir temp_dir;

    auto created_database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(created_database_result.ok());
    REQUIRE(created_database_result.value().close().ok());

    auto reopened_database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(reopened_database_result.ok());
    REQUIRE(reopened_database_result.value().close().ok());
}

TEST_CASE("Database rejects an invalid existing database file", "[execution][database]") {
    const TempDir temp_dir;

    std::ofstream invalid_database_file(temp_dir.database_path(), std::ios::binary);
    invalid_database_file << "not a DanDB database";
    invalid_database_file.close();

    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE_FALSE(database_result.ok());
}
