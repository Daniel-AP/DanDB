#include <catch_amalgamated.hpp>

#include <dandb/btree/BTree.h>
#include <dandb/btree/BTreeCursor.h>
#include <dandb/catalog/Catalog.h>
#include <dandb/catalog/IndexNames.h>
#include <dandb/catalog/SystemTables.h>
#include <dandb/core/Status.h>
#include <dandb/record/Row.h>
#include <dandb/record/RowCodec.h>
#include <dandb/record/RowHelpers.h>
#include <dandb/record/Schema.h>
#include <dandb/record/Value.h>
#include <dandb/storage/DatabaseHeader.h>
#include <dandb/storage/Pager.h>
#include <testutil/TempDir.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using dandb::btree::BTree;
using dandb::catalog::Catalog;
using dandb::catalog::CATALOG_NAME_CAPACITY;
using dandb::catalog::DANDB_COLUMNS_ID;
using dandb::catalog::DANDB_COLUMNS_NAME;
using dandb::catalog::DANDB_COLUMNS_PRIMARY_INDEX_ID;
using dandb::catalog::DANDB_INDEXES_ID;
using dandb::catalog::DANDB_INDEXES_NAME;
using dandb::catalog::DANDB_INDEXES_PRIMARY_INDEX_ID;
using dandb::catalog::DANDB_INDEX_COLUMNS_ID;
using dandb::catalog::DANDB_INDEX_COLUMNS_NAME;
using dandb::catalog::DANDB_INDEX_COLUMNS_PRIMARY_INDEX_ID;
using dandb::catalog::DANDB_TABLES_ID;
using dandb::catalog::DANDB_TABLES_NAME;
using dandb::catalog::DANDB_TABLES_PRIMARY_INDEX_ID;
using dandb::catalog::IndexId;
using dandb::catalog::SystemTables;
using dandb::catalog::TableId;
using dandb::core::StatusCode;
using dandb::record::Row;
using dandb::record::RowCodec;
using dandb::record::RowHelpers;
using dandb::record::Schema;
using dandb::record::Value;
using dandb::storage::PageId;
using dandb::storage::Pager;
using dandb::testutil::TempDir;

namespace {

    constexpr std::size_t TEST_BPM_CAPACITY = 10;

    struct StoredRow {
        std::vector<std::byte> key;
        Row row;
    };

