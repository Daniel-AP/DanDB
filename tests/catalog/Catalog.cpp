#include <catch_amalgamated.hpp>

#include <dandb/btree/BTree.h>
#include <dandb/catalog/Catalog.h>
#include <dandb/catalog/IndexNames.h>
#include <dandb/catalog/SystemTables.h>
#include <dandb/core/Status.h>
#include <dandb/record/Column.h>
#include <dandb/record/LogicalType.h>
#include <dandb/record/Schema.h>
#include <dandb/storage/Pager.h>
#include <testutil/TempDir.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

using dandb::btree::BTree;
using dandb::catalog::Catalog;
using dandb::core::StatusCode;
using dandb::record::Column;
using dandb::record::LogicalType;
using dandb::record::Schema;
using dandb::storage::Pager;
using dandb::testutil::TempDir;

namespace {

    constexpr std::size_t TEST_BPM_CAPACITY = 10;

    Schema make_schema(std::string primary_key_name = "id") {
        auto column_result = Column::create(
            std::move(primary_key_name),
            LogicalType::int64(),
            false,
            true,
            true
        );
        REQUIRE(column_result.ok());

        auto schema_result = Schema::create({ std::move(column_result.value()) });
        REQUIRE(schema_result.ok());

        return std::move(schema_result.value());
    }

    Schema make_schema_with_unique_column() {
        auto primary_key_result = Column::create(
            "id",
            LogicalType::int64(),
            false,
            true,
            true
        );
        REQUIRE(primary_key_result.ok());

        auto email_type_result = LogicalType::string(120);
        REQUIRE(email_type_result.ok());

        auto unique_column_result = Column::create(
            "email",
            email_type_result.value(),
            false,
            false,
            true
        );
        REQUIRE(unique_column_result.ok());

        auto schema_result = Schema::create({
            std::move(primary_key_result.value()),
            std::move(unique_column_result.value())
        });
        REQUIRE(schema_result.ok());

        return std::move(schema_result.value());
    }

    Schema make_unrepresentable_schema() {
        auto primary_key_type_result = LogicalType::string(65537);
        REQUIRE(primary_key_type_result.ok());

        auto primary_key_result = Column::create(
            "id",
            primary_key_type_result.value(),
            false,
            true,
            true
        );
        REQUIRE(primary_key_result.ok());

        auto schema_result = Schema::create({ std::move(primary_key_result.value()) });
        REQUIRE(schema_result.ok());

        return std::move(schema_result.value());
    }

}

TEST_CASE("Catalog create_table rejects a name already in use", "[catalog][create-table]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());

    const auto status = catalog_result.value().create_table("dandb_tables", make_schema());
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::AlreadyExists);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog create_table rejects an empty name", "[catalog][create-table]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());

    const auto status = catalog_result.value().create_table("", make_schema());
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog create_table rejects a name with the reserved prefix", "[catalog][create-table]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());

    const auto status = catalog_result.value().create_table("dandb_users", make_schema());
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog create_table rejects a name longer than the catalog capacity", "[catalog][create-table]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());

    const auto status = catalog_result.value().create_table(
        std::string(dandb::catalog::CATALOG_NAME_CAPACITY + 1, 'a'),
        make_schema()
    );
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog create_table rejects a column name longer than the catalog capacity", "[catalog][create-table]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());

    const auto status = catalog_result.value().create_table(
        "users",
        make_schema(std::string(dandb::catalog::CATALOG_NAME_CAPACITY + 1, 'a'))
    );
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog creates table metadata that survives reopening", "[catalog][create-table]") {
    const TempDir temp_dir;
    const Schema schema = make_schema();

    {
        auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
        REQUIRE(pager_result.ok());
        Pager& pager = pager_result.value();

        auto catalog_result = Catalog::load(pager);
        REQUIRE(catalog_result.ok());
        Catalog& catalog = catalog_result.value();

        REQUIRE(catalog.create_table("users", schema).ok());

        const auto* table = catalog.find_table("users");
        REQUIRE(table != nullptr);
        REQUIRE(table->root_page_id().is_valid());
        REQUIRE(catalog.schema_for_table(table->table_id())->column_count() == schema.column_count());

        const auto indexes = catalog.indexes_for_table(table->table_id());
        REQUIRE(indexes.size() == 1);
        REQUIRE(indexes[0].primary());
        REQUIRE(indexes[0].internal());
        REQUIRE(indexes[0].unique());
        REQUIRE(indexes[0].root_page_id() == table->root_page_id());
        REQUIRE(indexes[0].name() == dandb::catalog::internal_primary_index_name(table->table_id()));

        auto table_tree_result = BTree::open_existing(
            pager,
            table->root_page_id(),
            static_cast<std::uint16_t>(schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(schema.row_size())
        );
        REQUIRE(table_tree_result.ok());
        REQUIRE(table_tree_result.value().validate().ok());

        REQUIRE(pager.close().ok());
    }

    auto reopened_pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(reopened_pager_result.ok());
    Pager& reopened_pager = reopened_pager_result.value();

    auto reopened_catalog_result = Catalog::load(reopened_pager);
    REQUIRE(reopened_catalog_result.ok());
    const Catalog& reopened_catalog = reopened_catalog_result.value();

    const auto* reopened_table = reopened_catalog.find_table("users");
    REQUIRE(reopened_table != nullptr);
    REQUIRE(reopened_catalog.schema_for_table(reopened_table->table_id())->column_count() == schema.column_count());

    const auto reopened_indexes = reopened_catalog.indexes_for_table(reopened_table->table_id());
    REQUIRE(reopened_indexes.size() == 1);
    REQUIRE(reopened_indexes[0].primary());
    REQUIRE(reopened_indexes[0].root_page_id() == reopened_table->root_page_id());

    REQUIRE(reopened_pager.close().ok());
}

