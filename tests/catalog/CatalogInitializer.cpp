#include <catch_amalgamated.hpp>

#include <dandb/btree/BTree.h>
#include <dandb/btree/BTreeCursor.h>
#include <dandb/catalog/IndexNames.h>
#include <dandb/catalog/SystemTables.h>
#include <dandb/record/LogicalTypeCodec.h>
#include <dandb/record/RowCodec.h>
#include <dandb/storage/DatabaseHeader.h>
#include <dandb/storage/DiskManager.h>
#include <dandb/storage/Pager.h>
#include <testutil/TempDir.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using dandb::btree::BTree;
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
using dandb::catalog::SystemTables;
using dandb::catalog::TableId;
using dandb::catalog::internal_primary_index_name;
using dandb::record::LogicalTypeCodec;
using dandb::record::Row;
using dandb::record::RowCodec;
using dandb::record::Schema;
using dandb::storage::DatabaseHeader;
using dandb::storage::DiskManager;
using dandb::storage::PageId;
using dandb::storage::Pager;
using dandb::testutil::TempDir;

namespace {

    DatabaseHeader create_and_read_header(const TempDir& temp_dir) {
        auto created_pager_result = Pager::create(temp_dir.database_path(), 10);
        REQUIRE(created_pager_result.ok());
        REQUIRE(created_pager_result.value().checkpoint().ok());
        REQUIRE(created_pager_result.value().close().ok());

        auto disk_manager_result = DiskManager::open_existing(temp_dir.database_path());
        REQUIRE(disk_manager_result.ok());

        auto header_result = disk_manager_result.value().read_header();
        REQUIRE(header_result.ok());
        REQUIRE(disk_manager_result.value().close().ok());

        return std::move(header_result.value());
    }

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

    std::vector<Row> scan_rows(BTree& tree, const Schema& schema) {
        auto cursor_result = tree.scan();
        REQUIRE(cursor_result.ok());

        auto cursor = std::move(cursor_result.value());
        std::vector<Row> rows;

        while(true) {
            auto entry_result = cursor.next();
            REQUIRE(entry_result.ok());

            if(!entry_result.value().has_value()) {
                break;
            }

            auto row_result = RowCodec::decode(schema, entry_result.value()->value);
            REQUIRE(row_result.ok());

            rows.push_back(std::move(row_result.value()));
        }

        return rows;
    }

}

TEST_CASE("Creating a database stores valid system table roots", "[catalog][initializer]") {
    const TempDir temp_dir;

    const DatabaseHeader header = create_and_read_header(temp_dir);

    REQUIRE(header.catalog_root_page_id().is_valid());
    REQUIRE(header.system_tables_root_page_id().is_valid());
    REQUIRE(header.system_columns_root_page_id().is_valid());
    REQUIRE(header.system_indexes_root_page_id().is_valid());
    REQUIRE(header.system_index_columns_root_page_id().is_valid());
    REQUIRE(header.catalog_root_page_id() == header.system_tables_root_page_id());
}

TEST_CASE("Reopened databases expose dandb_tables rows", "[catalog][initializer]") {
    const TempDir temp_dir;

    const DatabaseHeader header = create_and_read_header(temp_dir);

    auto opened_pager_result = Pager::open(temp_dir.database_path(), 1);
    REQUIRE(opened_pager_result.ok());

    auto tables_schema_result = SystemTables::tables_schema();
    REQUIRE(tables_schema_result.ok());

    auto tables_tree = open_tree(
        opened_pager_result.value(),
        header.system_tables_root_page_id(),
        tables_schema_result.value()
    );
    const auto table_rows = scan_rows(tables_tree, tables_schema_result.value());

    REQUIRE(table_rows.size() == 4);
    REQUIRE(table_rows[0].value(0).as_integer() == static_cast<std::int64_t>(DANDB_TABLES_ID.id));
    REQUIRE(table_rows[0].value(1).as_string() == DANDB_TABLES_NAME);
    REQUIRE(table_rows[0].value(2).as_integer() == static_cast<std::int64_t>(header.system_tables_root_page_id().id));
    REQUIRE(table_rows[1].value(1).as_string() == DANDB_COLUMNS_NAME);
    REQUIRE(table_rows[2].value(1).as_string() == DANDB_INDEXES_NAME);
    REQUIRE(table_rows[3].value(1).as_string() == DANDB_INDEX_COLUMNS_NAME);

    REQUIRE(opened_pager_result.value().close().ok());
}

