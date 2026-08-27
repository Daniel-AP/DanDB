#include <catch_amalgamated.hpp>

#include <dandb/btree/BTree.h>
#include <dandb/catalog/Catalog.h>
#include <dandb/catalog/IndexDescriptor.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/execution/Database.h>
#include <dandb/platform/FileFaultInjector.h>
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

    class FailNextSyncInjector final : public dandb::platform::FileFaultInjector {
        public:
            dandb::core::Status before_sync() override {
                if(!fail_next_sync_) return dandb::core::Status::Ok();

                fail_next_sync_ = false;
                return dandb::core::Status::IoError("Injected WAL sync failure");
            }

        private:
            bool fail_next_sync_ = true;
    };

    std::vector<std::byte> encode_int64_key(std::int64_t value) {

        const auto key_result = KeyCodec::encode(Value::int64(value));
        REQUIRE(key_result.ok());
        return key_result.value();

    }

    BTree open_int64_index_tree(
        Pager& pager,
        const dandb::catalog::IndexDescriptor& index_descriptor
    ) {

        const std::uint16_t key_size = index_descriptor.unique() ? 8 : 16;
        auto index_tree_result = BTree::open_existing(
            pager,
            index_descriptor.root_page_id(),
            key_size,
            static_cast<std::uint16_t>(8)
        );
        REQUIRE(index_tree_result.ok());
        return std::move(index_tree_result.value());

    }

    std::vector<std::byte> int64_index_key(
        const dandb::catalog::IndexDescriptor& index_descriptor,
        std::int64_t indexed_value,
        std::int64_t primary_key
    ) {

        auto key = encode_int64_key(indexed_value);
        if(!index_descriptor.unique()) {
            const auto primary_key_bytes = encode_int64_key(primary_key);
            key.insert(key.end(), primary_key_bytes.begin(), primary_key_bytes.end());
        }

        return key;

    }

    void require_int64_index_entry(
        Pager& pager,
        const dandb::catalog::IndexDescriptor& index_descriptor,
        std::int64_t indexed_value,
        std::int64_t primary_key
    ) {

        auto index_tree = open_int64_index_tree(pager, index_descriptor);
        const auto entry_result = index_tree.find(
            int64_index_key(index_descriptor, indexed_value, primary_key)
        );

        REQUIRE(entry_result.ok());
        REQUIRE(entry_result.value() == encode_int64_key(primary_key));

    }

    void require_missing_int64_index_entry(
        Pager& pager,
        const dandb::catalog::IndexDescriptor& index_descriptor,
        std::int64_t indexed_value,
        std::int64_t primary_key
    ) {

        auto index_tree = open_int64_index_tree(pager, index_descriptor);
        const auto entry_result = index_tree.find(
            int64_index_key(index_descriptor, indexed_value, primary_key)
        );

        REQUIRE_FALSE(entry_result.ok());
        REQUIRE(entry_result.status().code() == StatusCode::NotFound);

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

TEST_CASE("Database preserves case-sensitive table names with mixed-case keywords", "[execution][database][ddl][identifiers]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto create_results = database.execute(
        "cReAtE tAbLe Users (Id INT64 pRiMaRy kEy);"
    );

    REQUIRE(create_results.size() == 1);
    INFO(create_results[0].status.message());
    REQUIRE(create_results[0].status.ok());

    const auto exact_case_results = database.execute("sElEcT * fRoM Users;");

    REQUIRE(exact_case_results.size() == 1);
    INFO(exact_case_results[0].status.message());
    REQUIRE(exact_case_results[0].status.ok());
    REQUIRE(exact_case_results[0].row_set.has_value());
    REQUIRE(exact_case_results[0].row_set->rows.empty());

    const auto different_case_results = database.execute("SELECT * FROM users;");

    REQUIRE(different_case_results.size() == 1);
    REQUIRE(different_case_results[0].status.code() == StatusCode::NotFound);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database treats case-only table-name variants as distinct", "[execution][database][ddl][identifiers]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE Users (Id INT64 PRIMARY KEY);"
        "CREATE TABLE users (id INT64 PRIMARY KEY);"
        "INSERT INTO Users VALUES (1);"
        "INSERT INTO users VALUES (2);"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto users_results = database.execute("SELECT Id FROM Users;");
    const auto lowercase_users_results = database.execute("SELECT id FROM users;");

    REQUIRE(users_results.size() == 1);
    REQUIRE(users_results[0].status.ok());
    REQUIRE(users_results[0].row_set.has_value());
    REQUIRE(users_results[0].row_set->rows.size() == 1);
    REQUIRE(users_results[0].row_set->rows[0].value(0).as_integer() == 1);

    REQUIRE(lowercase_users_results.size() == 1);
    REQUIRE(lowercase_users_results[0].status.ok());
    REQUIRE(lowercase_users_results[0].row_set.has_value());
    REQUIRE(lowercase_users_results[0].row_set->rows.size() == 1);
    REQUIRE(lowercase_users_results[0].row_set->rows[0].value(0).as_integer() == 2);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database treats case-only column-name variants as distinct", "[execution][database][ddl][identifiers]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE Users (Id INT64 PRIMARY KEY, id INT64 NOT NULL);"
        "INSERT INTO Users VALUES (1, 2);"
    );

    REQUIRE(setup_results.size() == 2);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto upper_case_results = database.execute("SELECT Id FROM Users;");
    const auto lower_case_results = database.execute("SELECT id FROM Users;");
    const auto different_case_results = database.execute("SELECT ID FROM Users;");

    REQUIRE(upper_case_results.size() == 1);
    REQUIRE(upper_case_results[0].status.ok());
    REQUIRE(upper_case_results[0].row_set.has_value());
    REQUIRE(upper_case_results[0].row_set->rows.size() == 1);
    REQUIRE(upper_case_results[0].row_set->rows[0].value(0).as_integer() == 1);

    REQUIRE(lower_case_results.size() == 1);
    REQUIRE(lower_case_results[0].status.ok());
    REQUIRE(lower_case_results[0].row_set.has_value());
    REQUIRE(lower_case_results[0].row_set->rows.size() == 1);
    REQUIRE(lower_case_results[0].row_set->rows[0].value(0).as_integer() == 2);

    REQUIRE(different_case_results.size() == 1);
    REQUIRE(different_case_results[0].status.code() == StatusCode::NotFound);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database treats case-only index-name variants as distinct", "[execution][database][ddl][identifiers]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE Users (Id INT64 PRIMARY KEY, Age INT64 NOT NULL);"
        "CREATE TABLE users (id INT64 PRIMARY KEY, age INT64 NOT NULL);"
        "CREATE INDEX AgeLookup ON Users(Age);"
        "CREATE INDEX agelookup ON users(age);"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto different_case_results = database.execute("DROP INDEX AGELOOKUP;");
    const auto upper_case_results = database.execute("DROP INDEX AgeLookup;");
    const auto lower_case_results = database.execute("DROP INDEX agelookup;");

    REQUIRE(different_case_results.size() == 1);
    REQUIRE(different_case_results[0].status.code() == StatusCode::NotFound);

    REQUIRE(upper_case_results.size() == 1);
    REQUIRE(upper_case_results[0].status.ok());

    REQUIRE(lower_case_results.size() == 1);
    REQUIRE(lower_case_results[0].status.ok());
    REQUIRE(database.close().ok());
}

TEST_CASE("Database reserves system catalog names only with exact case", "[execution][database][ddl][identifiers]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto create_results = database.execute(
        "CREATE TABLE DANDB_tables (id INT64 PRIMARY KEY);"
    );

    REQUIRE(create_results.size() == 1);
    INFO(create_results[0].status.message());
    REQUIRE(create_results[0].status.ok());

    const auto select_results = database.execute("SELECT * FROM DANDB_tables;");
    const auto system_insert_results = database.execute(
        "INSERT INTO dandb_tables VALUES (99, 'shadow', 1);"
    );

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.empty());

    REQUIRE(system_insert_results.size() == 1);
    REQUIRE(system_insert_results[0].status.code() == StatusCode::InvalidArgument);
    REQUIRE(database.close().ok());
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
    REQUIRE(
        create_index_results[0].status.message() ==
        "Cannot create unique index 'users_by_age' on table 'users': duplicate values in column 'age'"
    );
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

