#include <catch_amalgamated.hpp>

#include <dandb/btree/BTree.h>
#include <dandb/catalog/Catalog.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/execution/Database.h>
#include <dandb/record/KeyCodec.h>
#include <dandb/record/Value.h>
#include <dandb/storage/Pager.h>
#include <testutil/TempDir.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using dandb::core::StatusCode;
using dandb::execution::Database;
using dandb::btree::BTree;
using dandb::catalog::Catalog;
using dandb::record::KeyCodec;
using dandb::record::Value;
using dandb::storage::Pager;
using dandb::testutil::TempDir;

namespace {

    constexpr std::size_t TEST_BPM_CAPACITY = 10;

    std::vector<std::byte> encode_int64_key(std::int64_t value) {

        const auto key_result = KeyCodec::encode(Value::int64(value));
        REQUIRE(key_result.ok());
        return key_result.value();

    }

}

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

TEST_CASE("Database creates an empty index through SQL", "[execution][database][ddl][create-index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
    );

    REQUIRE(results.size() == 2);
    for(const auto& result: results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    REQUIRE(database_result.value().close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());

    const auto* index_descriptor = catalog_result.value().find_index("users_by_age");
    REQUIRE(index_descriptor != nullptr);
    REQUIRE_FALSE(index_descriptor->unique());

    auto index_tree_result = BTree::open_existing(
        pager_result.value(),
        index_descriptor->root_page_id(),
        static_cast<std::uint16_t>(16),
        static_cast<std::uint16_t>(8)
    );
    REQUIRE(index_tree_result.ok());

    auto cursor_result = index_tree_result.value().scan();
    REQUIRE(cursor_result.ok());

    const auto entry_result = cursor_result.value().next();
    REQUIRE(entry_result.ok());
    REQUIRE_FALSE(entry_result.value().has_value());
    REQUIRE(pager_result.value().close().ok());

}

TEST_CASE("Database backfills a non-unique index through SQL", "[execution][database][ddl][create-index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto setup_results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "INSERT INTO users VALUES (1, 20);"
        "INSERT INTO users VALUES (2, 10);"
        "INSERT INTO users VALUES (3, 20);"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto create_index_results = database_result.value().execute(
        "CREATE INDEX users_by_age ON users(age);"
    );

    REQUIRE(create_index_results.size() == 1);
    INFO(create_index_results[0].status.message());
    REQUIRE(create_index_results[0].status.ok());
    REQUIRE(database_result.value().close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());

    const auto* index_descriptor = catalog_result.value().find_index("users_by_age");
    REQUIRE(index_descriptor != nullptr);

    auto index_tree_result = BTree::open_existing(
        pager_result.value(),
        index_descriptor->root_page_id(),
        static_cast<std::uint16_t>(16),
        static_cast<std::uint16_t>(8)
    );
    REQUIRE(index_tree_result.ok());

    auto age_ten_key = encode_int64_key(10);
    const auto id_two_key = encode_int64_key(2);
    age_ten_key.insert(age_ten_key.end(), id_two_key.begin(), id_two_key.end());

    auto age_ten_result = index_tree_result.value().find(age_ten_key);
    REQUIRE(age_ten_result.ok());
    REQUIRE(age_ten_result.value() == id_two_key);

    auto age_twenty_id_one_key = encode_int64_key(20);
    const auto id_one_key = encode_int64_key(1);
    age_twenty_id_one_key.insert(age_twenty_id_one_key.end(), id_one_key.begin(), id_one_key.end());

    auto age_twenty_id_one_result = index_tree_result.value().find(age_twenty_id_one_key);
    REQUIRE(age_twenty_id_one_result.ok());
    REQUIRE(age_twenty_id_one_result.value() == id_one_key);

    auto age_twenty_id_three_key = encode_int64_key(20);
    const auto id_three_key = encode_int64_key(3);
    age_twenty_id_three_key.insert(age_twenty_id_three_key.end(), id_three_key.begin(), id_three_key.end());

    auto age_twenty_id_three_result = index_tree_result.value().find(age_twenty_id_three_key);
    REQUIRE(age_twenty_id_three_result.ok());
    REQUIRE(age_twenty_id_three_result.value() == id_three_key);
    REQUIRE(pager_result.value().close().ok());

}