    BTree open_tree(Pager& pager, PageId root_page_id, const Schema& schema) {
        auto tree_result = BTree::open_existing(
            pager,
            root_page_id,
            static_cast<std::uint16_t>(schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(schema.row_size())
        );
        REQUIRE(tree_result.ok());

        return std::move(tree_result.value());
    }

    StoredRow find_row(BTree& tree, const Schema& schema, std::int64_t primary_key) {
        auto cursor_result = tree.scan();
        REQUIRE(cursor_result.ok());

        auto cursor = std::move(cursor_result.value());
        std::optional<StoredRow> stored_row;

        while(true) {
            auto entry_result = cursor.next();
            REQUIRE(entry_result.ok());

            if(!entry_result.value().has_value()) break;

            auto row_result = RowCodec::decode(schema, entry_result.value()->value);
            REQUIRE(row_result.ok());

            if(row_result.value().value(schema.primary_key_ordinal()).as_integer() == primary_key) {
                stored_row.emplace(StoredRow{ entry_result.value()->key, std::move(row_result.value()) });
                break;
            }
        }

        REQUIRE(stored_row.has_value());
        return std::move(stored_row.value());
    }

    void replace_row(BTree& tree, const Schema& schema, const StoredRow& stored_row, std::vector<Value> replacement_values) {
        auto replacement_row_result = RowHelpers::build_row(schema, std::move(replacement_values));
        REQUIRE(replacement_row_result.ok());

        auto replacement_key_result = RowHelpers::primary_key_bytes(schema, replacement_row_result.value());
        auto replacement_value_result = RowCodec::encode(schema, replacement_row_result.value());
        REQUIRE(replacement_key_result.ok());
        REQUIRE(replacement_value_result.ok());

        REQUIRE(tree.erase(stored_row.key).ok());
        REQUIRE(tree.insert(replacement_key_result.value(), replacement_value_result.value()).ok());
    }

}

TEST_CASE("Catalog loads system metadata from an initialized database", "[catalog][loader]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());
    const Catalog& catalog = catalog_result.value();

    auto tables_schema_result = SystemTables::tables_schema();
    auto columns_schema_result = SystemTables::columns_schema();
    auto indexes_schema_result = SystemTables::indexes_schema();
    auto index_columns_schema_result = SystemTables::index_columns_schema();
    REQUIRE(tables_schema_result.ok());
    REQUIRE(columns_schema_result.ok());
    REQUIRE(indexes_schema_result.ok());
    REQUIRE(index_columns_schema_result.ok());

    const std::array expected_system_tables{
        std::tuple{ DANDB_TABLES_ID, &tables_schema_result.value(), DANDB_TABLES_PRIMARY_INDEX_ID },
        std::tuple{ DANDB_COLUMNS_ID, &columns_schema_result.value(), DANDB_COLUMNS_PRIMARY_INDEX_ID },
        std::tuple{ DANDB_INDEXES_ID, &indexes_schema_result.value(), DANDB_INDEXES_PRIMARY_INDEX_ID },
        std::tuple{ DANDB_INDEX_COLUMNS_ID, &index_columns_schema_result.value(), DANDB_INDEX_COLUMNS_PRIMARY_INDEX_ID }
    };

    for(const auto& [table_id, canonical_schema, primary_index_id]: expected_system_tables) {
        const auto* table = catalog.find_table(table_id);
        const auto* loaded_schema = catalog.schema_for_table(table_id);
        REQUIRE(table != nullptr);
        REQUIRE(loaded_schema != nullptr);
        REQUIRE(loaded_schema->column_count() == canonical_schema->column_count());

        for(const auto& canonical_column: canonical_schema->columns()) {
            const auto* loaded_column = catalog.find_column(table_id, canonical_column.name());
            REQUIRE(loaded_column != nullptr);
            REQUIRE(loaded_column->ordinal() == canonical_column.ordinal());
            REQUIRE(loaded_column->logical_type().kind() == canonical_column.logical_type().kind());
            REQUIRE(loaded_column->logical_type().capacity() == canonical_column.logical_type().capacity());
            REQUIRE(loaded_column->nullable() == canonical_column.nullable());
            REQUIRE(loaded_column->primary_key() == canonical_column.pk());
            REQUIRE(loaded_column->unique() == canonical_column.unique());
        }

        const auto indexes = catalog.indexes_for_table(table_id);
        REQUIRE(indexes.size() == 1);
        REQUIRE(indexes[0].index_id() == primary_index_id);
        REQUIRE(indexes[0].root_page_id() == table->root_page_id());
        REQUIRE(indexes[0].primary());
    }

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog finds every system table by name", "[catalog][loader]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());
    const Catalog& catalog = catalog_result.value();

    const std::array expected_system_tables{
        std::pair<std::string_view, TableId>{ DANDB_TABLES_NAME, DANDB_TABLES_ID },
        std::pair<std::string_view, TableId>{ DANDB_COLUMNS_NAME, DANDB_COLUMNS_ID },
        std::pair<std::string_view, TableId>{ DANDB_INDEXES_NAME, DANDB_INDEXES_ID },
        std::pair<std::string_view, TableId>{ DANDB_INDEX_COLUMNS_NAME, DANDB_INDEX_COLUMNS_ID }
    };

    for(const auto& [name, table_id]: expected_system_tables) {
        const auto* table = catalog.find_table(name);
        REQUIRE(table != nullptr);
        REQUIRE(table->table_id() == table_id);
    }

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog loads system metadata after reopening the database", "[catalog][loader]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    REQUIRE(pager_result.value().close().ok());