TEST_CASE("Database clears failed autocommit CREATE INDEX catalog state", "[execution][database][ddl][create-index][d13-t04]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
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

    const auto failed_create_results = database.execute(
        "CREATE UNIQUE INDEX users_by_age ON users(age);"
    );

    REQUIRE(failed_create_results.size() == 1);
    REQUIRE(failed_create_results[0].status.code() == StatusCode::ConstraintViolation);

    const auto create_index_results = database.execute(
        "CREATE INDEX users_by_age ON users(age);"
    );

    REQUIRE(create_index_results.size() == 1);
    INFO(create_index_results[0].status.message());
    REQUIRE(create_index_results[0].status.ok());
    REQUIRE(database.close().ok());

}

TEST_CASE("Database allows multiple user indexes alongside automatic indexes", "[execution][database][ddl][create-index][d12-t09]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "account_code INT64 UNIQUE, "
        "tax_code INT64 UNIQUE, "
        "age INT64 NOT NULL, "
        "score INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "CREATE UNIQUE INDEX users_by_score ON users(score);"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto duplicate_primary_index_results = database.execute(
        "CREATE INDEX users_by_id ON users(id);"
    );
    REQUIRE(duplicate_primary_index_results.size() == 1);
    REQUIRE(duplicate_primary_index_results[0].status.code() == StatusCode::InvalidArgument);
    REQUIRE(duplicate_primary_index_results[0].status.message() == "Cannot create index 'users_by_id': column 'id' in table 'users' already has an index");

    const auto duplicate_internal_unique_index_results = database.execute(
        "CREATE INDEX users_by_account_code ON users(account_code);"
    );
    REQUIRE(duplicate_internal_unique_index_results.size() == 1);
    REQUIRE(duplicate_internal_unique_index_results[0].status.code() == StatusCode::InvalidArgument);
    REQUIRE(duplicate_internal_unique_index_results[0].status.message() == "Cannot create index 'users_by_account_code': column 'account_code' in table 'users' already has an index");

    const auto duplicate_user_index_results = database.execute(
        "CREATE INDEX users_by_age_again ON users(age);"
    );
    REQUIRE(duplicate_user_index_results.size() == 1);
    REQUIRE(duplicate_user_index_results[0].status.code() == StatusCode::InvalidArgument);
    REQUIRE(duplicate_user_index_results[0].status.message() == "Cannot create index 'users_by_age_again': column 'age' in table 'users' already has an index");

    REQUIRE(database.close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());
    const auto& catalog = catalog_result.value();

    const auto* table_descriptor = catalog.find_table("users");
    REQUIRE(table_descriptor != nullptr);

    const auto indexes = catalog.indexes_for_table(table_descriptor->table_id());
    REQUIRE(indexes.size() == 5);

    std::size_t primary_index_count = 0;
    std::size_t internal_unique_index_count = 0;
    for(const auto& index: indexes) {
        if(index.primary()) {
            primary_index_count++;
        } else if(index.internal() && index.unique()) {
            internal_unique_index_count++;
        }
    }

    REQUIRE(primary_index_count == 1);
    REQUIRE(internal_unique_index_count == 2);

    const auto* age_index = catalog.find_index("users_by_age");
    REQUIRE(age_index != nullptr);
    REQUIRE_FALSE(age_index->unique());
    REQUIRE_FALSE(age_index->primary());
    REQUIRE_FALSE(age_index->internal());

    const auto* score_index = catalog.find_index("users_by_score");
    REQUIRE(score_index != nullptr);
    REQUIRE(score_index->unique());
    REQUIRE_FALSE(score_index->primary());
    REQUIRE_FALSE(score_index->internal());

    REQUIRE(pager_result.value().close().ok());

}