TEST_CASE("Database backfills a unique index through SQL", "[execution][database][ddl][create-index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto setup_results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "INSERT INTO users VALUES (1, 20);"
        "INSERT INTO users VALUES (2, 10);"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto create_index_results = database_result.value().execute(
        "CREATE UNIQUE INDEX users_by_age ON users(age);"
    );

    REQUIRE(create_index_results.size() == 1);
    INFO(create_index_results[0].status.message());
    REQUIRE(create_index_results[0].status.ok());
    REQUIRE(database_result.value().close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());

    const auto* index_descriptor = catalog_result.value().find_index("users_by_age");
    REQUIRE(index_descriptor != nullptr);
    REQUIRE(index_descriptor->unique());

    auto index_tree_result = BTree::open_existing(
        pager_result.value(),
        index_descriptor->root_page_id(),
        static_cast<std::uint16_t>(8),
        static_cast<std::uint16_t>(8)
    );
    REQUIRE(index_tree_result.ok());

    const auto age_ten_key = encode_int64_key(10);
    const auto id_two_key = encode_int64_key(2);
    auto age_ten_result = index_tree_result.value().find(age_ten_key);
    REQUIRE(age_ten_result.ok());
    REQUIRE(age_ten_result.value() == id_two_key);

    const auto age_twenty_key = encode_int64_key(20);
    const auto id_one_key = encode_int64_key(1);
    auto age_twenty_result = index_tree_result.value().find(age_twenty_key);
    REQUIRE(age_twenty_result.ok());
    REQUIRE(age_twenty_result.value() == id_one_key);
    REQUIRE(pager_result.value().close().ok());

}

TEST_CASE("Database rejects a duplicate unique index without persisting metadata", "[execution][database][ddl][create-index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto setup_results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "INSERT INTO users VALUES (1, 20);"
        "INSERT INTO users VALUES (2, 20);"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto create_index_results = database_result.value().execute(
        "CREATE UNIQUE INDEX users_by_age ON users(age);"
    );

    REQUIRE(create_index_results.size() == 1);
    REQUIRE(create_index_results[0].status.code() == StatusCode::ConstraintViolation);
    REQUIRE(database_result.value().close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());
    REQUIRE(catalog_result.value().find_index("users_by_age") == nullptr);

    const auto* table_descriptor = catalog_result.value().find_table("users");
    REQUIRE(table_descriptor != nullptr);
    REQUIRE(catalog_result.value().indexes_for_table(table_descriptor->table_id()).size() == 1);
    REQUIRE(pager_result.value().close().ok());

}

TEST_CASE("Database removes a created table after rollback", "[execution][database][ddl]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto begin_results = database_result.value().execute("BEGIN;");
    REQUIRE(begin_results.size() == 1);
    REQUIRE(begin_results[0].status.ok());

    const auto create_results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
    );
    REQUIRE(create_results.size() == 1);
    INFO(create_results[0].status.message());
    REQUIRE(create_results[0].status.ok());

    const auto rollback_results = database_result.value().execute("ROLLBACK;");
    REQUIRE(rollback_results.size() == 1);
    REQUIRE(rollback_results[0].status.ok());
    REQUIRE(database_result.value().close().ok());

    auto reopened_database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(reopened_database_result.ok());

    const auto drop_results = reopened_database_result.value().execute("DROP TABLE users;");

    REQUIRE(drop_results.size() == 1);
    REQUIRE(drop_results[0].status.code() == StatusCode::NotFound);
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