    auto reopened_pager_result = Pager::open(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(reopened_pager_result.ok());
    Pager& reopened_pager = reopened_pager_result.value();

    auto catalog_result = Catalog::load(reopened_pager);
    REQUIRE(catalog_result.ok());

    const Catalog& catalog = catalog_result.value();
    const auto* tables = catalog.find_table(DANDB_TABLES_NAME);
    REQUIRE(tables != nullptr);
    REQUIRE(tables->table_id() == DANDB_TABLES_ID);
    REQUIRE(catalog.schema_for_table(DANDB_TABLES_ID) != nullptr);

    REQUIRE(reopened_pager.close().ok());
}

TEST_CASE("Catalog lookups return no metadata for unknown tables and columns", "[catalog][loader]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());
    const Catalog& catalog = catalog_result.value();
    const TableId unknown_table_id{ 999 };

    REQUIRE(catalog.find_table("unknown_table") == nullptr);
    REQUIRE(catalog.find_table(unknown_table_id) == nullptr);
    REQUIRE(catalog.schema_for_table(unknown_table_id) == nullptr);
    REQUIRE(catalog.find_column(DANDB_TABLES_ID, "unknown_column") == nullptr);
    REQUIRE(catalog.find_column(unknown_table_id, "table_id") == nullptr);
    REQUIRE(catalog.indexes_for_table(unknown_table_id).empty());

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog load rejects duplicate table names", "[catalog][loader][corruption]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto schema_result = SystemTables::tables_schema();
    REQUIRE(schema_result.ok());
    const Schema& schema = schema_result.value();
    auto tree = open_tree(pager, pager.database_header().system_tables_root_page_id(), schema);
    const auto stored_row = find_row(tree, schema, static_cast<std::int64_t>(DANDB_COLUMNS_ID.id));

    auto duplicate_name_result = Value::string(DANDB_TABLES_NAME, CATALOG_NAME_CAPACITY);
    REQUIRE(duplicate_name_result.ok());
    auto replacement_values = stored_row.row.values();
    replacement_values[1] = std::move(duplicate_name_result.value());

    REQUIRE(pager.begin_transaction().ok());
    replace_row(tree, schema, stored_row, std::move(replacement_values));
    REQUIRE(pager.commit_transaction().ok());

    const auto catalog_result = Catalog::load(pager);
    REQUIRE_FALSE(catalog_result.ok());
    REQUIRE(catalog_result.status().code() == StatusCode::Corruption);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog load rejects duplicate index names", "[catalog][loader][corruption]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto schema_result = SystemTables::indexes_schema();
    REQUIRE(schema_result.ok());
    const Schema& schema = schema_result.value();
    auto tree = open_tree(pager, pager.database_header().system_indexes_root_page_id(), schema);
    const auto stored_row = find_row(tree, schema, static_cast<std::int64_t>(DANDB_COLUMNS_PRIMARY_INDEX_ID.id));

    auto duplicate_name_result = Value::string(
        dandb::catalog::internal_primary_index_name(DANDB_TABLES_ID),
        CATALOG_NAME_CAPACITY
    );
    REQUIRE(duplicate_name_result.ok());
    auto replacement_values = stored_row.row.values();
    replacement_values[2] = std::move(duplicate_name_result.value());

    REQUIRE(pager.begin_transaction().ok());
    replace_row(tree, schema, stored_row, std::move(replacement_values));
    REQUIRE(pager.commit_transaction().ok());

    const auto catalog_result = Catalog::load(pager);
    REQUIRE_FALSE(catalog_result.ok());
    REQUIRE(catalog_result.status().code() == StatusCode::Corruption);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog load rejects a missing required system table row", "[catalog][loader][corruption]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto schema_result = SystemTables::tables_schema();
    REQUIRE(schema_result.ok());
    const Schema& schema = schema_result.value();
    auto tree = open_tree(pager, pager.database_header().system_tables_root_page_id(), schema);
    const auto stored_row = find_row(tree, schema, static_cast<std::int64_t>(DANDB_INDEX_COLUMNS_ID.id));

    REQUIRE(pager.begin_transaction().ok());
    REQUIRE(tree.erase(stored_row.key).ok());
    REQUIRE(pager.commit_transaction().ok());

    const auto catalog_result = Catalog::load(pager);
    REQUIRE_FALSE(catalog_result.ok());
    REQUIRE(catalog_result.status().code() == StatusCode::Corruption);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog load rejects an index missing its column mapping", "[catalog][loader][corruption]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto schema_result = SystemTables::index_columns_schema();
    REQUIRE(schema_result.ok());
    const Schema& schema = schema_result.value();
    auto tree = open_tree(pager, pager.database_header().system_index_columns_root_page_id(), schema);
    const auto stored_row = find_row(
        tree,
        schema,
        static_cast<std::int64_t>(DANDB_TABLES_PRIMARY_INDEX_ID.id)
    );

    REQUIRE(pager.begin_transaction().ok());
    REQUIRE(tree.erase(stored_row.key).ok());
    REQUIRE(pager.commit_transaction().ok());

    const auto catalog_result = Catalog::load(pager);
    REQUIRE_FALSE(catalog_result.ok());
    REQUIRE(catalog_result.status().code() == StatusCode::Corruption);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog load rejects a table root outside the database page range", "[catalog][loader][corruption]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto schema_result = SystemTables::tables_schema();
    REQUIRE(schema_result.ok());
    const Schema& schema = schema_result.value();
    auto tree = open_tree(pager, pager.database_header().system_tables_root_page_id(), schema);
    const auto stored_row = find_row(tree, schema, static_cast<std::int64_t>(DANDB_TABLES_ID.id));

    auto replacement_values = stored_row.row.values();
    replacement_values[2] = Value::int64(static_cast<std::int64_t>(pager.database_header().page_count()));

    REQUIRE(pager.begin_transaction().ok());
    replace_row(tree, schema, stored_row, std::move(replacement_values));
    REQUIRE(pager.commit_transaction().ok());

    const auto catalog_result = Catalog::load(pager);
    REQUIRE_FALSE(catalog_result.ok());
    REQUIRE(catalog_result.status().code() == StatusCode::Corruption);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Catalog load rejects changed system primary index ids", "[catalog][loader][corruption]") {
    const std::int64_t stored_index_id = GENERATE(1, 2, 3, 4);
    const std::int64_t replacement_index_id = stored_index_id+100;
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto indexes_schema_result = SystemTables::indexes_schema();
    auto index_columns_schema_result = SystemTables::index_columns_schema();
    REQUIRE(indexes_schema_result.ok());
    REQUIRE(index_columns_schema_result.ok());
    const Schema& indexes_schema = indexes_schema_result.value();
    const Schema& index_columns_schema = index_columns_schema_result.value();

    auto indexes_tree = open_tree(pager, pager.database_header().system_indexes_root_page_id(), indexes_schema);
    auto index_columns_tree = open_tree(pager, pager.database_header().system_index_columns_root_page_id(), index_columns_schema);
    const auto stored_index_row = find_row(indexes_tree, indexes_schema, stored_index_id);
    const auto stored_index_column_row = find_row(index_columns_tree, index_columns_schema, stored_index_id);

    auto replacement_index_values = stored_index_row.row.values();
    auto replacement_index_column_values = stored_index_column_row.row.values();
    replacement_index_values[0] = Value::int64(replacement_index_id);
    replacement_index_column_values[0] = Value::int64(replacement_index_id);

    REQUIRE(pager.begin_transaction().ok());
    replace_row(indexes_tree, indexes_schema, stored_index_row, std::move(replacement_index_values));
    replace_row(index_columns_tree, index_columns_schema, stored_index_column_row, std::move(replacement_index_column_values));
    REQUIRE(pager.commit_transaction().ok());

    const auto catalog_result = Catalog::load(pager);
    REQUIRE_FALSE(catalog_result.ok());
    REQUIRE(catalog_result.status().code() == StatusCode::Corruption);

    REQUIRE(pager.close().ok());
}