TEST_CASE("Database drops one user index while another remains usable", "[execution][database][ddl][drop-index][d12-t09]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL, "
        "score INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "CREATE UNIQUE INDEX users_by_score ON users(score);"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto drop_index_results = database.execute("DROP INDEX users_by_age;");
    REQUIRE(drop_index_results.size() == 1);
    INFO(drop_index_results[0].status.message());
    REQUIRE(drop_index_results[0].status.ok());

    const auto insert_results = database.execute("INSERT INTO users VALUES (1, 20, 30);");
    REQUIRE(insert_results.size() == 1);
    INFO(insert_results[0].status.message());
    REQUIRE(insert_results[0].status.ok());

    REQUIRE(database.close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());
    const auto& catalog = catalog_result.value();

    REQUIRE(catalog.find_index("users_by_age") == nullptr);

    const auto* score_index = catalog.find_index("users_by_score");
    REQUIRE(score_index != nullptr);
    REQUIRE(score_index->unique());
    REQUIRE_FALSE(score_index->internal());
    require_int64_index_entry(pager_result.value(), *score_index, 30, 1);

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

TEST_CASE("Database makes a lexer error inside a transaction rollback-only", "[execution][database][transaction]") {
    const TempDir temp_dir;
    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto begin_results = database.execute("BEGIN;");
    REQUIRE(begin_results.size() == 1);
    REQUIRE(begin_results[0].status.ok());

    const auto lexer_error_results = database.execute("!;");
    REQUIRE(lexer_error_results.size() == 1);
    REQUIRE(lexer_error_results[0].status.code() == StatusCode::ParseError);

    const auto rejected_results = database.execute("SELECT * FROM dandb_tables;");
    REQUIRE(rejected_results.size() == 1);
    REQUIRE(rejected_results[0].status.code() == StatusCode::TransactionError);

    const auto rollback_results = database.execute("ROLLBACK;");
    REQUIRE(rollback_results.size() == 1);
    REQUIRE(rollback_results[0].status.ok());
    REQUIRE(database.close().ok());
}

TEST_CASE("Database makes a constraint error inside a transaction rollback-only", "[execution][database][transaction]") {
    const TempDir temp_dir;
    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto create_results = database.execute(
        "CREATE TABLE users (id INT64 PRIMARY KEY, name STRING(64));"
    );
    REQUIRE(create_results.size() == 1);
    REQUIRE(create_results[0].status.ok());

    const auto first_insert_results = database.execute("INSERT INTO users VALUES (1, 'Ada');");
    REQUIRE(first_insert_results.size() == 1);
    REQUIRE(first_insert_results[0].status.ok());

    const auto begin_results = database.execute("BEGIN;");
    REQUIRE(begin_results.size() == 1);
    REQUIRE(begin_results[0].status.ok());

    const auto constraint_error_results = database.execute("INSERT INTO users VALUES (1, 'Grace');");
    REQUIRE(constraint_error_results.size() == 1);
    REQUIRE(constraint_error_results[0].status.code() == StatusCode::ConstraintViolation);

    const auto rejected_results = database.execute("SELECT * FROM users;");
    REQUIRE(rejected_results.size() == 1);
    REQUIRE(rejected_results[0].status.code() == StatusCode::TransactionError);

    const auto rollback_results = database.execute("ROLLBACK;");
    REQUIRE(rollback_results.size() == 1);
    REQUIRE(rollback_results[0].status.ok());

    const auto reused_results = database.execute("INSERT INTO users VALUES (2, 'Grace');");
    REQUIRE(reused_results.size() == 1);
    REQUIRE(reused_results[0].status.ok());
    REQUIRE(database.close().ok());
}

TEST_CASE("Database keeps a transaction usable after incomplete parser input", "[execution][database][transaction]") {
    const TempDir temp_dir;
    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto create_results = database.execute("CREATE TABLE users (id INT64 PRIMARY KEY);");
    REQUIRE(create_results.size() == 1);
    REQUIRE(create_results[0].status.ok());

    const auto begin_results = database.execute("BEGIN;");
    REQUIRE(begin_results.size() == 1);
    REQUIRE(begin_results[0].status.ok());

    const auto incomplete_results = database.execute("INSERT INTO users VALUES (");
    REQUIRE(incomplete_results.size() == 1);
    REQUIRE(incomplete_results[0].status.code() == StatusCode::IncompleteInput);

    const auto insert_results = database.execute("INSERT INTO users VALUES (1);");
    REQUIRE(insert_results.size() == 1);
    REQUIRE(insert_results[0].status.ok());

    const auto rollback_results = database.execute("ROLLBACK;");
    REQUIRE(rollback_results.size() == 1);
    REQUIRE(rollback_results[0].status.ok());
    REQUIRE(database.close().ok());
}

TEST_CASE("Database rejects SQL while a transaction is unresolved", "[execution][database][transaction]") {
    const TempDir temp_dir;
    auto database_result = Database::open_or_create(temp_dir.database_path());

    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto create_results = database.execute("CREATE TABLE users (id INT64 PRIMARY KEY);");
    REQUIRE(create_results.size() == 1);
    REQUIRE(create_results[0].status.ok());

    const auto begin_results = database.execute("BEGIN;");
    REQUIRE(begin_results.size() == 1);
    REQUIRE(begin_results[0].status.ok());

    const auto insert_results = database.execute("INSERT INTO users VALUES (1);");
    REQUIRE(insert_results.size() == 1);
    REQUIRE(insert_results[0].status.ok());

    FailNextSyncInjector injector;
    database.set_wal_fault_injector(&injector);

    const auto commit_results = database.execute("COMMIT;");
    REQUIRE(commit_results.size() == 1);
    REQUIRE(commit_results[0].status.code() == StatusCode::IoError);

    constexpr std::string_view UNRESOLVED_MESSAGE =
        "Cannot execute statement: transaction is unresolved; close and reopen the database to recover";

    const auto malformed_results = database.execute("!;");
    REQUIRE(malformed_results.size() == 1);
    REQUIRE(malformed_results[0].status.code() == StatusCode::TransactionError);
    REQUIRE(malformed_results[0].status.message() == UNRESOLVED_MESSAGE);

    const auto select_results = database.execute("SELECT * FROM users;");
    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.code() == StatusCode::TransactionError);
    REQUIRE(select_results[0].status.message() == UNRESOLVED_MESSAGE);

    const auto rollback_results = database.execute("ROLLBACK;");
    REQUIRE(rollback_results.size() == 1);
    REQUIRE(rollback_results[0].status.code() == StatusCode::TransactionError);
    REQUIRE(rollback_results[0].status.message() == UNRESOLVED_MESSAGE);
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
    REQUIRE(
        duplicate_insert_results[0].status.message() ==
        "Cannot insert into table 'users': duplicate primary key value"
    );
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

TEST_CASE("Database maintains non-unique indexes on INSERT", "[execution][database][dml][insert][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    const auto setup_results = database_result.value().execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20);"
        "INSERT INTO users VALUES (2, 20);"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
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

    const auto id_one_key = encode_int64_key(1);
    auto age_twenty_id_one_key = encode_int64_key(20);
    age_twenty_id_one_key.insert(age_twenty_id_one_key.end(), id_one_key.begin(), id_one_key.end());

    const auto age_twenty_id_one_result = index_tree_result.value().find(age_twenty_id_one_key);
    REQUIRE(age_twenty_id_one_result.ok());
    REQUIRE(age_twenty_id_one_result.value() == id_one_key);

    const auto id_two_key = encode_int64_key(2);
    auto age_twenty_id_two_key = encode_int64_key(20);
    age_twenty_id_two_key.insert(age_twenty_id_two_key.end(), id_two_key.begin(), id_two_key.end());

    const auto age_twenty_id_two_result = index_tree_result.value().find(age_twenty_id_two_key);
    REQUIRE(age_twenty_id_two_result.ok());
    REQUIRE(age_twenty_id_two_result.value() == id_two_key);
    REQUIRE(pager_result.value().close().ok());
}

TEST_CASE("Database maintains internal unique indexes on INSERT", "[execution][database][dml][insert][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "code INT64 UNIQUE"
        ");"
        "INSERT INTO users VALUES (1, 20);"
    );

    REQUIRE(setup_results.size() == 2);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto duplicate_insert_results = database.execute("INSERT INTO users VALUES (2, 20);");
    REQUIRE(duplicate_insert_results.size() == 1);
    REQUIRE(duplicate_insert_results[0].status.code() == StatusCode::ConstraintViolation);
    REQUIRE(
        duplicate_insert_results[0].status.message() ==
        "Cannot insert into table 'users': duplicate value for unique column 'code'"
    );
    REQUIRE_FALSE(duplicate_insert_results[0].rows_affected.has_value());

    const auto missing_row_results = database.execute("SELECT id FROM users WHERE id = 2;");
    REQUIRE(missing_row_results.size() == 1);
    REQUIRE(missing_row_results[0].status.ok());
    REQUIRE(missing_row_results[0].row_set.has_value());
    REQUIRE(missing_row_results[0].row_set->rows.empty());

    REQUIRE(database.close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());

    const auto* table_descriptor = catalog_result.value().find_table("users");
    REQUIRE(table_descriptor != nullptr);

    const dandb::catalog::IndexDescriptor* index_descriptor = nullptr;
    for(const auto& descriptor: catalog_result.value().indexes_for_table(table_descriptor->table_id())) {
        if(!descriptor.primary() && descriptor.internal() && descriptor.unique()) {
            index_descriptor = &descriptor;
        }
    }

    REQUIRE(index_descriptor != nullptr);

    auto index_tree_result = BTree::open_existing(
        pager_result.value(),
        index_descriptor->root_page_id(),
        static_cast<std::uint16_t>(8),
        static_cast<std::uint16_t>(8)
    );
    REQUIRE(index_tree_result.ok());

    const auto code_key = encode_int64_key(20);
    const auto id_one_key = encode_int64_key(1);
    const auto code_result = index_tree_result.value().find(code_key);
    REQUIRE(code_result.ok());
    REQUIRE(code_result.value() == id_one_key);
    REQUIRE(pager_result.value().close().ok());
}

TEST_CASE("Database maintains user-created unique indexes on INSERT", "[execution][database][dml][insert][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "code INT64 NOT NULL"
        ");"
        "CREATE UNIQUE INDEX users_by_code ON users(code);"
        "INSERT INTO users VALUES (1, 20);"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto duplicate_insert_results = database.execute("INSERT INTO users VALUES (2, 20);");
    REQUIRE(duplicate_insert_results.size() == 1);
    REQUIRE(duplicate_insert_results[0].status.code() == StatusCode::ConstraintViolation);
    REQUIRE_FALSE(duplicate_insert_results[0].rows_affected.has_value());

    const auto missing_row_results = database.execute("SELECT id FROM users WHERE id = 2;");
    REQUIRE(missing_row_results.size() == 1);
    REQUIRE(missing_row_results[0].status.ok());
    REQUIRE(missing_row_results[0].row_set.has_value());
    REQUIRE(missing_row_results[0].row_set->rows.empty());

    REQUIRE(database.close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());

    const auto* index_descriptor = catalog_result.value().find_index("users_by_code");
    REQUIRE(index_descriptor != nullptr);
    REQUIRE_FALSE(index_descriptor->internal());
    REQUIRE(index_descriptor->unique());

    auto index_tree_result = BTree::open_existing(
        pager_result.value(),
        index_descriptor->root_page_id(),
        static_cast<std::uint16_t>(8),
        static_cast<std::uint16_t>(8)
    );
    REQUIRE(index_tree_result.ok());

    const auto code_key = encode_int64_key(20);
    const auto id_one_key = encode_int64_key(1);
    const auto code_result = index_tree_result.value().find(code_key);
    REQUIRE(code_result.ok());
    REQUIRE(code_result.value() == id_one_key);
    REQUIRE(pager_result.value().close().ok());
}