TEST_CASE("Database makes a parse error inside a transaction rollback-only", "[execution][database][transaction]") {
    const TempDir temp_dir;
    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto begin_results = database.execute("BEGIN;");
    REQUIRE(begin_results.size() == 1);
    REQUIRE(begin_results[0].status.ok());

    const auto parse_error_results = database.execute("INVALID SQL;");
    REQUIRE(parse_error_results.size() == 1);
    REQUIRE_FALSE(parse_error_results[0].status.ok());

    const auto rejected_results = database.execute("SELECT * FROM dandb_tables;");
    REQUIRE(rejected_results.size() == 1);
    REQUIRE(rejected_results[0].status.code() == StatusCode::TransactionError);

    const auto rollback_results = database.execute("ROLLBACK;");
    REQUIRE(rollback_results.size() == 1);
    REQUIRE(rollback_results[0].status.ok());

    REQUIRE(database.close().ok());
}

TEST_CASE("Database autocommits INSERT rows to the primary table B+ tree", "[execution][database][dml][insert]") {
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
    REQUIRE(create_results[0].status.ok());

    const auto insert_results = database_result.value().execute("INSERT INTO users VALUES (1, 'Ada');");

    REQUIRE(insert_results.size() == 1);
    INFO(insert_results[0].status.message());
    REQUIRE(insert_results[0].status.ok());
    REQUIRE(insert_results[0].rows_affected == 1);
    REQUIRE(database_result.value().close().ok());

    auto reopened_database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(reopened_database_result.ok());

    const auto duplicate_insert_results = reopened_database_result.value().execute(
        "INSERT INTO users VALUES (1, 'Ada');"
    );

    REQUIRE(duplicate_insert_results.size() == 1);
    REQUIRE(duplicate_insert_results[0].status.code() == StatusCode::ConstraintViolation);
    REQUIRE(reopened_database_result.value().close().ok());
}

TEST_CASE("Database rejects INSERT rows with duplicate primary keys", "[execution][database][dml][insert]") {
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
    REQUIRE(create_results[0].status.ok());

    const auto first_insert_results = database_result.value().execute("INSERT INTO users VALUES (1, 'Ada');");
    REQUIRE(first_insert_results.size() == 1);
    REQUIRE(first_insert_results[0].status.ok());

    const auto duplicate_insert_results = database_result.value().execute("INSERT INTO users VALUES (1, 'Grace');");

    REQUIRE(duplicate_insert_results.size() == 1);
    REQUIRE(duplicate_insert_results[0].status.code() == StatusCode::ConstraintViolation);
    REQUIRE_FALSE(duplicate_insert_results[0].rows_affected.has_value());
    REQUIRE(database_result.value().close().ok());
}

TEST_CASE("Database rejects overflow values in INSERT rows", "[execution][database][dml][insert]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto create_results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT8 PRIMARY KEY, "
        "name STRING(64)"
        ");"
    );
    REQUIRE(create_results.size() == 1);
    REQUIRE(create_results[0].status.ok());

    const auto insert_results = database_result.value().execute("INSERT INTO users VALUES (128, 'Ada');");

    REQUIRE(insert_results.size() == 1);
    REQUIRE(insert_results[0].status.code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(insert_results[0].rows_affected.has_value());
    REQUIRE(database_result.value().close().ok());
}

TEST_CASE("Database removes INSERT rows after manual transaction rollback", "[execution][database][dml][insert]") {
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
    REQUIRE(create_results[0].status.ok());

    const auto transaction_results = database_result.value().execute(
        "BEGIN; INSERT INTO users VALUES (1, 'Ada'); ROLLBACK;"
    );

    REQUIRE(transaction_results.size() == 3);
    REQUIRE(transaction_results[0].status.ok());
    REQUIRE(transaction_results[1].status.ok());
    REQUIRE(transaction_results[1].rows_affected == 1);
    REQUIRE(transaction_results[2].status.ok());

    const auto reinsert_results = database_result.value().execute("INSERT INTO users VALUES (1, 'Ada');");

    REQUIRE(reinsert_results.size() == 1);
    REQUIRE(reinsert_results[0].status.ok());
    REQUIRE(reinsert_results[0].rows_affected == 1);
    REQUIRE(database_result.value().close().ok());
}

