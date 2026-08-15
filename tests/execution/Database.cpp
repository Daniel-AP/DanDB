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
#include <vector>

using dandb::core::StatusCode;
using dandb::execution::Database;
using dandb::testutil::TempDir;

TEST_CASE("Database declares the lifecycle facade API", "[execution][database]") {
    STATIC_REQUIRE(std::is_same_v<
        decltype(Database::open_or_create(std::declval<std::filesystem::path>())),
        dandb::core::Result<Database>
    >);
    STATIC_REQUIRE(std::is_same_v<
        decltype(std::declval<Database&>().execute(std::declval<std::string_view>())),
        std::vector<dandb::execution::ExecutionResult>
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

TEST_CASE("Database persists a table created through SQL", "[execution][database][ddl]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto create_results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
    );

    REQUIRE(create_results.size() == 1);
    INFO(create_results[0].status.message());
    REQUIRE(create_results[0].status.ok());
    REQUIRE(create_results[0].success_message == "Table 'users' created");
    REQUIRE(database_result.value().close().ok());

    auto reopened_database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(reopened_database_result.ok());

    const auto duplicate_create_results = reopened_database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
    );

    REQUIRE(duplicate_create_results.size() == 1);
    REQUIRE(duplicate_create_results[0].status.code() == StatusCode::AlreadyExists);
    REQUIRE(reopened_database_result.value().close().ok());
}

TEST_CASE("Database persists a table dropped through SQL", "[execution][database][ddl]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto create_results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
    );
    REQUIRE(create_results.size() == 1);
    INFO(create_results[0].status.message());
    REQUIRE(create_results[0].status.ok());

    const auto drop_results = database_result.value().execute("DROP TABLE users;");

    REQUIRE(drop_results.size() == 1);
    REQUIRE(drop_results[0].status.ok());
    REQUIRE(drop_results[0].success_message == "Table 'users' dropped");
    REQUIRE(database_result.value().close().ok());

    auto reopened_database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(reopened_database_result.ok());

    const auto recreate_results = reopened_database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
    );

    REQUIRE(recreate_results.size() == 1);
    REQUIRE(recreate_results[0].status.ok());
    REQUIRE(reopened_database_result.value().close().ok());
}

TEST_CASE("Database restores a dropped table after rollback", "[execution][database][ddl]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto create_results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
    );
    REQUIRE(create_results.size() == 1);
    INFO(create_results[0].status.message());
    REQUIRE(create_results[0].status.ok());

    const auto begin_results = database_result.value().execute("BEGIN;");
    REQUIRE(begin_results.size() == 1);
    REQUIRE(begin_results[0].status.ok());

    const auto drop_results = database_result.value().execute("DROP TABLE users;");
    REQUIRE(drop_results.size() == 1);
    REQUIRE(drop_results[0].status.ok());

    const auto rollback_results = database_result.value().execute("ROLLBACK;");
    REQUIRE(rollback_results.size() == 1);
    REQUIRE(rollback_results[0].status.ok());

    const auto duplicate_create_results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
    );

    REQUIRE(duplicate_create_results.size() == 1);
    REQUIRE(duplicate_create_results[0].status.code() == StatusCode::AlreadyExists);
    REQUIRE(database_result.value().close().ok());
}

TEST_CASE("Database rejects DROP TABLE for a missing table", "[execution][database][ddl]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto results = database_result.value().execute("DROP TABLE missing;");

    REQUIRE(results.size() == 1);
    REQUIRE(results[0].status.code() == StatusCode::NotFound);
    REQUIRE(database_result.value().close().ok());
}

TEST_CASE("Database rejects DROP TABLE for a system table", "[execution][database][ddl]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto results = database_result.value().execute("DROP TABLE dandb_tables;");

    REQUIRE(results.size() == 1);
    REQUIRE(results[0].status.code() == StatusCode::InvalidArgument);
    REQUIRE(database_result.value().close().ok());
}

