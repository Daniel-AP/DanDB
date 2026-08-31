#include <catch_amalgamated.hpp>

#include <dandb/execution/Database.h>

#include <testutil/CrashTestHarness.h>
#include <testutil/TempDir.h>

#include <chrono>
#include <filesystem>
#include <utility>

TEST_CASE("Database recovers after a crash worker is terminated", "[crash][recovery]") {
    const dandb::testutil::TempDir temp_dir;
    const std::filesystem::path worker_path{ DANDB_CRASH_TEST_WORKER_PATH };

    auto crash_harness_result = dandb::testutil::CrashTestHarness::start(
        worker_path,
        temp_dir.path(),
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64) NOT NULL"
        ");"
        "INSERT INTO users VALUES (1, 'Ada');"
    );

    INFO(crash_harness_result.status().message());
    REQUIRE(crash_harness_result.ok());

    auto crash_harness = std::move(crash_harness_result.value());
    const auto ready_status = crash_harness.wait_for_ready(std::chrono::milliseconds{ 5000 });
    const auto terminate_status = crash_harness.terminate(std::chrono::milliseconds{ 5000 });

    INFO(ready_status.message());
    REQUIRE(ready_status.ok());
    INFO(terminate_status.message());
    REQUIRE(terminate_status.ok());

    auto recovered_database_result = dandb::execution::Database::open_or_create(temp_dir.database_path());

    INFO(recovered_database_result.status().message());
    REQUIRE(recovered_database_result.ok());

    auto& recovered_database = recovered_database_result.value();
    const auto recovery_results = recovered_database.execute("SELECT id, name FROM users;");

    REQUIRE(recovery_results.size() == 1);
    REQUIRE(recovery_results[0].status.ok());
    REQUIRE(recovery_results[0].row_set.has_value());
    REQUIRE(recovery_results[0].row_set->rows.size() == 1);
    REQUIRE(recovery_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(recovery_results[0].row_set->rows[0].value(1).as_string() == "Ada");
    REQUIRE(recovered_database.close().ok());
}