TEST_CASE("Catalog rejects a schema whose B+ tree sizes cannot be represented", "[catalog][create-table]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());
    Catalog& catalog = catalog_result.value();

    const auto status = catalog.create_table("users", make_unrepresentable_schema());
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);
    REQUIRE(catalog.find_table("users") == nullptr);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog creates an internal index for a unique column", "[catalog][create-table]") {
    const TempDir temp_dir;
    const Schema schema = make_schema_with_unique_column();

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());
    Catalog& catalog = catalog_result.value();

    REQUIRE(catalog.create_table("users", schema).ok());

    const auto* table = catalog.find_table("users");
    REQUIRE(table != nullptr);

    const auto* email_column = catalog.find_column(table->table_id(), "email");
    REQUIRE(email_column != nullptr);

    const auto indexes = catalog.indexes_for_table(table->table_id());
    REQUIRE(indexes.size() == 2);

    const dandb::catalog::IndexDescriptor* unique_index = nullptr;
    for(const auto& index: indexes) {
        if(!index.primary()) {
            unique_index = &index;
        }
    }

    REQUIRE(unique_index != nullptr);
    REQUIRE(unique_index->internal());
    REQUIRE(unique_index->unique());
    REQUIRE(unique_index->indexed_column_id() == email_column->column_id());
    REQUIRE(unique_index->root_page_id() != table->root_page_id());
    REQUIRE(unique_index->name() == dandb::catalog::internal_unique_index_name(
        table->table_id(),
        email_column->column_id()
    ));

    auto unique_tree_result = BTree::open_existing(
        pager,
        unique_index->root_page_id(),
        static_cast<std::uint16_t>(email_column->logical_type().fixed_size()),
        static_cast<std::uint16_t>(schema.primary_key_column().logical_type().fixed_size())
    );
    REQUIRE(unique_tree_result.ok());
    REQUIRE(unique_tree_result.value().validate().ok());

    REQUIRE(pager.close().ok());

    auto reopened_pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(reopened_pager_result.ok());
    Pager& reopened_pager = reopened_pager_result.value();

    auto reopened_catalog_result = Catalog::load(reopened_pager);
    REQUIRE(reopened_catalog_result.ok());
    const Catalog& reopened_catalog = reopened_catalog_result.value();

    const auto* reopened_table = reopened_catalog.find_table("users");
    REQUIRE(reopened_table != nullptr);

    const auto* reopened_email_column = reopened_catalog.find_column(reopened_table->table_id(), "email");
    REQUIRE(reopened_email_column != nullptr);

    const auto reopened_indexes = reopened_catalog.indexes_for_table(reopened_table->table_id());
    REQUIRE(reopened_indexes.size() == 2);

    const dandb::catalog::IndexDescriptor* reopened_unique_index = nullptr;
    for(const auto& index: reopened_indexes) {
        if(!index.primary()) {
            reopened_unique_index = &index;
        }
    }

    REQUIRE(reopened_unique_index != nullptr);
    REQUIRE(reopened_unique_index->internal());
    REQUIRE(reopened_unique_index->unique());
    REQUIRE(reopened_unique_index->indexed_column_id() == reopened_email_column->column_id());
    REQUIRE(reopened_unique_index->root_page_id() != reopened_table->root_page_id());

    auto reopened_unique_tree_result = BTree::open_existing(
        reopened_pager,
        reopened_unique_index->root_page_id(),
        static_cast<std::uint16_t>(reopened_email_column->logical_type().fixed_size()),
        static_cast<std::uint16_t>(schema.primary_key_column().logical_type().fixed_size())
    );
    REQUIRE(reopened_unique_tree_result.ok());
    REQUIRE(reopened_unique_tree_result.value().validate().ok());

    REQUIRE(reopened_pager.close().ok());
}

