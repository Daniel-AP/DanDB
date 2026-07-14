#include "CatalogLoader.h"

#include <dandb/btree/BTree.h>
#include <dandb/catalog/SystemTables.h>
#include <dandb/record/Column.h>
#include <dandb/record/LogicalTypeCodec.h>
#include <dandb/record/RowCodec.h>
#include <dandb/record/RowHelpers.h>
#include <dandb/storage/DatabaseHeader.h>
#include <dandb/storage/PageId.h>
#include <dandb/storage/Pager.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dandb::catalog {

    core::Result<Catalog> CatalogLoader::load(storage::Pager& pager) {

        const auto& header = pager.database_header();
        LoadState state;

        auto status = load_tables(pager, header, state);
        if(!status.ok()) {
            return status;
        }

        status = load_columns(pager, header, state);
        if(!status.ok()) {
            return status;
        }

        status = load_index_columns(pager, header, state);
        if(!status.ok()) {
            return status;
        }

        status = load_indexes(pager, header, state);
        if(!status.ok()) {
            return status;
        }

        status = resolve_schemas(state);
        if(!status.ok()) {
            return status;
        }

        return Catalog{ pager, std::move(state.table_by_id), std::move(state.table_schema_by_id), std::move(state.table_id_by_name) };

    }

    core::Status CatalogLoader::load_tables(
        storage::Pager& pager,
        const storage::DatabaseHeader& header,
        LoadState& state
    ) {

        auto schema_result = SystemTables::tables_schema();
        if(!schema_result.ok()) {
            return schema_result.status();
        }

        record::Schema schema = std::move(schema_result.value());
        const auto root_page_id = header.system_tables_root_page_id();

        if(!root_page_id.is_valid()) {
            return core::Status::Corruption("Cannot load catalog: dandb_tables root page id is invalid");
        }

        auto tree_result = btree::BTree::open_existing(
            pager,
            root_page_id,
            static_cast<std::uint16_t>(schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(schema.row_size())
        );
        if(!tree_result.ok()) {
            if(tree_result.status().code() == core::StatusCode::IoError) {
                return tree_result.status();
            }
            return core::Status::Corruption("Cannot load catalog: invalid dandb_tables B+ tree: "+tree_result.status().message());
        }

        auto cursor_result = tree_result.value().scan();
        if(!cursor_result.ok()) {
            if(cursor_result.status().code() == core::StatusCode::IoError) {
                return cursor_result.status();
            }
            return core::Status::Corruption("Cannot load catalog: cannot scan dandb_tables: "+cursor_result.status().message());
        }

        auto cursor = std::move(cursor_result.value());

        while(true) {

            auto entry_result = cursor.next();
            if(!entry_result.ok()) {
                if(entry_result.status().code() == core::StatusCode::IoError) {
                    return entry_result.status();
                }
                return core::Status::Corruption("Cannot load catalog: invalid dandb_tables entry: "+entry_result.status().message());
            }

            if(!entry_result.value().has_value()) break;

            auto& entry = entry_result.value().value();

            auto row_result = record::RowCodec::decode(schema, entry.value);
            if(!row_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: cannot decode dandb_tables row: "+row_result.status().message());
            }

            record::Row row = std::move(row_result.value());

            auto primary_key_result = record::RowHelpers::primary_key_bytes(schema, row);
            if(!primary_key_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: cannot encode dandb_tables row key: "+primary_key_result.status().message());
            }

            if(entry.key != primary_key_result.value()) {
                return core::Status::Corruption("Cannot load catalog: dandb_tables entry key does not match row primary key");
            }

            const auto stored_table_id = row.value(0).as_integer();
            const auto& table_name = row.value(1).as_string();
            const auto stored_table_root_page_id = row.value(2).as_integer();

            if(stored_table_id < 0) {
                return core::Status::Corruption("Cannot load catalog: table id cannot be negative");
            }

            if(stored_table_root_page_id <= 0) {
                return core::Status::Corruption("Cannot load catalog: table root page id must be positive");
            }

            const TableId table_id{ static_cast<std::uint64_t>(stored_table_id) };
            const storage::PageId table_root_page_id{ static_cast<std::uint64_t>(stored_table_root_page_id) };

            if(!table_id.is_valid()) {
                return core::Status::Corruption("Cannot load catalog: table id is invalid");
            }

            if(!table_root_page_id.is_valid() || table_root_page_id == storage::HEADER_PAGE_ID || table_root_page_id.id >= header.page_count()) {
                return core::Status::Corruption("Cannot load catalog: table root page id is outside database page range");
            }

            if(state.table_by_id.contains(table_id)) {
                return core::Status::Corruption("Cannot load catalog: duplicate table id");
            }

            if(state.table_id_by_name.contains(table_name)) {
                return core::Status::Corruption("Cannot load catalog: duplicate table name");
            }

            auto descriptor_result = TableDescriptor::create(table_id, table_name, table_root_page_id);
            if(!descriptor_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: invalid table descriptor: "+descriptor_result.status().message());
            }

            state.table_by_id.emplace(table_id, Catalog::TableInfo{ std::move(descriptor_result.value()), {}, {} });
            state.table_id_by_name.emplace(table_name, table_id);

        }

        struct ExpectedSystemTable {
            TableId table_id;
            std::string_view name;
            storage::PageId root_page_id;
        };

        const std::array expected_system_tables{
            ExpectedSystemTable{ DANDB_TABLES_ID, DANDB_TABLES_NAME, header.system_tables_root_page_id() },
            ExpectedSystemTable{ DANDB_COLUMNS_ID, DANDB_COLUMNS_NAME, header.system_columns_root_page_id() },
            ExpectedSystemTable{ DANDB_INDEXES_ID, DANDB_INDEXES_NAME, header.system_indexes_root_page_id() },
            ExpectedSystemTable{ DANDB_INDEX_COLUMNS_ID, DANDB_INDEX_COLUMNS_NAME, header.system_index_columns_root_page_id() }
        };

        for(const auto& expected: expected_system_tables) {

            const auto table_it = state.table_by_id.find(expected.table_id);
            if(table_it == state.table_by_id.end()) {
                return core::Status::Corruption("Cannot load catalog: required system table is missing");
            }

            const auto& descriptor = table_it->second.table_descriptor;
            if(descriptor.name() != expected.name) {
                return core::Status::Corruption("Cannot load catalog: system table name does not match its id");
            }

            if(descriptor.root_page_id() != expected.root_page_id) {
                return core::Status::Corruption("Cannot load catalog: system table root does not match database header");
            }

        }

        return core::Status::Ok();

    }

    core::Status CatalogLoader::load_columns(
        storage::Pager& pager,
        const storage::DatabaseHeader& header,
        LoadState& state
    ) {

        auto schema_result = SystemTables::columns_schema();
        if(!schema_result.ok()) {
            return schema_result.status();
        }

        record::Schema schema = std::move(schema_result.value());
        const auto root_page_id = header.system_columns_root_page_id();

        if(!root_page_id.is_valid()) {
            return core::Status::Corruption("Cannot load catalog: dandb_columns root page id is invalid");
        }

        auto tree_result = btree::BTree::open_existing(
            pager,
            root_page_id,
            static_cast<std::uint16_t>(schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(schema.row_size())
        );
        if(!tree_result.ok()) {
            if(tree_result.status().code() == core::StatusCode::IoError) {
                return tree_result.status();
            }
            return core::Status::Corruption("Cannot load catalog: invalid dandb_columns B+ tree: "+tree_result.status().message());
        }

        auto cursor_result = tree_result.value().scan();
        if(!cursor_result.ok()) {
            if(cursor_result.status().code() == core::StatusCode::IoError) {
                return cursor_result.status();
            }
            return core::Status::Corruption("Cannot load catalog: cannot scan dandb_columns: "+cursor_result.status().message());
        }

        auto cursor = std::move(cursor_result.value());

        while(true) {

            auto entry_result = cursor.next();
            if(!entry_result.ok()) {
                if(entry_result.status().code() == core::StatusCode::IoError) {
                    return entry_result.status();
                }
                return core::Status::Corruption("Cannot load catalog: invalid dandb_columns entry: "+entry_result.status().message());
            }

            if(!entry_result.value().has_value()) break;

            auto& entry = entry_result.value().value();

            auto row_result = record::RowCodec::decode(schema, entry.value);
            if(!row_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: cannot decode dandb_columns row: "+row_result.status().message());
            }

            record::Row row = std::move(row_result.value());

            auto primary_key_result = record::RowHelpers::primary_key_bytes(schema, row);
            if(!primary_key_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: cannot encode dandb_columns row key: "+primary_key_result.status().message());
            }

            if(entry.key != primary_key_result.value()) {
                return core::Status::Corruption("Cannot load catalog: dandb_columns entry key does not match row primary key");
            }

            const auto stored_column_id = row.value(0).as_integer();
            const auto stored_table_id = row.value(1).as_integer();
            const auto& column_name = row.value(2).as_string();
            const auto stored_type_kind = row.value(3).as_integer();
            const auto& type_capacity_value = row.value(4);
            const auto stored_ordinal = row.value(5).as_integer();

            if(stored_column_id < 0 || stored_table_id < 0) {
                return core::Status::Corruption("Cannot load catalog: column and table ids cannot be negative");
            }

            if(stored_ordinal < 0) {
                return core::Status::Corruption("Cannot load catalog: column ordinal cannot be negative");
            }

            if(stored_type_kind < 0) {
                return core::Status::Corruption("Cannot load catalog: logical type kind cannot be negative");
            }

            const ColumnId column_id{ static_cast<std::uint64_t>(stored_column_id) };
            const TableId table_id{ static_cast<std::uint64_t>(stored_table_id) };

            if(!column_id.is_valid() || !table_id.is_valid()) {
                return core::Status::Corruption("Cannot load catalog: column or table id is invalid");
            }

            const auto table_it = state.table_by_id.find(table_id);
            if(table_it == state.table_by_id.end()) {
                return core::Status::Corruption("Cannot load catalog: column references an unknown table");
            }

            if(state.table_id_by_column_id.contains(column_id)) {
                return core::Status::Corruption("Cannot load catalog: duplicate column id");
            }

            std::optional<std::size_t> type_capacity;

            if(!type_capacity_value.is_null()) {
                const auto stored_capacity = type_capacity_value.as_integer();
                if(stored_capacity < 0) {
                    return core::Status::Corruption("Cannot load catalog: logical type capacity cannot be negative");
                }

                type_capacity = static_cast<std::size_t>(stored_capacity);
            }

            auto logical_type_result = record::LogicalTypeCodec::decode(static_cast<std::uint8_t>(stored_type_kind), type_capacity);
            if(!logical_type_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: invalid logical type: "+logical_type_result.status().message());
            }

            auto descriptor_result = ColumnDescriptor::create(
                column_id,
                table_id,
                column_name,
                logical_type_result.value(),
                static_cast<std::size_t>(stored_ordinal),
                row.value(6).as_boolean(),
                row.value(7).as_boolean(),
                row.value(8).as_boolean()
            );
            if(!descriptor_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: invalid column descriptor: "+descriptor_result.status().message());
            }

            table_it->second.columns.push_back(std::move(descriptor_result.value()));
            state.table_id_by_column_id.emplace(column_id, table_id);

        }

        return core::Status::Ok();

    }

    core::Status CatalogLoader::load_index_columns(
        storage::Pager& pager,
        const storage::DatabaseHeader& header,
        LoadState& state
    ) {

        auto schema_result = SystemTables::index_columns_schema();
        if(!schema_result.ok()) {
            return schema_result.status();
        }

        record::Schema schema = std::move(schema_result.value());
        const auto root_page_id = header.system_index_columns_root_page_id();

        if(!root_page_id.is_valid()) {
            return core::Status::Corruption("Cannot load catalog: dandb_index_columns root page id is invalid");
        }

        auto tree_result = btree::BTree::open_existing(
            pager,
            root_page_id,
            static_cast<std::uint16_t>(schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(schema.row_size())
        );
        if(!tree_result.ok()) {
            if(tree_result.status().code() == core::StatusCode::IoError) {
                return tree_result.status();
            }
            return core::Status::Corruption("Cannot load catalog: invalid dandb_index_columns B+ tree: "+tree_result.status().message());
        }

        auto cursor_result = tree_result.value().scan();
        if(!cursor_result.ok()) {
            if(cursor_result.status().code() == core::StatusCode::IoError) {
                return cursor_result.status();
            }
            return core::Status::Corruption("Cannot load catalog: cannot scan dandb_index_columns: "+cursor_result.status().message());
        }

        auto cursor = std::move(cursor_result.value());

        while(true) {

            auto entry_result = cursor.next();
            if(!entry_result.ok()) {
                if(entry_result.status().code() == core::StatusCode::IoError) {
                    return entry_result.status();
                }
                return core::Status::Corruption("Cannot load catalog: invalid dandb_index_columns entry: "+entry_result.status().message());
            }

            if(!entry_result.value().has_value()) {
                break;
            }

            auto& entry = entry_result.value().value();

            auto row_result = record::RowCodec::decode(schema, entry.value);
            if(!row_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: cannot decode dandb_index_columns row: "+row_result.status().message());
            }

            record::Row row = std::move(row_result.value());

            auto primary_key_result = record::RowHelpers::primary_key_bytes(schema, row);
            if(!primary_key_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: cannot encode dandb_index_columns row key: "+primary_key_result.status().message());
            }

            if(entry.key != primary_key_result.value()) {
                return core::Status::Corruption("Cannot load catalog: dandb_index_columns entry key does not match row primary key");
            }

            const auto stored_index_id = row.value(0).as_integer();
            const auto stored_column_id = row.value(1).as_integer();
            const auto stored_ordinal = row.value(2).as_integer();

            if(stored_index_id < 0 || stored_column_id < 0) {
                return core::Status::Corruption("Cannot load catalog: index and column ids cannot be negative");
            }

            if(stored_ordinal != 0) {
                return core::Status::Corruption("Cannot load catalog: index column ordinal must be zero");
            }

            const IndexId index_id{ static_cast<std::uint64_t>(stored_index_id) };
            const ColumnId column_id{ static_cast<std::uint64_t>(stored_column_id) };

            if(!index_id.is_valid() || !column_id.is_valid()) {
                return core::Status::Corruption("Cannot load catalog: index or column id is invalid");
            }

            if(!state.table_id_by_column_id.contains(column_id)) {
                return core::Status::Corruption("Cannot load catalog: index column references an unknown column");
            }

            if(state.column_id_by_index_id.contains(index_id)) {
                return core::Status::Corruption("Cannot load catalog: duplicate index column mapping");
            }

            state.column_id_by_index_id.emplace(index_id, column_id);

        }

        return core::Status::Ok();

    }

    core::Status CatalogLoader::load_indexes(
        storage::Pager& pager,
        const storage::DatabaseHeader& header,
        LoadState& state
    ) {

        auto schema_result = SystemTables::indexes_schema();
        if(!schema_result.ok()) {
            return schema_result.status();
        }

        record::Schema schema = std::move(schema_result.value());
        const auto root_page_id = header.system_indexes_root_page_id();

        if(!root_page_id.is_valid()) {
            return core::Status::Corruption("Cannot load catalog: dandb_indexes root page id is invalid");
        }

        auto tree_result = btree::BTree::open_existing(
            pager,
            root_page_id,
            static_cast<std::uint16_t>(schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(schema.row_size())
        );
        if(!tree_result.ok()) {
            if(tree_result.status().code() == core::StatusCode::IoError) {
                return tree_result.status();
            }
            return core::Status::Corruption("Cannot load catalog: invalid dandb_indexes B+ tree: "+tree_result.status().message());
        }

        auto cursor_result = tree_result.value().scan();
        if(!cursor_result.ok()) {
            if(cursor_result.status().code() == core::StatusCode::IoError) {
                return cursor_result.status();
            }
            return core::Status::Corruption("Cannot load catalog: cannot scan dandb_indexes: "+cursor_result.status().message());
        }

        auto cursor = std::move(cursor_result.value());
        std::unordered_set<std::string> index_names;

        while(true) {

            auto entry_result = cursor.next();
            if(!entry_result.ok()) {
                if(entry_result.status().code() == core::StatusCode::IoError) {
                    return entry_result.status();
                }
                return core::Status::Corruption("Cannot load catalog: invalid dandb_indexes entry: "+entry_result.status().message());
            }

            if(!entry_result.value().has_value()) {
                break;
            }

            auto& entry = entry_result.value().value();

            auto row_result = record::RowCodec::decode(schema, entry.value);
            if(!row_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: cannot decode dandb_indexes row: "+row_result.status().message());
            }

            record::Row row = std::move(row_result.value());

            auto primary_key_result = record::RowHelpers::primary_key_bytes(schema, row);
            if(!primary_key_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: cannot encode dandb_indexes row key: "+primary_key_result.status().message());
            }

            if(entry.key != primary_key_result.value()) {
                return core::Status::Corruption("Cannot load catalog: dandb_indexes entry key does not match row primary key");
            }

            const auto stored_index_id = row.value(0).as_integer();
            const auto stored_table_id = row.value(1).as_integer();
            const auto& index_name = row.value(2).as_string();
            const auto stored_root_page_id = row.value(3).as_integer();

            if(!index_names.insert(index_name).second) {
                return core::Status::Corruption("Cannot load catalog: duplicate index name");
            }

            if(stored_index_id < 0 || stored_table_id < 0) {
                return core::Status::Corruption("Cannot load catalog: index and table ids cannot be negative");
            }

            if(stored_root_page_id <= 0) {
                return core::Status::Corruption("Cannot load catalog: index root page id must be positive");
            }

            const IndexId index_id{ static_cast<std::uint64_t>(stored_index_id) };
            const TableId table_id{ static_cast<std::uint64_t>(stored_table_id) };
            const storage::PageId index_root_page_id{ static_cast<std::uint64_t>(stored_root_page_id) };

            if(!index_id.is_valid() || !table_id.is_valid()) {
                return core::Status::Corruption("Cannot load catalog: index or table id is invalid");
            }

            if(!index_root_page_id.is_valid() || index_root_page_id == storage::HEADER_PAGE_ID || index_root_page_id.id >= header.page_count()) {
                return core::Status::Corruption("Cannot load catalog: index root page id is outside database page range");
            }

            const auto table_it = state.table_by_id.find(table_id);
            if(table_it == state.table_by_id.end()) {
                return core::Status::Corruption("Cannot load catalog: index references an unknown table");
            }

            const auto index_column_it = state.column_id_by_index_id.find(index_id);
            if(index_column_it == state.column_id_by_index_id.end()) {
                return core::Status::Corruption("Cannot load catalog: index is missing its indexed-column mapping");
            }

            const auto column_table_it = state.table_id_by_column_id.find(index_column_it->second);
            if(column_table_it == state.table_id_by_column_id.end() || column_table_it->second != table_id) {
                return core::Status::Corruption("Cannot load catalog: indexed column belongs to a different table");
            }

            auto descriptor_result = IndexDescriptor::create(
                index_id,
                table_id,
                index_name,
                index_root_page_id,
                row.value(4).as_boolean(),
                row.value(5).as_boolean(),
                row.value(6).as_boolean(),
                index_column_it->second
            );
            if(!descriptor_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: invalid index descriptor: "+descriptor_result.status().message());
            }

            table_it->second.indexes.push_back(std::move(descriptor_result.value()));
            state.column_id_by_index_id.erase(index_column_it);

        }

        if(!state.column_id_by_index_id.empty()) {
            return core::Status::Corruption("Cannot load catalog: index-column mapping references an unknown index");
        }

        return core::Status::Ok();

    }

    core::Status CatalogLoader::resolve_schemas(LoadState& state) {

        for(auto& [table_id, table_info]: state.table_by_id) {

            std::sort(
                table_info.columns.begin(),
                table_info.columns.end(),
                [](const ColumnDescriptor& left, const ColumnDescriptor& right) {
                    return left.ordinal() < right.ordinal();
                }
            );

            std::vector<record::Column> columns;
            columns.reserve(table_info.columns.size());

            for(std::size_t ordinal = 0; ordinal < table_info.columns.size(); ordinal++) {

                const auto& descriptor = table_info.columns[ordinal];
                if(descriptor.ordinal() != ordinal) {
                    return core::Status::Corruption("Cannot load catalog: table column ordinals must be contiguous and start at zero");
                }

                auto column_result = record::Column::create(
                    descriptor.name(),
                    descriptor.logical_type(),
                    descriptor.nullable(),
                    descriptor.primary_key(),
                    descriptor.unique()
                );
                if(!column_result.ok()) {
                    return core::Status::Corruption("Cannot load catalog: invalid schema column: "+column_result.status().message());
                }

                columns.push_back(std::move(column_result.value()));

            }

            auto schema_result = record::Schema::create(std::move(columns));
            if(!schema_result.ok()) {
                return core::Status::Corruption("Cannot load catalog: invalid table schema: "+schema_result.status().message());
            }

            record::Schema schema = std::move(schema_result.value());

            const IndexDescriptor* primary_index = nullptr;
            std::size_t primary_index_count = 0;

            for(const auto& index: table_info.indexes) {
                if(index.primary()) {
                    primary_index = &index;
                    primary_index_count++;
                }
            }

            if(primary_index_count != 1) {
                return core::Status::Corruption("Cannot load catalog: each table must have exactly one primary index");
            }

            const auto primary_key_ordinal = schema.primary_key_ordinal();
            const auto primary_key_column_id = table_info.columns[primary_key_ordinal].column_id();

            if(primary_index->indexed_column_id() != primary_key_column_id) {
                return core::Status::Corruption("Cannot load catalog: primary index does not reference the schema primary key");
            }

            if(primary_index->root_page_id() != table_info.table_descriptor.root_page_id()) {
                return core::Status::Corruption("Cannot load catalog: primary index root does not match table root");
            }

            if(table_id == DANDB_TABLES_ID && primary_index->index_id() != DANDB_TABLES_PRIMARY_INDEX_ID) {
                return core::Status::Corruption("Cannot load catalog: dandb_tables primary index ID is invalid");
            }

            if(table_id == DANDB_COLUMNS_ID && primary_index->index_id() != DANDB_COLUMNS_PRIMARY_INDEX_ID) {
                return core::Status::Corruption("Cannot load catalog: dandb_columns primary index ID is invalid");
            }

            if(table_id == DANDB_INDEXES_ID && primary_index->index_id() != DANDB_INDEXES_PRIMARY_INDEX_ID) {
                return core::Status::Corruption("Cannot load catalog: dandb_indexes primary index ID is invalid");
            }
            
            if(table_id == DANDB_INDEX_COLUMNS_ID && primary_index->index_id() != DANDB_INDEX_COLUMNS_PRIMARY_INDEX_ID) {
                return core::Status::Corruption("Cannot load catalog: dandb_index_columns primary index ID is invalid");
            }

            state.table_schema_by_id.emplace(table_id, std::move(schema));

        }

        auto tables_schema_result = SystemTables::tables_schema();
        auto columns_schema_result = SystemTables::columns_schema();
        auto indexes_schema_result = SystemTables::indexes_schema();
        auto index_columns_schema_result = SystemTables::index_columns_schema();

        if(!tables_schema_result.ok()) {
            return tables_schema_result.status();
        }
        if(!columns_schema_result.ok()) {
            return columns_schema_result.status();
        }
        if(!indexes_schema_result.ok()) {
            return indexes_schema_result.status();
        }
        if(!index_columns_schema_result.ok()) {
            return index_columns_schema_result.status();
        }

        const std::array canonical_schemas{
            std::pair{ DANDB_TABLES_ID, &tables_schema_result.value() },
            std::pair{ DANDB_COLUMNS_ID, &columns_schema_result.value() },
            std::pair{ DANDB_INDEXES_ID, &indexes_schema_result.value() },
            std::pair{ DANDB_INDEX_COLUMNS_ID, &index_columns_schema_result.value() }
        };

        for(const auto& [table_id, canonical_schema]: canonical_schemas) {

            const auto loaded_schema_it = state.table_schema_by_id.find(table_id);
            const auto& loaded_schema = loaded_schema_it->second;

            if(loaded_schema.column_count() != canonical_schema->column_count()) {
                return core::Status::Corruption("Cannot load catalog: system table schema column count does not match canonical schema");
            }

            for(std::size_t ordinal = 0; ordinal < canonical_schema->column_count(); ordinal++) {

                const auto& loaded_column = loaded_schema.column(ordinal);
                const auto& canonical_column = canonical_schema->column(ordinal);

                bool different = (
                    loaded_column.name() != canonical_column.name() ||
                    loaded_column.logical_type().kind() != canonical_column.logical_type().kind() ||
                    loaded_column.logical_type().capacity() != canonical_column.logical_type().capacity() ||
                    loaded_column.nullable() != canonical_column.nullable() ||
                    loaded_column.pk() != canonical_column.pk() ||
                    loaded_column.unique() != canonical_column.unique()
                );

                if(different) {
                    return core::Status::Corruption("Cannot load catalog: system table schema does not match canonical schema");
                }

            }

        }

        return core::Status::Ok();

    }

}