TEST_CASE("Database preserves committed INSERT rows after reopen", "[execution][database][dml][insert]") {
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
    REQUIRE(create_results[0].status.ok());

    const auto transaction_results = database_result.value().execute(
        "BEGIN; INSERT INTO users VALUES (1, 'Ada'); COMMIT;"
    );

    REQUIRE(transaction_results.size() == 3);
    REQUIRE(transaction_results[0].status.ok());
    REQUIRE(transaction_results[1].status.ok());
    REQUIRE(transaction_results[1].rows_affected == 1);
    REQUIRE(transaction_results[2].status.ok());
    REQUIRE(database_result.value().close().ok());

    auto reopened_database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(reopened_database_result.ok());

    const auto duplicate_insert_results = reopened_database_result.value().execute(
        "INSERT INTO users VALUES (1, 'Ada');"
    );

    REQUIRE(duplicate_insert_results.size() == 1);
    REQUIRE(duplicate_insert_results[0].status.code() == StatusCode::ConstraintViolation);
    REQUIRE(reopened_database_result.value().close().ok());
}

TEST_CASE("Database executes SELECT statements", "[execution][database][dml][select]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();

    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64), "
        "nickname STRING(64)"
        ");"
        "INSERT INTO users VALUES (2, 'Grace', NULL);"
        "INSERT INTO users VALUES (1, 'Ada', 'Ada');"
        "INSERT INTO users VALUES (3, 'Linus', 'Lin');"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    SECTION("returns all rows in primary-key order") {
        const auto results = database.execute("SELECT * FROM users;");

        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].row_set.has_value());

        const auto& row_set = *results[0].row_set;
        REQUIRE(row_set.column_names == std::vector<std::string>{ "id", "name", "nickname" });
        REQUIRE(row_set.rows.size() == 3);
        REQUIRE(row_set.rows[0].value(0).as_integer() == 1);
        REQUIRE(row_set.rows[1].value(0).as_integer() == 2);
        REQUIRE(row_set.rows[2].value(0).as_integer() == 3);
    }

    SECTION("preserves projection order") {
        const auto results = database.execute("SELECT name, id FROM users;");

        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].row_set.has_value());

        const auto& row_set = *results[0].row_set;
        REQUIRE(row_set.column_names == std::vector<std::string>{ "name", "id" });
        REQUIRE(row_set.rows.size() == 3);
        REQUIRE(row_set.rows[0].value(0).as_string() == "Ada");
        REQUIRE(row_set.rows[0].value(1).as_integer() == 1);
    }

    SECTION("selects exact and range primary keys") {
        const auto exact_results = database.execute("SELECT name FROM users WHERE id = 2;");

        REQUIRE(exact_results.size() == 1);
        REQUIRE(exact_results[0].status.ok());
        REQUIRE(exact_results[0].row_set.has_value());
        REQUIRE(exact_results[0].row_set->rows.size() == 1);
        REQUIRE(exact_results[0].row_set->rows[0].value(0).as_string() == "Grace");

        const auto range_results = database.execute("SELECT id FROM users WHERE id >= 2;");

        REQUIRE(range_results.size() == 1);
        REQUIRE(range_results[0].status.ok());
        REQUIRE(range_results[0].row_set.has_value());
        REQUIRE(range_results[0].row_set->rows.size() == 2);
        REQUIRE(range_results[0].row_set->rows[0].value(0).as_integer() == 2);
        REQUIRE(range_results[0].row_set->rows[1].value(0).as_integer() == 3);
    }

    SECTION("filters a non-indexed column") {
        const auto results = database.execute("SELECT id FROM users WHERE name = 'Ada';");

        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].row_set.has_value());
        REQUIRE(results[0].row_set->rows.size() == 1);
        REQUIRE(results[0].row_set->rows[0].value(0).as_integer() == 1);
    }

    SECTION("selects system-table rows") {
        const auto results = database.execute("SELECT name FROM dandb_tables WHERE table_id = 1;");

        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].row_set.has_value());
        REQUIRE(results[0].row_set->column_names == std::vector<std::string>{ "name" });
        REQUIRE(results[0].row_set->rows.size() == 1);
        REQUIRE(results[0].row_set->rows[0].value(0).as_string() == "dandb_tables");
    }

    SECTION("applies null predicate behavior") {
        const auto is_null_results = database.execute("SELECT id FROM users WHERE nickname IS NULL;");

        REQUIRE(is_null_results.size() == 1);
        REQUIRE(is_null_results[0].status.ok());
        REQUIRE(is_null_results[0].row_set.has_value());
        REQUIRE(is_null_results[0].row_set->rows.size() == 1);
        REQUIRE(is_null_results[0].row_set->rows[0].value(0).as_integer() == 2);

        const auto is_not_null_results = database.execute("SELECT id FROM users WHERE nickname IS NOT NULL;");

        REQUIRE(is_not_null_results.size() == 1);
        REQUIRE(is_not_null_results[0].status.ok());
        REQUIRE(is_not_null_results[0].row_set.has_value());
        REQUIRE(is_not_null_results[0].row_set->rows.size() == 2);
        REQUIRE(is_not_null_results[0].row_set->rows[0].value(0).as_integer() == 1);
        REQUIRE(is_not_null_results[0].row_set->rows[1].value(0).as_integer() == 3);

        const auto null_comparison_results = database.execute("SELECT id FROM users WHERE nickname = NULL;");

        REQUIRE(null_comparison_results.size() == 1);
        REQUIRE(null_comparison_results[0].status.ok());
        REQUIRE(null_comparison_results[0].row_set.has_value());
        REQUIRE(null_comparison_results[0].row_set->rows.empty());
    }

    REQUIRE(database.close().ok());
}