TEST_CASE("Database maintains every secondary index with multiple user indexes", "[execution][database][dml][index][d12-t09]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "account_code INT64 UNIQUE, "
        "tax_code INT64 UNIQUE, "
        "age INT64 NOT NULL, "
        "score INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "CREATE UNIQUE INDEX users_by_score ON users(score);"
        "INSERT INTO users VALUES (1, 101, 1001, 20, 30);"
        "INSERT INTO users VALUES (2, 102, 1002, 20, 31);"
    );

    REQUIRE(setup_results.size() == 5);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_age_results = database.execute("UPDATE users SET age = 21 WHERE id = 1;");
    REQUIRE(update_age_results.size() == 1);
    INFO(update_age_results[0].status.message());
    REQUIRE(update_age_results[0].status.ok());
    REQUIRE(update_age_results[0].rows_affected == 1);

    const auto update_score_results = database.execute("UPDATE users SET score = 32 WHERE id = 1;");
    REQUIRE(update_score_results.size() == 1);
    INFO(update_score_results[0].status.message());
    REQUIRE(update_score_results[0].status.ok());
    REQUIRE(update_score_results[0].rows_affected == 1);

    REQUIRE(database.close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());
    const auto& catalog = catalog_result.value();

    const auto* table_descriptor = catalog.find_table("users");
    REQUIRE(table_descriptor != nullptr);

    const auto* account_code_column = catalog.find_column(table_descriptor->table_id(), "account_code");
    REQUIRE(account_code_column != nullptr);

    const auto* tax_code_column = catalog.find_column(table_descriptor->table_id(), "tax_code");
    REQUIRE(tax_code_column != nullptr);

    const dandb::catalog::IndexDescriptor* account_code_index = nullptr;
    const dandb::catalog::IndexDescriptor* tax_code_index = nullptr;
    for(const auto& index: catalog.indexes_for_table(table_descriptor->table_id())) {
        if(index.internal() && index.indexed_column_id() == account_code_column->column_id()) {
            account_code_index = &index;
        }
        if(index.internal() && index.indexed_column_id() == tax_code_column->column_id()) {
            tax_code_index = &index;
        }
    }

    REQUIRE(account_code_index != nullptr);
    REQUIRE(tax_code_index != nullptr);

    const auto* age_index = catalog.find_index("users_by_age");
    REQUIRE(age_index != nullptr);

    const auto* score_index = catalog.find_index("users_by_score");
    REQUIRE(score_index != nullptr);

    require_int64_index_entry(pager_result.value(), *account_code_index, 101, 1);
    require_int64_index_entry(pager_result.value(), *account_code_index, 102, 2);
    require_int64_index_entry(pager_result.value(), *tax_code_index, 1001, 1);
    require_int64_index_entry(pager_result.value(), *tax_code_index, 1002, 2);

    require_missing_int64_index_entry(pager_result.value(), *age_index, 20, 1);
    require_int64_index_entry(pager_result.value(), *age_index, 21, 1);
    require_int64_index_entry(pager_result.value(), *age_index, 20, 2);

    require_missing_int64_index_entry(pager_result.value(), *score_index, 30, 1);
    require_int64_index_entry(pager_result.value(), *score_index, 32, 1);
    require_int64_index_entry(pager_result.value(), *score_index, 31, 2);

    REQUIRE(pager_result.value().close().ok());

}

TEST_CASE("Database removes entries from every secondary index with multiple user indexes", "[execution][database][dml][delete][index][d12-t09]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "account_code INT64 UNIQUE, "
        "tax_code INT64 UNIQUE, "
        "age INT64 NOT NULL, "
        "score INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "CREATE UNIQUE INDEX users_by_score ON users(score);"
        "INSERT INTO users VALUES (1, 101, 1001, 20, 30);"
        "INSERT INTO users VALUES (2, 102, 1002, 20, 31);"
    );

    REQUIRE(setup_results.size() == 5);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users WHERE id = 1;");
    REQUIRE(delete_results.size() == 1);
    INFO(delete_results[0].status.message());
    REQUIRE(delete_results[0].status.ok());
    REQUIRE(delete_results[0].rows_affected == 1);

    const auto select_results = database.execute("SELECT id FROM users;");
    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 1);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 2);

    REQUIRE(database.close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());
    const auto& catalog = catalog_result.value();

    const auto* table_descriptor = catalog.find_table("users");
    REQUIRE(table_descriptor != nullptr);

    const auto* account_code_column = catalog.find_column(table_descriptor->table_id(), "account_code");
    REQUIRE(account_code_column != nullptr);

    const auto* tax_code_column = catalog.find_column(table_descriptor->table_id(), "tax_code");
    REQUIRE(tax_code_column != nullptr);

    const dandb::catalog::IndexDescriptor* account_code_index = nullptr;
    const dandb::catalog::IndexDescriptor* tax_code_index = nullptr;
    for(const auto& index: catalog.indexes_for_table(table_descriptor->table_id())) {
        if(index.internal() && index.indexed_column_id() == account_code_column->column_id()) {
            account_code_index = &index;
        }
        if(index.internal() && index.indexed_column_id() == tax_code_column->column_id()) {
            tax_code_index = &index;
        }
    }

    REQUIRE(account_code_index != nullptr);
    REQUIRE(tax_code_index != nullptr);

    const auto* age_index = catalog.find_index("users_by_age");
    REQUIRE(age_index != nullptr);

    const auto* score_index = catalog.find_index("users_by_score");
    REQUIRE(score_index != nullptr);

    require_missing_int64_index_entry(pager_result.value(), *account_code_index, 101, 1);
    require_missing_int64_index_entry(pager_result.value(), *tax_code_index, 1001, 1);
    require_missing_int64_index_entry(pager_result.value(), *age_index, 20, 1);
    require_missing_int64_index_entry(pager_result.value(), *score_index, 30, 1);

    require_int64_index_entry(pager_result.value(), *account_code_index, 102, 2);
    require_int64_index_entry(pager_result.value(), *tax_code_index, 1002, 2);
    require_int64_index_entry(pager_result.value(), *age_index, 20, 2);
    require_int64_index_entry(pager_result.value(), *score_index, 31, 2);

    REQUIRE(pager_result.value().close().ok());

}

