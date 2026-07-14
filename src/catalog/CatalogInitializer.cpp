#include "CatalogInitializer.h"

#include <dandb/catalog/IndexNames.h>
#include <dandb/catalog/SystemTables.h>
#include <dandb/record/LogicalTypeCodec.h>
#include <dandb/record/RowCodec.h>
#include <dandb/record/RowHelpers.h>
#include <dandb/record/Value.h>
#include <dandb/storage/Pager.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace dandb::catalog {

    namespace {

        core::Status insert_row(
            btree::BTree& tree,
            const record::Schema& schema,
            const record::Row& row
        ) {

            auto primary_key_result = record::RowHelpers::primary_key_bytes(schema, row);
            if(!primary_key_result.ok()) {
                return primary_key_result.status();
            }

            auto row_bytes_result = record::RowCodec::encode(schema, row);
            if(!row_bytes_result.ok()) {
                return row_bytes_result.status();
            }

            return tree.insert(primary_key_result.value(), row_bytes_result.value());

        }

    }

    CatalogInitializer::CatalogInitializer(storage::Pager& pager) :
        pager_(pager)
    {}

    core::Status CatalogInitializer::initialize() {

        auto begin_status = pager_.begin_transaction();
        if(!begin_status.ok()) {
            return begin_status;
        }

        // Create the four catalog B-trees

        auto state_result = create_system_tables();
        if(!state_result.ok()) {
            auto rollback_status = pager_.rollback_transaction();
            return rollback_status.ok() ? state_result.status() : rollback_status;
        }

        InitializationState state = std::move(state_result.value());

        // Populate catalog metadata in dependency order

        auto insert_status = insert_column_metadata(state);
        if(!insert_status.ok()) {
            auto rollback_status = pager_.rollback_transaction();
            return rollback_status.ok() ? insert_status : rollback_status;
        }

        insert_status = insert_table_metadata(state);
        if(!insert_status.ok()) {
            auto rollback_status = pager_.rollback_transaction();
            return rollback_status.ok() ? insert_status : rollback_status;
        }

        insert_status = insert_index_metadata(state);
        if(!insert_status.ok()) {
            auto rollback_status = pager_.rollback_transaction();
            return rollback_status.ok() ? insert_status : rollback_status;
        }

        insert_status = insert_index_column_metadata(state);
        if(!insert_status.ok()) {
            auto rollback_status = pager_.rollback_transaction();
            return rollback_status.ok() ? insert_status : rollback_status;
        }

        // Store system table roots in the database header

        auto mark_header_dirty_status = pager_.mark_dirty(storage::HEADER_PAGE_ID);
        if(!mark_header_dirty_status.ok()) {
            auto rollback_status = pager_.rollback_transaction();
            return rollback_status.ok() ? mark_header_dirty_status : rollback_status;
        }

        pager_.db_header_.set_system_tables_root_page_id(state.tables_tree.root_page_id());
        pager_.db_header_.set_system_columns_root_page_id(state.columns_tree.root_page_id());
        pager_.db_header_.set_system_indexes_root_page_id(state.indexes_tree.root_page_id());
        pager_.db_header_.set_system_index_columns_root_page_id(state.index_columns_tree.root_page_id());

        return pager_.commit_transaction();

    }

    core::Result<CatalogInitializer::InitializationState> CatalogInitializer::create_system_tables() {

        auto tables_schema_result = SystemTables::tables_schema();
        if(!tables_schema_result.ok()) {
            return tables_schema_result.status();
        }

        record::Schema tables_schema = std::move(tables_schema_result.value());

        auto columns_schema_result = SystemTables::columns_schema();
        if(!columns_schema_result.ok()) {
            return columns_schema_result.status();
        }

        record::Schema columns_schema = std::move(columns_schema_result.value());

        auto indexes_schema_result = SystemTables::indexes_schema();
        if(!indexes_schema_result.ok()) {
            return indexes_schema_result.status();
        }

        record::Schema indexes_schema = std::move(indexes_schema_result.value());

        auto index_columns_schema_result = SystemTables::index_columns_schema();
        if(!index_columns_schema_result.ok()) {
            return index_columns_schema_result.status();
        }

        record::Schema index_columns_schema = std::move(index_columns_schema_result.value());

        auto tables_tree_result = btree::BTree::create_new(
            pager_,
            tables_schema.primary_key_column().logical_type().fixed_size(),
            tables_schema.row_size()
        );
        if(!tables_tree_result.ok()) {
            return tables_tree_result.status();
        }

        btree::BTree tables_tree = std::move(tables_tree_result.value());

        auto columns_tree_result = btree::BTree::create_new(
            pager_,
            columns_schema.primary_key_column().logical_type().fixed_size(),
            columns_schema.row_size()
        );
        if(!columns_tree_result.ok()) {
            return columns_tree_result.status();
        }

        btree::BTree columns_tree = std::move(columns_tree_result.value());

        auto indexes_tree_result = btree::BTree::create_new(
            pager_,
            indexes_schema.primary_key_column().logical_type().fixed_size(),
            indexes_schema.row_size()
        );
        if(!indexes_tree_result.ok()) {
            return indexes_tree_result.status();
        }

        btree::BTree indexes_tree = std::move(indexes_tree_result.value());

        auto index_columns_tree_result = btree::BTree::create_new(
            pager_,
            index_columns_schema.primary_key_column().logical_type().fixed_size(),
            index_columns_schema.row_size()
        );
        if(!index_columns_tree_result.ok()) {
            return index_columns_tree_result.status();
        }

        btree::BTree index_columns_tree = std::move(index_columns_tree_result.value());

        return InitializationState{
            std::move(tables_schema),
            std::move(columns_schema),
            std::move(indexes_schema),
            std::move(index_columns_schema),
            std::move(tables_tree),
            std::move(columns_tree),
            std::move(indexes_tree),
            std::move(index_columns_tree),
            {
                INVALID_COLUMN_ID,
                INVALID_COLUMN_ID,
                INVALID_COLUMN_ID,
                INVALID_COLUMN_ID
            }
        };

    }

    core::Status CatalogInitializer::insert_column_metadata(InitializationState& state) {

        const std::array system_table_schemas{
            std::pair{ DANDB_TABLES_ID, &state.tables_schema },
            std::pair{ DANDB_COLUMNS_ID, &state.columns_schema },
            std::pair{ DANDB_INDEXES_ID, &state.indexes_schema },
            std::pair{ DANDB_INDEX_COLUMNS_ID, &state.index_columns_schema }
        };

        std::uint64_t next_column_id = 1;

        for(std::size_t table_index = 0; table_index < system_table_schemas.size(); table_index++) {

            const auto& [table_id, schema] = system_table_schemas[table_index];

            for(const auto& column: schema->columns()) {

                const ColumnId column_id{ next_column_id };
                next_column_id++;

                if(column.pk()) {
                    state.primary_key_column_ids[table_index] = column_id;
                }

                auto column_name_result = record::Value::string(column.name(), CATALOG_NAME_CAPACITY);
                if(!column_name_result.ok()) {
                    return column_name_result.status();
                }

                auto type_kind_result = record::Value::int8(record::LogicalTypeCodec::encode_kind(column.logical_type().kind()));
                if(!type_kind_result.ok()) {
                    return type_kind_result.status();
                }

                const auto type_capacity = column.logical_type().capacity();
                record::Value type_capacity_value = type_capacity.has_value()
                    ? record::Value::int64(static_cast<std::int64_t>(type_capacity.value()))
                    : record::Value::null(record::LogicalType::int64());

                record::Row column_row(std::vector<record::Value>{
                    record::Value::int64(static_cast<std::int64_t>(column_id.id)),
                    record::Value::int64(static_cast<std::int64_t>(table_id.id)),
                    std::move(column_name_result.value()),
                    std::move(type_kind_result.value()),
                    std::move(type_capacity_value),
                    record::Value::int64(static_cast<std::int64_t>(column.ordinal())),
                    record::Value::boolean(column.nullable()),
                    record::Value::boolean(column.pk()),
                    record::Value::boolean(column.unique())
                });

                auto insert_status = insert_row(state.columns_tree, state.columns_schema, column_row);
                if(!insert_status.ok()) {
                    return insert_status;
                }

            }
        }

        return core::Status::Ok();

    }

    core::Status CatalogInitializer::insert_table_metadata(InitializationState& state) {

        auto tables_name_result = record::Value::string(DANDB_TABLES_NAME, CATALOG_NAME_CAPACITY);
        if(!tables_name_result.ok()) {
            return tables_name_result.status();
        }

        record::Row tables_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_TABLES_ID.id)),
            std::move(tables_name_result.value()),
            record::Value::int64(static_cast<std::int64_t>(state.tables_tree.root_page_id().id))
        });

        auto insert_status = insert_row(state.tables_tree, state.tables_schema, tables_row);
        if(!insert_status.ok()) {
            return insert_status;
        }

        auto columns_name_result = record::Value::string(DANDB_COLUMNS_NAME, CATALOG_NAME_CAPACITY);
        if(!columns_name_result.ok()) {
            return columns_name_result.status();
        }

        record::Row columns_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_COLUMNS_ID.id)),
            std::move(columns_name_result.value()),
            record::Value::int64(static_cast<std::int64_t>(state.columns_tree.root_page_id().id))
        });

        insert_status = insert_row(state.tables_tree, state.tables_schema, columns_row);
        if(!insert_status.ok()) {
            return insert_status;
        }

        auto indexes_name_result = record::Value::string(DANDB_INDEXES_NAME, CATALOG_NAME_CAPACITY);
        if(!indexes_name_result.ok()) {
            return indexes_name_result.status();
        }

        record::Row indexes_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_INDEXES_ID.id)),
            std::move(indexes_name_result.value()),
            record::Value::int64(static_cast<std::int64_t>(state.indexes_tree.root_page_id().id))
        });

        insert_status = insert_row(state.tables_tree, state.tables_schema, indexes_row);
        if(!insert_status.ok()) {
            return insert_status;
        }

        auto index_columns_name_result = record::Value::string(
            DANDB_INDEX_COLUMNS_NAME,
            CATALOG_NAME_CAPACITY
        );
        if(!index_columns_name_result.ok()) {
            return index_columns_name_result.status();
        }

        record::Row index_columns_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_INDEX_COLUMNS_ID.id)),
            std::move(index_columns_name_result.value()),
            record::Value::int64(static_cast<std::int64_t>(state.index_columns_tree.root_page_id().id))
        });

        insert_status = insert_row(state.tables_tree, state.tables_schema, index_columns_row);
        if(!insert_status.ok()) {
            return insert_status;
        }

        return core::Status::Ok();

    }

    core::Status CatalogInitializer::insert_index_metadata(InitializationState& state) {

        auto tables_index_name_result = record::Value::string(
            internal_primary_index_name(DANDB_TABLES_ID),
            CATALOG_NAME_CAPACITY
        );
        if(!tables_index_name_result.ok()) {
            return tables_index_name_result.status();
        }

        record::Row tables_index_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_TABLES_PRIMARY_INDEX_ID.id)),
            record::Value::int64(static_cast<std::int64_t>(DANDB_TABLES_ID.id)),
            std::move(tables_index_name_result.value()),
            record::Value::int64(static_cast<std::int64_t>(state.tables_tree.root_page_id().id)),
            record::Value::boolean(true),
            record::Value::boolean(true),
            record::Value::boolean(true)
        });

        auto insert_status = insert_row(state.indexes_tree, state.indexes_schema, tables_index_row);
        if(!insert_status.ok()) {
            return insert_status;
        }

        auto columns_index_name_result = record::Value::string(
            internal_primary_index_name(DANDB_COLUMNS_ID),
            CATALOG_NAME_CAPACITY
        );
        if(!columns_index_name_result.ok()) {
            return columns_index_name_result.status();
        }

        record::Row columns_index_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_COLUMNS_PRIMARY_INDEX_ID.id)),
            record::Value::int64(static_cast<std::int64_t>(DANDB_COLUMNS_ID.id)),
            std::move(columns_index_name_result.value()),
            record::Value::int64(static_cast<std::int64_t>(state.columns_tree.root_page_id().id)),
            record::Value::boolean(true),
            record::Value::boolean(true),
            record::Value::boolean(true)
        });

        insert_status = insert_row(state.indexes_tree, state.indexes_schema, columns_index_row);
        if(!insert_status.ok()) {
            return insert_status;
        }

        auto indexes_index_name_result = record::Value::string(
            internal_primary_index_name(DANDB_INDEXES_ID),
            CATALOG_NAME_CAPACITY
        );
        if(!indexes_index_name_result.ok()) {
            return indexes_index_name_result.status();
        }

        record::Row indexes_index_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_INDEXES_PRIMARY_INDEX_ID.id)),
            record::Value::int64(static_cast<std::int64_t>(DANDB_INDEXES_ID.id)),
            std::move(indexes_index_name_result.value()),
            record::Value::int64(static_cast<std::int64_t>(state.indexes_tree.root_page_id().id)),
            record::Value::boolean(true),
            record::Value::boolean(true),
            record::Value::boolean(true)
        });

        insert_status = insert_row(state.indexes_tree, state.indexes_schema, indexes_index_row);
        if(!insert_status.ok()) {
            return insert_status;
        }

        auto index_columns_index_name_result = record::Value::string(
            internal_primary_index_name(DANDB_INDEX_COLUMNS_ID),
            CATALOG_NAME_CAPACITY
        );
        if(!index_columns_index_name_result.ok()) {
            return index_columns_index_name_result.status();
        }

        record::Row index_columns_index_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_INDEX_COLUMNS_PRIMARY_INDEX_ID.id)),
            record::Value::int64(static_cast<std::int64_t>(DANDB_INDEX_COLUMNS_ID.id)),
            std::move(index_columns_index_name_result.value()),
            record::Value::int64(static_cast<std::int64_t>(state.index_columns_tree.root_page_id().id)),
            record::Value::boolean(true),
            record::Value::boolean(true),
            record::Value::boolean(true)
        });

        insert_status = insert_row(state.indexes_tree, state.indexes_schema, index_columns_index_row);
        if(!insert_status.ok()) {
            return insert_status;
        }

        return core::Status::Ok();

    }

    core::Status CatalogInitializer::insert_index_column_metadata(InitializationState& state) {

        record::Row tables_index_column_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_TABLES_PRIMARY_INDEX_ID.id)),
            record::Value::int64(static_cast<std::int64_t>(state.primary_key_column_ids[0].id)),
            record::Value::int64(0)
        });

        auto insert_status = insert_row(
            state.index_columns_tree,
            state.index_columns_schema,
            tables_index_column_row
        );
        if(!insert_status.ok()) {
            return insert_status;
        }

        record::Row columns_index_column_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_COLUMNS_PRIMARY_INDEX_ID.id)),
            record::Value::int64(static_cast<std::int64_t>(state.primary_key_column_ids[1].id)),
            record::Value::int64(0)
        });

        insert_status = insert_row(
            state.index_columns_tree,
            state.index_columns_schema,
            columns_index_column_row
        );
        if(!insert_status.ok()) {
            return insert_status;
        }

        record::Row indexes_index_column_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_INDEXES_PRIMARY_INDEX_ID.id)),
            record::Value::int64(static_cast<std::int64_t>(state.primary_key_column_ids[2].id)),
            record::Value::int64(0)
        });

        insert_status = insert_row(
            state.index_columns_tree,
            state.index_columns_schema,
            indexes_index_column_row
        );
        if(!insert_status.ok()) {
            return insert_status;
        }

        record::Row index_columns_index_column_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(DANDB_INDEX_COLUMNS_PRIMARY_INDEX_ID.id)),
            record::Value::int64(static_cast<std::int64_t>(state.primary_key_column_ids[3].id)),
            record::Value::int64(0)
        });

        insert_status = insert_row(
            state.index_columns_tree,
            state.index_columns_schema,
            index_columns_index_column_row
        );
        if(!insert_status.ok()) {
            return insert_status;
        }

        return core::Status::Ok();

    }

}