TEST_CASE("Database updates one matching row", "[execution][database][dml][update]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
        "INSERT INTO users VALUES (1, 'Ada');"
        "INSERT INTO users VALUES (2, 'Grace');"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_results = database.execute("UPDATE users SET name = 'Linus' WHERE id = 2;");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.ok());
    REQUIRE(update_results[0].rows_affected == 1);

    const auto select_results = database.execute("SELECT name FROM users WHERE id = 2;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 1);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_string() == "Linus");
    REQUIRE(database.close().ok());
}

TEST_CASE("Database updates all rows without a predicate", "[execution][database][dml][update]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
        "INSERT INTO users VALUES (1, 'Ada');"
        "INSERT INTO users VALUES (2, 'Grace');"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_results = database.execute("UPDATE users SET name = 'Unknown';");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.ok());
    REQUIRE(update_results[0].rows_affected == 2);

    const auto select_results = database.execute("SELECT name FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 2);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_string() == "Unknown");
    REQUIRE(select_results[0].row_set->rows[1].value(0).as_string() == "Unknown");
    REQUIRE(database.close().ok());
}

TEST_CASE("Database rejects UPDATE of a primary key", "[execution][database][dml][update]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto create_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
    );
    REQUIRE(create_results.size() == 1);
    REQUIRE(create_results[0].status.ok());

    const auto update_results = database.execute("UPDATE users SET id = 2 WHERE id = 1;");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(update_results[0].rows_affected.has_value());
    REQUIRE(database.close().ok());
}

TEST_CASE("Database rejects an overflowing UPDATE in a manual transaction", "[execution][database][dml][update]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT8"
        ");"
        "INSERT INTO users VALUES (1, 10);"
    );

    REQUIRE(setup_results.size() == 2);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto begin_results = database.execute("BEGIN;");
    REQUIRE(begin_results.size() == 1);
    REQUIRE(begin_results[0].status.ok());

    const auto update_results = database.execute("UPDATE users SET age = 128 WHERE id = 1;");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(update_results[0].rows_affected.has_value());

    const auto rejected_results = database.execute("SELECT age FROM users WHERE id = 1;");
    REQUIRE(rejected_results.size() == 1);
    REQUIRE(rejected_results[0].status.code() == StatusCode::TransactionError);

    const auto rollback_results = database.execute("ROLLBACK;");
    REQUIRE(rollback_results.size() == 1);
    REQUIRE(rollback_results[0].status.ok());
    REQUIRE(database.close().ok());
}