TEST_CASE("Database leaves indexes unchanged after a duplicate-primary-key INSERT", "[execution][database][dml][insert][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20);"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto duplicate_insert_results = database.execute("INSERT INTO users VALUES (1, 30);");
    REQUIRE(duplicate_insert_results.size() == 1);
    REQUIRE(duplicate_insert_results[0].status.code() == StatusCode::ConstraintViolation);
    REQUIRE_FALSE(duplicate_insert_results[0].rows_affected.has_value());
    REQUIRE(database.close().ok());

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

    const auto id_one_key = encode_int64_key(1);
    auto age_thirty_id_one_key = encode_int64_key(30);
    age_thirty_id_one_key.insert(age_thirty_id_one_key.end(), id_one_key.begin(), id_one_key.end());

    const auto age_thirty_id_one_result = index_tree_result.value().find(age_thirty_id_one_key);
    REQUIRE_FALSE(age_thirty_id_one_result.ok());
    REQUIRE(age_thirty_id_one_result.status().code() == StatusCode::NotFound);
    REQUIRE(pager_result.value().close().ok());
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

TEST_CASE("Database evaluates predicates for every logical type", "[execution][database][dml][predicate]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto setup_results = database.execute(
        "CREATE TABLE predicate_values ("
        "id INT64 PRIMARY KEY, "
        "int8_value INT8 NOT NULL, "
        "int16_value INT16 NOT NULL, "
        "int32_value INT32 NOT NULL, "
        "int64_value INT64 NOT NULL, "
        "double_value DOUBLE NOT NULL, "
        "string_value STRING(16) NOT NULL, "
        "bool_value BOOL NOT NULL"
        ");"
        "INSERT INTO predicate_values VALUES (1, 1, 10, 100, 1000, 1.5, 'Ada', FALSE);"
        "INSERT INTO predicate_values VALUES (2, 2, 20, 200, 2000, 2.5, 'Grace', TRUE);"
        "INSERT INTO predicate_values VALUES (3, 3, 30, 300, 3000, 3.5, 'Linus', TRUE);"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto require_selected_ids = [&database](
        std::string_view statement_text,
        const std::vector<std::int64_t>& expected_ids
    ) {
        const auto results = database.execute(statement_text);

        INFO(statement_text);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].row_set.has_value());

        const auto& row_set = *results[0].row_set;
        REQUIRE(row_set.rows.size() == expected_ids.size());
        for(std::size_t index = 0; index < expected_ids.size(); ++index) {
            REQUIRE(row_set.rows[index].value(0).as_integer() == expected_ids[index]);
        }
    };

    require_selected_ids("SELECT id FROM predicate_values WHERE int8_value < 2;", { 1 });
    require_selected_ids("SELECT id FROM predicate_values WHERE int16_value <= 20;", { 1, 2 });
    require_selected_ids("SELECT id FROM predicate_values WHERE int32_value > 100;", { 2, 3 });
    require_selected_ids("SELECT id FROM predicate_values WHERE int64_value >= 3000;", { 3 });
    require_selected_ids("SELECT id FROM predicate_values WHERE double_value = 2.5;", { 2 });
    require_selected_ids("SELECT id FROM predicate_values WHERE string_value != 'Grace';", { 1, 3 });
    require_selected_ids("SELECT id FROM predicate_values WHERE bool_value = TRUE;", { 2, 3 });

    const auto incompatible_type_results = database.execute(
        "SELECT id FROM predicate_values WHERE int8_value = '1';"
    );

    REQUIRE(incompatible_type_results.size() == 1);
    REQUIRE(incompatible_type_results[0].status.code() == StatusCode::InvalidArgument);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database applies NULL predicate rules across statements", "[execution][database][dml][predicate]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "value STRING(16), "
        "label STRING(16) NOT NULL"
        ");"
        "INSERT INTO users VALUES (1, NULL, 'Missing');"
        "INSERT INTO users VALUES (2, 'Ada', 'Ada');"
        "INSERT INTO users VALUES (3, 'Linus', 'Linus');"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const std::vector<std::string_view> comparison_operators{
        "=", "!=", "<", "<=", ">", ">="
    };

    for(const auto comparison_operator: comparison_operators) {
        const auto statement_text = std::string("SELECT id FROM users WHERE value ")
            +std::string(comparison_operator)+" NULL;";
        const auto results = database.execute(statement_text);

        INFO(statement_text);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].row_set.has_value());
        REQUIRE(results[0].row_set->rows.empty());
    }

    const auto update_null_results = database.execute(
        "UPDATE users SET label = 'Updated' WHERE value IS NULL;"
    );

    REQUIRE(update_null_results.size() == 1);
    REQUIRE(update_null_results[0].status.ok());
    REQUIRE(update_null_results[0].rows_affected == 1);

    const auto update_comparison_results = database.execute(
        "UPDATE users SET label = 'Invalid' WHERE value != NULL;"
    );

    REQUIRE(update_comparison_results.size() == 1);
    REQUIRE(update_comparison_results[0].status.ok());
    REQUIRE(update_comparison_results[0].rows_affected == 0);

    const auto delete_comparison_results = database.execute(
        "DELETE FROM users WHERE value < NULL;"
    );

    REQUIRE(delete_comparison_results.size() == 1);
    REQUIRE(delete_comparison_results[0].status.ok());
    REQUIRE(delete_comparison_results[0].rows_affected == 0);

    const auto delete_not_null_results = database.execute(
        "DELETE FROM users WHERE value IS NOT NULL;"
    );

    REQUIRE(delete_not_null_results.size() == 1);
    REQUIRE(delete_not_null_results[0].status.ok());
    REQUIRE(delete_not_null_results[0].rows_affected == 2);

    const auto remaining_rows_results = database.execute("SELECT id, label FROM users;");

    REQUIRE(remaining_rows_results.size() == 1);
    REQUIRE(remaining_rows_results[0].status.ok());
    REQUIRE(remaining_rows_results[0].row_set.has_value());
    REQUIRE(remaining_rows_results[0].row_set->rows.size() == 1);
    REQUIRE(remaining_rows_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(remaining_rows_results[0].row_set->rows[0].value(1).as_string() == "Updated");
    REQUIRE(database.close().ok());
}

TEST_CASE("Database returns the same predicate matches with and without an index", "[execution][database][dml][predicate][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());
    auto& database = database_result.value();

    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "indexed_value INT64 NOT NULL, "
        "table_scan_value INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_indexed_value ON users(indexed_value);"
        "INSERT INTO users VALUES (1, 20, 20);"
        "INSERT INTO users VALUES (2, 30, 30);"
        "INSERT INTO users VALUES (3, 20, 20);"
        "INSERT INTO users VALUES (4, 10, 10);"
    );

    REQUIRE(setup_results.size() == 6);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto selected_ids = [&database](std::string_view statement_text) {
        const auto results = database.execute(statement_text);

        INFO(statement_text);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].row_set.has_value());

        std::vector<std::int64_t> ids;
        ids.reserve(results[0].row_set->rows.size());
        for(const auto& row: results[0].row_set->rows) {
            ids.push_back(row.value(0).as_integer());
        }

        std::sort(ids.begin(), ids.end());
        return ids;
    };

    const auto require_equivalent_matches = [&selected_ids](
        std::string_view comparison_operator,
        const std::vector<std::int64_t>& expected_ids
    ) {
        const auto indexed_statement = std::string("SELECT id FROM users WHERE indexed_value ")
            +std::string(comparison_operator)+" 20;";
        const auto table_scan_statement = std::string("SELECT id FROM users WHERE table_scan_value ")
            +std::string(comparison_operator)+" 20;";

        const auto indexed_ids = selected_ids(indexed_statement);
        const auto table_scan_ids = selected_ids(table_scan_statement);

        INFO(comparison_operator);
        REQUIRE(indexed_ids == expected_ids);
        REQUIRE(table_scan_ids == expected_ids);
    };

    require_equivalent_matches("=", { 1, 3 });
    require_equivalent_matches("!=", { 2, 4 });
    require_equivalent_matches("<", { 4 });
    require_equivalent_matches("<=", { 1, 3, 4 });
    require_equivalent_matches(">", { 2 });
    require_equivalent_matches(">=", { 1, 2, 3 });
    REQUIRE(database.close().ok());
}