TEST_CASE("System tables describe the complete system catalog", "[catalog][initializer]") {
    const TempDir temp_dir;

    const DatabaseHeader header = create_and_read_header(temp_dir);

    auto opened_pager_result = Pager::open(temp_dir.database_path(), 1);
    REQUIRE(opened_pager_result.ok());
    Pager& pager = opened_pager_result.value();

    auto tables_schema_result = SystemTables::tables_schema();
    auto columns_schema_result = SystemTables::columns_schema();
    auto indexes_schema_result = SystemTables::indexes_schema();
    auto index_columns_schema_result = SystemTables::index_columns_schema();

    REQUIRE(tables_schema_result.ok());
    REQUIRE(columns_schema_result.ok());
    REQUIRE(indexes_schema_result.ok());
    REQUIRE(index_columns_schema_result.ok());

    const Schema& tables_schema = tables_schema_result.value();
    const Schema& columns_schema = columns_schema_result.value();
    const Schema& indexes_schema = indexes_schema_result.value();
    const Schema& index_columns_schema = index_columns_schema_result.value();

    auto tables_tree = open_tree(pager, header.system_tables_root_page_id(), tables_schema);
    auto columns_tree = open_tree(pager, header.system_columns_root_page_id(), columns_schema);
    auto indexes_tree = open_tree(pager, header.system_indexes_root_page_id(), indexes_schema);
    auto index_columns_tree = open_tree(pager, header.system_index_columns_root_page_id(), index_columns_schema);

    const auto table_rows = scan_rows(tables_tree, tables_schema);
    const auto column_rows = scan_rows(columns_tree, columns_schema);
    const auto index_rows = scan_rows(indexes_tree, indexes_schema);
    const auto index_column_rows = scan_rows(index_columns_tree, index_columns_schema);

    const std::array system_table_schemas{
        std::pair{ DANDB_TABLES_ID, &tables_schema },
        std::pair{ DANDB_COLUMNS_ID, &columns_schema },
        std::pair{ DANDB_INDEXES_ID, &indexes_schema },
        std::pair{ DANDB_INDEX_COLUMNS_ID, &index_columns_schema }
    };
    const std::array system_table_roots{
        header.system_tables_root_page_id(),
        header.system_columns_root_page_id(),
        header.system_indexes_root_page_id(),
        header.system_index_columns_root_page_id()
    };
    const std::array primary_index_ids{
        DANDB_TABLES_PRIMARY_INDEX_ID,
        DANDB_COLUMNS_PRIMARY_INDEX_ID,
        DANDB_INDEXES_PRIMARY_INDEX_ID,
        DANDB_INDEX_COLUMNS_PRIMARY_INDEX_ID
    };

    REQUIRE(table_rows.size() == system_table_schemas.size());
    REQUIRE(index_rows.size() == system_table_schemas.size());
    REQUIRE(index_column_rows.size() == system_table_schemas.size());

    std::size_t expected_column_count = 0;
    for(const auto& [_, schema]: system_table_schemas) {
        expected_column_count += schema->column_count();
    }
    REQUIRE(column_rows.size() == expected_column_count);

    std::array<std::int64_t, 4> primary_key_column_ids{};
    std::size_t column_row_index = 0;

    for(std::size_t table_index = 0; table_index < system_table_schemas.size(); table_index++) {
        const auto& [table_id, schema] = system_table_schemas[table_index];

        REQUIRE(table_rows[table_index].value_count() == 3);
        REQUIRE(table_rows[table_index].value(0).as_integer() == static_cast<std::int64_t>(table_id.id));
        REQUIRE(table_rows[table_index].value(2).as_integer() == static_cast<std::int64_t>(system_table_roots[table_index].id));

        for(const auto& column: schema->columns()) {
            const Row& column_row = column_rows[column_row_index];
            const std::int64_t column_id = static_cast<std::int64_t>(column_row_index+1);

            REQUIRE(column_row.value(0).as_integer() == column_id);
            REQUIRE(column_row.value(1).as_integer() == static_cast<std::int64_t>(table_id.id));
            REQUIRE(column_row.value(2).as_string() == column.name());
            REQUIRE(column_row.value(3).as_integer() == LogicalTypeCodec::encode_kind(column.logical_type().kind()));
            REQUIRE(column_row.value(5).as_integer() == static_cast<std::int64_t>(column.ordinal()));
            REQUIRE(column_row.value(6).as_boolean() == column.nullable());
            REQUIRE(column_row.value(7).as_boolean() == column.pk());
            REQUIRE(column_row.value(8).as_boolean() == column.unique());

            const auto capacity = column.logical_type().capacity();
            if(capacity.has_value()) {
                REQUIRE_FALSE(column_row.value(4).is_null());
                REQUIRE(column_row.value(4).as_integer() == static_cast<std::int64_t>(capacity.value()));
            } else {
                REQUIRE(column_row.value(4).is_null());
            }

            if(column.pk()) {
                primary_key_column_ids[table_index] = column_id;
            }

            column_row_index++;
        }

        const Row& index_row = index_rows[table_index];
        REQUIRE(index_row.value(0).as_integer() == static_cast<std::int64_t>(primary_index_ids[table_index].id));
        REQUIRE(index_row.value(1).as_integer() == static_cast<std::int64_t>(table_id.id));
        REQUIRE(index_row.value(2).as_string() == internal_primary_index_name(table_id));
        REQUIRE(index_row.value(3).as_integer() == static_cast<std::int64_t>(system_table_roots[table_index].id));
        REQUIRE(index_row.value(4).as_boolean());
        REQUIRE(index_row.value(5).as_boolean());
        REQUIRE(index_row.value(6).as_boolean());

        const Row& index_column_row = index_column_rows[table_index];
        REQUIRE(index_column_row.value(0).as_integer() == static_cast<std::int64_t>(primary_index_ids[table_index].id));
        REQUIRE(index_column_row.value(1).as_integer() == primary_key_column_ids[table_index]);
        REQUIRE(index_column_row.value(2).as_integer() == 0);
    }

    REQUIRE(pager.close().ok());
}