TEST_CASE("Database restores rows updated in a rolled-back transaction", "[execution][database][dml][update]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
        "INSERT INTO users VALUES (1, 'Ada');"
    );

    REQUIRE(setup_results.size() == 2);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto transaction_results = database.execute(
        "BEGIN; UPDATE users SET name = 'Grace' WHERE id = 1; ROLLBACK;"
    );

    REQUIRE(transaction_results.size() == 3);
    REQUIRE(transaction_results[0].status.ok());
    REQUIRE(transaction_results[1].status.ok());
    REQUIRE(transaction_results[1].rows_affected == 1);
    REQUIRE(transaction_results[2].status.ok());

    const auto select_results = database.execute("SELECT name FROM users WHERE id = 1;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 1);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_string() == "Ada");
    REQUIRE(database.close().ok());
}

TEST_CASE("Database deletes one matching row", "[execution][database][dml][delete]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
        "INSERT INTO users VALUES (1, 'Ada');"
        "INSERT INTO users VALUES (2, 'Grace');"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users WHERE id = 2;");

    REQUIRE(delete_results.size() == 1);
    REQUIRE(delete_results[0].status.ok());
    REQUIRE(delete_results[0].rows_affected == 1);

    const auto select_results = database.execute("SELECT id FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 1);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database deletes all rows without a predicate", "[execution][database][dml][delete]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
        "INSERT INTO users VALUES (1, 'Ada');"
        "INSERT INTO users VALUES (2, 'Grace');"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users;");

    REQUIRE(delete_results.size() == 1);
    REQUIRE(delete_results[0].status.ok());
    REQUIRE(delete_results[0].rows_affected == 2);

    const auto select_results = database.execute("SELECT * FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.empty());
    REQUIRE(database.close().ok());
}

TEST_CASE("Database reports zero affected rows when DELETE matches no rows", "[execution][database][dml][delete]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
        "INSERT INTO users VALUES (1, 'Ada');"
    );

    REQUIRE(setup_results.size() == 2);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users WHERE id = 2;");

    REQUIRE(delete_results.size() == 1);
    REQUIRE(delete_results[0].status.ok());
    REQUIRE(delete_results[0].rows_affected == 0);

    const auto select_results = database.execute("SELECT id FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 1);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database restores deleted rows after transaction rollback", "[execution][database][dml][delete]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
        "INSERT INTO users VALUES (1, 'Ada');"
        "INSERT INTO users VALUES (2, 'Grace');"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto transaction_results = database.execute(
        "BEGIN; DELETE FROM users WHERE id = 2; ROLLBACK;"
    );

    REQUIRE(transaction_results.size() == 3);
    REQUIRE(transaction_results[0].status.ok());
    REQUIRE(transaction_results[1].status.ok());
    REQUIRE(transaction_results[1].rows_affected == 1);
    REQUIRE(transaction_results[2].status.ok());

    const auto select_results = database.execute("SELECT id FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 2);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(select_results[0].row_set->rows[1].value(0).as_integer() == 2);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database preserves committed deletes after reopen", "[execution][database][dml][delete]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "name STRING(64)"
        ");"
        "INSERT INTO users VALUES (1, 'Ada');"
        "INSERT INTO users VALUES (2, 'Grace');"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto transaction_results = database.execute(
        "BEGIN; DELETE FROM users WHERE id = 2; COMMIT;"
    );

    REQUIRE(transaction_results.size() == 3);
    REQUIRE(transaction_results[0].status.ok());
    REQUIRE(transaction_results[1].status.ok());
    REQUIRE(transaction_results[1].rows_affected == 1);
    REQUIRE(transaction_results[2].status.ok());
    REQUIRE(database.close().ok());

    auto reopened_database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(reopened_database_result.ok());

    const auto select_results = reopened_database_result.value().execute("SELECT id FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 1);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(reopened_database_result.value().close().ok());
}