TEST_CASE("Database selects through a non-unique secondary index", "[execution][database][dml][select][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 10);"
        "INSERT INTO users VALUES (2, 20);"
        "INSERT INTO users VALUES (3, 20);"
        "INSERT INTO users VALUES (4, 30);"
    );

    REQUIRE(setup_results.size() == 6);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto require_selected_ids = [&database](
        std::string_view statement_text,
        const std::vector<std::int64_t>& expected_ids
    ) {
        const auto results = database.execute(statement_text);

        INFO(statement_text);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status.ok());
        REQUIRE(results[0].row_set.has_value());

        const auto& row_set = *results[0].row_set;
        REQUIRE(row_set.rows.size() == expected_ids.size());
        for(std::size_t index = 0; index < expected_ids.size(); ++index) {
            REQUIRE(row_set.rows[index].value(0).as_integer() == expected_ids[index]);
        }
    };

    require_selected_ids("SELECT id FROM users WHERE age = 20;", { 2, 3 });
    require_selected_ids("SELECT id FROM users WHERE age != 20;", { 1, 4 });
    require_selected_ids("SELECT id FROM users WHERE age < 20;", { 1 });
    require_selected_ids("SELECT id FROM users WHERE age <= 20;", { 1, 2, 3 });
    require_selected_ids("SELECT id FROM users WHERE age > 20;", { 4 });
    require_selected_ids("SELECT id FROM users WHERE age >= 20;", { 2, 3, 4 });

    REQUIRE(database.close().ok());
}

TEST_CASE("Database selects through an internal unique index", "[execution][database][dml][select][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "email STRING(64) UNIQUE, "
        "name STRING(64) NOT NULL"
        ");"
        "INSERT INTO users VALUES (1, 'ada@example.com', 'Ada');"
        "INSERT INTO users VALUES (2, 'grace@example.com', 'Grace');"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto results = database.execute(
        "SELECT id, name FROM users WHERE email = 'grace@example.com';"
    );

    REQUIRE(results.size() == 1);
    REQUIRE(results[0].status.ok());
    REQUIRE(results[0].row_set.has_value());

    const auto& row_set = *results[0].row_set;
    REQUIRE(row_set.rows.size() == 1);
    REQUIRE(row_set.rows[0].value(0).as_integer() == 2);
    REQUIRE(row_set.rows[0].value(1).as_string() == "Grace");
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

TEST_CASE("Database maintains non-unique indexes on UPDATE", "[execution][database][dml][update][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20);"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_results = database.execute("UPDATE users SET age = 30 WHERE id = 1;");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.ok());
    REQUIRE(update_results[0].rows_affected == 1);
    REQUIRE(database.close().ok());

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

    const auto id_key = encode_int64_key(1);
    auto old_index_key = encode_int64_key(20);
    old_index_key.insert(old_index_key.end(), id_key.begin(), id_key.end());

    const auto old_index_result = index_tree_result.value().find(old_index_key);
    REQUIRE_FALSE(old_index_result.ok());
    REQUIRE(old_index_result.status().code() == StatusCode::NotFound);

    auto new_index_key = encode_int64_key(30);
    new_index_key.insert(new_index_key.end(), id_key.begin(), id_key.end());

    const auto new_index_result = index_tree_result.value().find(new_index_key);
    REQUIRE(new_index_result.ok());
    REQUIRE(new_index_result.value() == id_key);
    REQUIRE(pager_result.value().close().ok());
}

TEST_CASE("Database updates matching rows through a non-unique secondary index", "[execution][database][dml][update][index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL, "
        "name STRING(64)"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20, 'Ada');"
        "INSERT INTO users VALUES (2, 20, 'Grace');"
        "INSERT INTO users VALUES (3, 30, 'Linus');"
    );

    REQUIRE(setup_results.size() == 5);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_results = database.execute("UPDATE users SET name = 'Updated' WHERE age = 20;");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.ok());
    REQUIRE(update_results[0].rows_affected == 2);

    const auto select_results = database.execute("SELECT id, name FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 3);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(select_results[0].row_set->rows[0].value(1).as_string() == "Updated");
    REQUIRE(select_results[0].row_set->rows[1].value(0).as_integer() == 2);
    REQUIRE(select_results[0].row_set->rows[1].value(1).as_string() == "Updated");
    REQUIRE(select_results[0].row_set->rows[2].value(0).as_integer() == 3);
    REQUIRE(select_results[0].row_set->rows[2].value(1).as_string() == "Linus");
    REQUIRE(database.close().ok());
}

TEST_CASE("Database updates one row through a unique secondary index", "[execution][database][dml][update][index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "code INT64 UNIQUE, "
        "name STRING(64)"
        ");"
        "INSERT INTO users VALUES (1, 10, 'Ada');"
        "INSERT INTO users VALUES (2, 20, 'Grace');"
        "INSERT INTO users VALUES (3, 30, 'Linus');"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_results = database.execute("UPDATE users SET name = 'Updated' WHERE code = 20;");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.ok());
    REQUIRE(update_results[0].rows_affected == 1);

    const auto select_results = database.execute("SELECT id, name FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 3);
    REQUIRE(select_results[0].row_set->rows[0].value(1).as_string() == "Ada");
    REQUIRE(select_results[0].row_set->rows[1].value(0).as_integer() == 2);
    REQUIRE(select_results[0].row_set->rows[1].value(1).as_string() == "Updated");
    REQUIRE(select_results[0].row_set->rows[2].value(1).as_string() == "Linus");
    REQUIRE(database.close().ok());
}