TEST_CASE("Database rejects an invalid existing database file", "[execution][database]") {
    const TempDir temp_dir;

    std::ofstream invalid_database_file(temp_dir.database_path(), std::ios::binary);
    invalid_database_file << "not a DanDB database";
    invalid_database_file.close();

    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE_FALSE(database_result.ok());
}

TEST_CASE("Database executes transaction control statements", "[execution][database][transaction]") {
    const TempDir temp_dir;
    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    SECTION("commits an empty transaction") {
        const auto results = database.execute("BEGIN; COMMIT;");

        REQUIRE(results.size() == 2);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].success_message == "Transaction started");
        REQUIRE(results[1].status.ok());
        REQUIRE(results[1].success_message == "Transaction committed");
    }

    SECTION("rolls back an empty transaction") {
        const auto results = database.execute("BEGIN; ROLLBACK;");

        REQUIRE(results.size() == 2);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].success_message == "Transaction started");
        REQUIRE(results[1].status.ok());
        REQUIRE(results[1].success_message == "Transaction rolled back");
    }

    SECTION("checkpoints outside a transaction") {
        const auto results = database.execute("CHECKPOINT;");

        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].success_message == "Checkpoint completed");
    }

    REQUIRE(database.close().ok());
}

TEST_CASE("Database rejects invalid transaction control", "[execution][database][transaction]") {
    const TempDir temp_dir;
    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    SECTION("rejects commit without an active transaction") {
        const auto results = database.execute("COMMIT;");

        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.code() == StatusCode::TransactionError);
        REQUIRE_FALSE(results[0].success_message.has_value());
    }

    SECTION("rejects rollback without an active transaction") {
        const auto results = database.execute("ROLLBACK;");

        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.code() == StatusCode::TransactionError);
        REQUIRE_FALSE(results[0].success_message.has_value());
    }

    SECTION("stops after a nested begin fails") {
        const auto results = database.execute("BEGIN; BEGIN; COMMIT;");

        REQUIRE(results.size() == 2);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[1].status.code() == StatusCode::TransactionError);

        const auto rollback_results = database.execute("ROLLBACK;");
        REQUIRE(rollback_results.size() == 1);
        REQUIRE(rollback_results[0].status.ok());
    }

    SECTION("rejects checkpoint during an active transaction") {
        const auto begin_results = database.execute("BEGIN;");
        REQUIRE(begin_results.size() == 1);
        REQUIRE(begin_results[0].status.ok());

        const auto checkpoint_results = database.execute("CHECKPOINT;");
        REQUIRE(checkpoint_results.size() == 1);
        REQUIRE(checkpoint_results[0].status.code() == StatusCode::TransactionError);

        const auto rollback_results = database.execute("ROLLBACK;");
        REQUIRE(rollback_results.size() == 1);
        REQUIRE(rollback_results[0].status.ok());
    }

    REQUIRE(database.close().ok());
}

TEST_CASE("Database makes a failed transaction rollback-only", "[execution][database][transaction]") {
    const TempDir temp_dir;
    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto begin_results = database.execute("BEGIN;");
    REQUIRE(begin_results.size() == 1);
    REQUIRE(begin_results[0].status.ok());

    const auto error_results = database.execute("SELECT * FROM missing;");
    REQUIRE(error_results.size() == 1);
    REQUIRE(error_results[0].status.code() == StatusCode::NotFound);

    const auto rejected_results = database.execute("SELECT * FROM dandb_tables;");
    REQUIRE(rejected_results.size() == 1);
    REQUIRE(rejected_results[0].status.code() == StatusCode::TransactionError);

    const auto rollback_results = database.execute("ROLLBACK;");
    REQUIRE(rollback_results.size() == 1);
    REQUIRE(rollback_results[0].status.ok());
    REQUIRE(rollback_results[0].success_message == "Transaction rolled back");

    const auto reused_results = database.execute("BEGIN; COMMIT;");
    REQUIRE(reused_results.size() == 2);
    REQUIRE(reused_results[0].status.ok());
    REQUIRE(reused_results[1].status.ok());

    REQUIRE(database.close().ok());
}