TEST_CASE("Catalog finds indexes by name", "[catalog][find-index]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());
    Catalog& catalog = catalog_result.value();

    REQUIRE(catalog.create_table("users", make_schema_with_unique_column()).ok());

    const auto* table = catalog.find_table("users");
    REQUIRE(table != nullptr);

    const auto* email_column = catalog.find_column(table->table_id(), "email");
    REQUIRE(email_column != nullptr);

    const auto* primary_index = catalog.find_index(
        dandb::catalog::internal_primary_index_name(table->table_id())
    );
    REQUIRE(primary_index != nullptr);
    REQUIRE(primary_index->primary());

    const auto* unique_index = catalog.find_index(
        dandb::catalog::internal_unique_index_name(table->table_id(), email_column->column_id())
    );
    REQUIRE(unique_index != nullptr);
    REQUIRE(unique_index->unique());

    REQUIRE(catalog.find_index("missing_index") == nullptr);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog discards staged table metadata after the caller rolls back", "[catalog][create-table]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());
    Catalog& catalog = catalog_result.value();

    REQUIRE(pager.begin_transaction().ok());
    REQUIRE(catalog.create_table("users", make_schema()).ok());
    REQUIRE(catalog.find_table("users") != nullptr);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(catalog.on_transaction_rolled_back().ok());
    REQUIRE(catalog.find_table("users") == nullptr);

    REQUIRE(pager.close().ok());

    auto reopened_pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(reopened_pager_result.ok());
    Pager& reopened_pager = reopened_pager_result.value();

    auto reopened_catalog_result = Catalog::load(reopened_pager);
    REQUIRE(reopened_catalog_result.ok());
    REQUIRE(reopened_catalog_result.value().find_table("users") == nullptr);

    REQUIRE(reopened_pager.close().ok());
}

TEST_CASE("Catalog publishes staged table metadata after the caller commits", "[catalog][create-table]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());
    Catalog& catalog = catalog_result.value();

    REQUIRE(pager.begin_transaction().ok());
    REQUIRE(catalog.create_table("users", make_schema()).ok());
    REQUIRE(catalog.find_table("users") != nullptr);

    REQUIRE(pager.commit_transaction().ok());
    REQUIRE(catalog.on_transaction_committed().ok());
    REQUIRE(catalog.find_table("users") != nullptr);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog drops table metadata that remains absent after reopening", "[catalog][drop-table]") {
    const TempDir temp_dir;

    {
        auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
        REQUIRE(pager_result.ok());
        Pager& pager = pager_result.value();

        auto catalog_result = Catalog::load(pager);
        REQUIRE(catalog_result.ok());
        Catalog& catalog = catalog_result.value();

        REQUIRE(catalog.create_table("users", make_schema()).ok());
        REQUIRE(catalog.drop_table("users").ok());
        REQUIRE(catalog.find_table("users") == nullptr);

        REQUIRE(pager.close().ok());
    }

    auto reopened_pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(reopened_pager_result.ok());
    Pager& reopened_pager = reopened_pager_result.value();

    auto reopened_catalog_result = Catalog::load(reopened_pager);
    REQUIRE(reopened_catalog_result.ok());
    REQUIRE(reopened_catalog_result.value().find_table("users") == nullptr);

    REQUIRE(reopened_pager.close().ok());
}

TEST_CASE("Catalog drop_table rejects a missing table", "[catalog][drop-table]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());

    const auto status = catalog_result.value().drop_table("users");
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::NotFound);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog drop_table rejects a system table", "[catalog][drop-table]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());
    Catalog& catalog = catalog_result.value();

    const auto status = catalog.drop_table(dandb::catalog::DANDB_TABLES_NAME);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);
    REQUIRE(catalog.find_table(dandb::catalog::DANDB_TABLES_NAME) != nullptr);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog drop_table removes internal index metadata", "[catalog][drop-table]") {
    const TempDir temp_dir;

    {
        auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
        REQUIRE(pager_result.ok());
        Pager& pager = pager_result.value();

        auto catalog_result = Catalog::load(pager);
        REQUIRE(catalog_result.ok());
        Catalog& catalog = catalog_result.value();

        REQUIRE(catalog.create_table("users", make_schema_with_unique_column()).ok());

        const auto* table = catalog.find_table("users");
        REQUIRE(table != nullptr);
        const auto table_id = table->table_id();
        REQUIRE(catalog.indexes_for_table(table_id).size() == 2);

        REQUIRE(catalog.drop_table("users").ok());
        REQUIRE(catalog.indexes_for_table(table_id).empty());

        REQUIRE(pager.close().ok());
    }

    auto reopened_pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(reopened_pager_result.ok());
    Pager& reopened_pager = reopened_pager_result.value();

    auto reopened_catalog_result = Catalog::load(reopened_pager);
    REQUIRE(reopened_catalog_result.ok());
    REQUIRE(reopened_catalog_result.value().find_table("users") == nullptr);

    REQUIRE(reopened_pager.close().ok());
}