TEST_CASE("Database updates an indexed predicate column without skipping rows", "[execution][database][dml][update][index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20);"
        "INSERT INTO users VALUES (2, 20);"
        "INSERT INTO users VALUES (3, 30);"
    );

    REQUIRE(setup_results.size() == 5);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_results = database.execute("UPDATE users SET age = 30 WHERE age = 20;");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.ok());
    REQUIRE(update_results[0].rows_affected == 2);

    const auto old_age_results = database.execute("SELECT id FROM users WHERE age = 20;");

    REQUIRE(old_age_results.size() == 1);
    REQUIRE(old_age_results[0].status.ok());
    REQUIRE(old_age_results[0].row_set.has_value());
    REQUIRE(old_age_results[0].row_set->rows.empty());

    const auto new_age_results = database.execute("SELECT id FROM users WHERE age = 30;");

    REQUIRE(new_age_results.size() == 1);
    REQUIRE(new_age_results[0].status.ok());
    REQUIRE(new_age_results[0].row_set.has_value());
    REQUIRE(new_age_results[0].row_set->rows.size() == 3);
    REQUIRE(new_age_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(new_age_results[0].row_set->rows[1].value(0).as_integer() == 2);
    REQUIRE(new_age_results[0].row_set->rows[2].value(0).as_integer() == 3);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database falls back to a table scan for an unindexed UPDATE predicate", "[execution][database][dml][update][index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL, "
        "name STRING(64)"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20, 'Ada');"
        "INSERT INTO users VALUES (2, 20, 'Grace');"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_results = database.execute("UPDATE users SET age = 30 WHERE name = 'Grace';");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.ok());
    REQUIRE(update_results[0].rows_affected == 1);

    const auto select_results = database.execute("SELECT id, age FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 2);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(select_results[0].row_set->rows[0].value(1).as_integer() == 20);
    REQUIRE(select_results[0].row_set->rows[1].value(0).as_integer() == 2);
    REQUIRE(select_results[0].row_set->rows[1].value(1).as_integer() == 30);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database restores rows and indexes after a secondary-index UPDATE fails", "[execution][database][dml][update][index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL, "
        "code INT64 UNIQUE"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20, 10);"
        "INSERT INTO users VALUES (2, 20, 20);"
        "INSERT INTO users VALUES (3, 30, 30);"
    );

    REQUIRE(setup_results.size() == 5);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_results = database.execute("UPDATE users SET code = 30 WHERE age = 20;");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.code() == StatusCode::ConstraintViolation);
    REQUIRE(
        update_results[0].status.message() ==
        "Cannot update table 'users': duplicate value for unique column 'code'"
    );
    REQUIRE_FALSE(update_results[0].rows_affected.has_value());

    const auto table_results = database.execute("SELECT id, age, code FROM users;");

    REQUIRE(table_results.size() == 1);
    REQUIRE(table_results[0].status.ok());
    REQUIRE(table_results[0].row_set.has_value());
    REQUIRE(table_results[0].row_set->rows.size() == 3);
    REQUIRE(table_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(table_results[0].row_set->rows[0].value(1).as_integer() == 20);
    REQUIRE(table_results[0].row_set->rows[0].value(2).as_integer() == 10);
    REQUIRE(table_results[0].row_set->rows[1].value(0).as_integer() == 2);
    REQUIRE(table_results[0].row_set->rows[1].value(1).as_integer() == 20);
    REQUIRE(table_results[0].row_set->rows[1].value(2).as_integer() == 20);
    REQUIRE(table_results[0].row_set->rows[2].value(0).as_integer() == 3);
    REQUIRE(table_results[0].row_set->rows[2].value(1).as_integer() == 30);
    REQUIRE(table_results[0].row_set->rows[2].value(2).as_integer() == 30);

    const auto age_lookup_results = database.execute("SELECT id FROM users WHERE age = 20;");

    REQUIRE(age_lookup_results.size() == 1);
    REQUIRE(age_lookup_results[0].status.ok());
    REQUIRE(age_lookup_results[0].row_set.has_value());
    REQUIRE(age_lookup_results[0].row_set->rows.size() == 2);
    REQUIRE(age_lookup_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(age_lookup_results[0].row_set->rows[1].value(0).as_integer() == 2);

    const auto code_lookup_results = database.execute("SELECT id FROM users WHERE code = 10;");

    REQUIRE(code_lookup_results.size() == 1);
    REQUIRE(code_lookup_results[0].status.ok());
    REQUIRE(code_lookup_results[0].row_set.has_value());
    REQUIRE(code_lookup_results[0].row_set->rows.size() == 1);
    REQUIRE(code_lookup_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database rolls back a multi-row UPDATE that duplicates a unique index", "[execution][database][dml][update][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "code INT64 UNIQUE"
        ");"
        "INSERT INTO users VALUES (1, 10);"
        "INSERT INTO users VALUES (2, 20);"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_results = database.execute("UPDATE users SET code = 30;");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.code() == StatusCode::ConstraintViolation);
    REQUIRE_FALSE(update_results[0].rows_affected.has_value());

    const auto select_results = database.execute("SELECT code FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 2);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 10);
    REQUIRE(select_results[0].row_set->rows[1].value(0).as_integer() == 20);
    REQUIRE(database.close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());

    const auto* table_descriptor = catalog_result.value().find_table("users");
    REQUIRE(table_descriptor != nullptr);

    const dandb::catalog::IndexDescriptor* index_descriptor = nullptr;
    for(const auto& descriptor: catalog_result.value().indexes_for_table(table_descriptor->table_id())) {
        if(!descriptor.primary() && descriptor.internal() && descriptor.unique()) {
            index_descriptor = &descriptor;
        }
    }

    REQUIRE(index_descriptor != nullptr);

    auto index_tree_result = BTree::open_existing(
        pager_result.value(),
        index_descriptor->root_page_id(),
        static_cast<std::uint16_t>(8),
        static_cast<std::uint16_t>(8)
    );
    REQUIRE(index_tree_result.ok());

    const auto code_ten_result = index_tree_result.value().find(encode_int64_key(10));
    REQUIRE(code_ten_result.ok());
    REQUIRE(code_ten_result.value() == encode_int64_key(1));

    const auto code_twenty_result = index_tree_result.value().find(encode_int64_key(20));
    REQUIRE(code_twenty_result.ok());
    REQUIRE(code_twenty_result.value() == encode_int64_key(2));

    const auto code_thirty_result = index_tree_result.value().find(encode_int64_key(30));
    REQUIRE_FALSE(code_thirty_result.ok());
    REQUIRE(code_thirty_result.status().code() == StatusCode::NotFound);
    REQUIRE(pager_result.value().close().ok());
}

TEST_CASE("Database leaves indexes unchanged when UPDATE changes an unindexed column", "[execution][database][dml][update][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL, "
        "name STRING(64)"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20, 'Ada');"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto update_results = database.execute("UPDATE users SET name = 'Grace' WHERE id = 1;");

    REQUIRE(update_results.size() == 1);
    REQUIRE(update_results[0].status.ok());
    REQUIRE(update_results[0].rows_affected == 1);
    REQUIRE(database.close().ok());

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

    const auto id_key = encode_int64_key(1);
    auto age_index_key = encode_int64_key(20);
    age_index_key.insert(age_index_key.end(), id_key.begin(), id_key.end());

    const auto age_index_result = index_tree_result.value().find(age_index_key);
    REQUIRE(age_index_result.ok());
    REQUIRE(age_index_result.value() == id_key);
    REQUIRE(pager_result.value().close().ok());
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

TEST_CASE("Database deletes matching rows through a non-unique secondary index", "[execution][database][dml][delete][index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL, "
        "name STRING(64)"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20, 'Ada');"
        "INSERT INTO users VALUES (2, 20, 'Grace');"
        "INSERT INTO users VALUES (3, 30, 'Linus');"
    );

    REQUIRE(setup_results.size() == 5);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users WHERE age = 20;");

    REQUIRE(delete_results.size() == 1);
    REQUIRE(delete_results[0].status.ok());
    REQUIRE(delete_results[0].rows_affected == 2);

    const auto select_results = database.execute("SELECT id FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 1);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 3);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database deletes matching rows through multiple secondary-index cursors", "[execution][database][dml][delete][index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 10);"
        "INSERT INTO users VALUES (2, 20);"
        "INSERT INTO users VALUES (3, 20);"
        "INSERT INTO users VALUES (4, 30);"
    );

    REQUIRE(setup_results.size() == 6);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users WHERE age != 20;");

    REQUIRE(delete_results.size() == 1);
    REQUIRE(delete_results[0].status.ok());
    REQUIRE(delete_results[0].rows_affected == 2);

    const auto select_results = database.execute("SELECT id FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 2);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 2);
    REQUIRE(select_results[0].row_set->rows[1].value(0).as_integer() == 3);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database deletes one row through a unique secondary index", "[execution][database][dml][delete][index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "code INT64 UNIQUE, "
        "name STRING(64)"
        ");"
        "INSERT INTO users VALUES (1, 10, 'Ada');"
        "INSERT INTO users VALUES (2, 20, 'Grace');"
        "INSERT INTO users VALUES (3, 30, 'Linus');"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users WHERE code = 20;");

    REQUIRE(delete_results.size() == 1);
    REQUIRE(delete_results[0].status.ok());
    REQUIRE(delete_results[0].rows_affected == 1);

    const auto select_results = database.execute("SELECT id FROM users;");

    REQUIRE(select_results.size() == 1);
    REQUIRE(select_results[0].status.ok());
    REQUIRE(select_results[0].row_set.has_value());
    REQUIRE(select_results[0].row_set->rows.size() == 2);
    REQUIRE(select_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(select_results[0].row_set->rows[1].value(0).as_integer() == 3);
    REQUIRE(database.close().ok());
}

TEST_CASE("Database falls back to a table scan for an unindexed DELETE predicate", "[execution][database][dml][delete][index]") {

    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL, "
        "name STRING(64)"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20, 'Ada');"
        "INSERT INTO users VALUES (2, 20, 'Grace');"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users WHERE name = 'Grace';");

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

TEST_CASE("Database removes a non-unique index entry on DELETE", "[execution][database][dml][delete][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20);"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users WHERE id = 1;");

    REQUIRE(delete_results.size() == 1);
    REQUIRE(delete_results[0].status.ok());
    REQUIRE(delete_results[0].rows_affected == 1);
    REQUIRE(database.close().ok());

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

    const auto id_key = encode_int64_key(1);
    auto age_index_key = encode_int64_key(20);
    age_index_key.insert(age_index_key.end(), id_key.begin(), id_key.end());

    const auto index_entry_result = index_tree_result.value().find(age_index_key);
    REQUIRE_FALSE(index_entry_result.ok());
    REQUIRE(index_entry_result.status().code() == StatusCode::NotFound);
    REQUIRE(pager_result.value().close().ok());
}

TEST_CASE("Database removes an internal unique index entry on DELETE", "[execution][database][dml][delete][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "email STRING(64) UNIQUE"
        ");"
        "INSERT INTO users VALUES (1, 'ada@example.com');"
    );

    REQUIRE(setup_results.size() == 2);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users WHERE id = 1;");

    REQUIRE(delete_results.size() == 1);
    REQUIRE(delete_results[0].status.ok());
    REQUIRE(delete_results[0].rows_affected == 1);
    REQUIRE(database.close().ok());

    auto pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());

    auto catalog_result = Catalog::load(pager_result.value());
    REQUIRE(catalog_result.ok());

    const auto* table_descriptor = catalog_result.value().find_table("users");
    REQUIRE(table_descriptor != nullptr);

    const dandb::catalog::IndexDescriptor* index_descriptor = nullptr;
    for(const auto& descriptor: catalog_result.value().indexes_for_table(table_descriptor->table_id())) {
        if(!descriptor.primary() && descriptor.internal() && descriptor.unique()) {
            index_descriptor = &descriptor;
        }
    }

    REQUIRE(index_descriptor != nullptr);

    auto index_tree_result = BTree::open_existing(
        pager_result.value(),
        index_descriptor->root_page_id(),
        static_cast<std::uint16_t>(64),
        static_cast<std::uint16_t>(8)
    );
    REQUIRE(index_tree_result.ok());

    auto email_value_result = Value::string("ada@example.com", 64);
    REQUIRE(email_value_result.ok());

    auto email_key_result = KeyCodec::encode(email_value_result.value());
    REQUIRE(email_key_result.ok());

    const auto index_entry_result = index_tree_result.value().find(email_key_result.value());
    REQUIRE_FALSE(index_entry_result.ok());
    REQUIRE(index_entry_result.status().code() == StatusCode::NotFound);
    REQUIRE(pager_result.value().close().ok());
}

TEST_CASE("Database clears a non-unique index when DELETE has no predicate", "[execution][database][dml][delete][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20);"
        "INSERT INTO users VALUES (2, 30);"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto delete_results = database.execute("DELETE FROM users;");

    REQUIRE(delete_results.size() == 1);
    REQUIRE(delete_results[0].status.ok());
    REQUIRE(delete_results[0].rows_affected == 2);
    REQUIRE(database.close().ok());

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

    auto cursor_result = index_tree_result.value().scan();
    REQUIRE(cursor_result.ok());

    const auto entry_result = cursor_result.value().next();
    REQUIRE(entry_result.ok());
    REQUIRE_FALSE(entry_result.value().has_value());
    REQUIRE(pager_result.value().close().ok());
}

TEST_CASE("Database restores a non-unique index entry after DELETE rollback", "[execution][database][dml][delete][index]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 20);"
    );

    REQUIRE(setup_results.size() == 3);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto transaction_results = database.execute(
        "BEGIN; DELETE FROM users WHERE id = 1; ROLLBACK;"
    );

    REQUIRE(transaction_results.size() == 3);
    REQUIRE(transaction_results[0].status.ok());
    REQUIRE(transaction_results[1].status.ok());
    REQUIRE(transaction_results[1].rows_affected == 1);
    REQUIRE(transaction_results[2].status.ok());
    REQUIRE(database.close().ok());

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

    const auto id_key = encode_int64_key(1);
    auto age_index_key = encode_int64_key(20);
    age_index_key.insert(age_index_key.end(), id_key.begin(), id_key.end());

    const auto index_entry_result = index_tree_result.value().find(age_index_key);
    REQUIRE(index_entry_result.ok());
    REQUIRE(index_entry_result.value() == id_key);
    REQUIRE(pager_result.value().close().ok());
}

TEST_CASE("Database preserves indexed rows through rollback, recovery, and checkpoint", "[execution][database][d12-t11]") {
    const TempDir temp_dir;

    auto database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(database_result.ok());

    auto& database = database_result.value();
    const auto setup_results = database.execute(
        "CREATE TABLE users ("
        "id INT64 PRIMARY KEY, "
        "email STRING(64) UNIQUE, "
        "age INT64 NOT NULL"
        ");"
        "CREATE INDEX users_by_age ON users(age);"
        "INSERT INTO users VALUES (1, 'ada@example.com', 20);"
        "INSERT INTO users VALUES (2, 'grace@example.com', 30);"
    );

    REQUIRE(setup_results.size() == 4);
    for(const auto& result: setup_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto rollback_results = database.execute(
        "BEGIN;"
        "UPDATE users SET age = 25 WHERE age = 20;"
        "DELETE FROM users WHERE email = 'grace@example.com';"
        "ROLLBACK;"
    );

    REQUIRE(rollback_results.size() == 4);
    for(const auto& result: rollback_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    const auto rollback_state_results = database.execute(
        "SELECT id FROM users WHERE age = 20;"
        "SELECT id FROM users WHERE email = 'grace@example.com';"
    );

    REQUIRE(rollback_state_results.size() == 2);
    REQUIRE(rollback_state_results[0].status.ok());
    REQUIRE(rollback_state_results[0].row_set.has_value());
    REQUIRE(rollback_state_results[0].row_set->rows.size() == 1);
    REQUIRE(rollback_state_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(rollback_state_results[1].status.ok());
    REQUIRE(rollback_state_results[1].row_set.has_value());
    REQUIRE(rollback_state_results[1].row_set->rows.size() == 1);
    REQUIRE(rollback_state_results[1].row_set->rows[0].value(0).as_integer() == 2);

    const auto commit_results = database.execute(
        "BEGIN;"
        "UPDATE users SET age = 25 WHERE age = 20;"
        "DELETE FROM users WHERE email = 'grace@example.com';"
        "COMMIT;"
    );

    REQUIRE(commit_results.size() == 4);
    for(const auto& result: commit_results) {
        INFO(result.status.message());
        REQUIRE(result.status.ok());
    }

    REQUIRE(database.close().ok());

    auto recovered_database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(recovered_database_result.ok());

    auto& recovered_database = recovered_database_result.value();
    const auto recovery_results = recovered_database.execute(
        "SELECT id FROM users WHERE age = 25;"
        "SELECT id FROM users WHERE email = 'grace@example.com';"
        "CHECKPOINT;"
    );

    REQUIRE(recovery_results.size() == 3);
    REQUIRE(recovery_results[0].status.ok());
    REQUIRE(recovery_results[0].row_set.has_value());
    REQUIRE(recovery_results[0].row_set->rows.size() == 1);
    REQUIRE(recovery_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(recovery_results[1].status.ok());
    REQUIRE(recovery_results[1].row_set.has_value());
    REQUIRE(recovery_results[1].row_set->rows.empty());
    REQUIRE(recovery_results[2].status.ok());
    REQUIRE(recovered_database.close().ok());

    auto checkpointed_database_result = Database::open_or_create(temp_dir.database_path());
    REQUIRE(checkpointed_database_result.ok());

    auto& checkpointed_database = checkpointed_database_result.value();
    const auto checkpointed_results = checkpointed_database.execute(
        "SELECT id FROM users WHERE email = 'ada@example.com';"
        "INSERT INTO users VALUES (2, 'ada@example.com', 30);"
    );

    REQUIRE(checkpointed_results.size() == 2);
    REQUIRE(checkpointed_results[0].status.ok());
    REQUIRE(checkpointed_results[0].row_set.has_value());
    REQUIRE(checkpointed_results[0].row_set->rows.size() == 1);
    REQUIRE(checkpointed_results[0].row_set->rows[0].value(0).as_integer() == 1);
    REQUIRE(checkpointed_results[1].status.code() == StatusCode::ConstraintViolation);
    REQUIRE(checkpointed_database.close().ok());
}
