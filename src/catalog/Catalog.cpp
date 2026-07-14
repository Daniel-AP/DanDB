#include <dandb/catalog/Catalog.h>

#include "CatalogInitializer.h"
#include "CatalogLoader.h"

#include <dandb/btree/BTree.h>
#include <dandb/catalog/CatalogNames.h>
#include <dandb/catalog/IndexNames.h>
#include <dandb/catalog/SystemTables.h>
#include <dandb/record/KeyCodec.h>
#include <dandb/record/LogicalTypeCodec.h>
#include <dandb/record/RowCodec.h>
#include <dandb/record/RowHelpers.h>
#include <dandb/record/Value.h>
#include <dandb/storage/Pager.h>

#include <cstdint>
#include <limits>
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

        core::Status erase_row(btree::BTree& tree, std::uint64_t object_id) {
            auto key_result = record::KeyCodec::encode(
                record::Value::int64(static_cast<std::int64_t>(object_id))
            );
            if(!key_result.ok()) {
                return key_result.status();
            }

            return tree.erase(key_result.value());
        };

    }

    Catalog::Catalog(
        storage::Pager& pager,
        std::unordered_map<TableId, TableInfo> table_by_id,
        std::unordered_map<TableId, record::Schema> table_schema_by_id,
        std::unordered_map<std::string, TableId> table_id_by_name
    ) :
        pager_(&pager),
        committed_state_{
            INVALID_TABLE_ID,
            INVALID_COLUMN_ID,
            INVALID_INDEX_ID,
            std::move(table_by_id),
            std::move(table_schema_by_id),
            std::move(table_id_by_name)
        }
    {

        std::uint64_t maximum_table_id = 0;
        std::uint64_t maximum_column_id = 0;
        std::uint64_t maximum_index_id = 0;

        for(const auto& [table_id, table_info]: committed_state_.table_by_id_) {

            if(table_id.id > maximum_table_id) {
                maximum_table_id = table_id.id;
            }

            for(const auto& column: table_info.columns) {
                if(column.column_id().id > maximum_column_id) {
                    maximum_column_id = column.column_id().id;
                }
            }

            for(const auto& index: table_info.indexes) {
                if(index.index_id().id > maximum_index_id) {
                    maximum_index_id = index.index_id().id;
                }
            }

        }

        committed_state_.next_table_id_ = TableId{ maximum_table_id+1 };
        committed_state_.next_column_id_ = ColumnId{ maximum_column_id+1 };
        committed_state_.next_index_id_ = IndexId{ maximum_index_id+1 };

    }

    core::Status Catalog::initialize(storage::Pager& pager) {
        return CatalogInitializer(pager).initialize();
    }

    core::Result<Catalog> Catalog::load(storage::Pager& pager) {
        return CatalogLoader::load(pager);
    }

    core::Status Catalog::create_table(std::string name, const record::Schema& schema) {

        if(name.empty()) {
            return core::Status::InvalidArgument("Cannot create table: name cannot be empty");
        }

        if(name.size() > CATALOG_NAME_CAPACITY) {
            return core::Status::InvalidArgument("Cannot create table: name exceeds the catalog capacity");
        }

        if(find_table(name) != nullptr) {
            return core::Status::AlreadyExists("Cannot create table: a table with this name already exists");
        }

        if(has_reserved_catalog_prefix(name)) {
            return core::Status::InvalidArgument("Cannot create table: name uses the reserved catalog prefix");
        }

        for(const auto& column: schema.columns()) {
            if(column.name().size() > CATALOG_NAME_CAPACITY) {
                return core::Status::InvalidArgument("Cannot create table: column name exceeds the catalog capacity");
            }
        }

        if(schema.primary_key_column().logical_type().fixed_size() > std::numeric_limits<std::uint16_t>::max() || schema.row_size() > std::numeric_limits<std::uint16_t>::max()) {
            return core::Status::InvalidArgument("Cannot create table: schema exceeds B+ tree size limits");
        }

        // Get the system table schemas
        auto tables_schema_result = SystemTables::tables_schema();
        if(!tables_schema_result.ok()) {
            return tables_schema_result.status();
        }

        auto columns_schema_result = SystemTables::columns_schema();
        if(!columns_schema_result.ok()) {
            return columns_schema_result.status();
        }

        auto indexes_schema_result = SystemTables::indexes_schema();
        if(!indexes_schema_result.ok()) {
            return indexes_schema_result.status();
        }

        auto index_columns_schema_result = SystemTables::index_columns_schema();
        if(!index_columns_schema_result.ok()) {
            return index_columns_schema_result.status();
        }

        const auto& tables_schema = tables_schema_result.value();
        const auto& columns_schema = columns_schema_result.value();
        const auto& indexes_schema = indexes_schema_result.value();
        const auto& index_columns_schema = index_columns_schema_result.value();

        // Open the system B+ trees
        auto tables_tree_result = btree::BTree::open_existing(
            *pager_,
            pager_->database_header().system_tables_root_page_id(),
            static_cast<std::uint16_t>(tables_schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(tables_schema.row_size())
        );
        if(!tables_tree_result.ok()) {
            return tables_tree_result.status();
        }

        auto columns_tree_result = btree::BTree::open_existing(
            *pager_,
            pager_->database_header().system_columns_root_page_id(),
            static_cast<std::uint16_t>(columns_schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(columns_schema.row_size())
        );
        if(!columns_tree_result.ok()) {
            return columns_tree_result.status();
        }

        auto indexes_tree_result = btree::BTree::open_existing(
            *pager_,
            pager_->database_header().system_indexes_root_page_id(),
            static_cast<std::uint16_t>(indexes_schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(indexes_schema.row_size())
        );
        if(!indexes_tree_result.ok()) {
            return indexes_tree_result.status();
        }

        auto index_columns_tree_result = btree::BTree::open_existing(
            *pager_,
            pager_->database_header().system_index_columns_root_page_id(),
            static_cast<std::uint16_t>(index_columns_schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(index_columns_schema.row_size())
        );
        if(!index_columns_tree_result.ok()) {
            return index_columns_tree_result.status();
        }

        btree::BTree tables_tree = std::move(tables_tree_result.value());
        btree::BTree columns_tree = std::move(columns_tree_result.value());
        btree::BTree indexes_tree = std::move(indexes_tree_result.value());
        btree::BTree index_columns_tree = std::move(index_columns_tree_result.value());

        // Start a transaction only if needed
        const bool owns_transaction = !pager_->in_transaction();
        if(owns_transaction) {
            auto begin_status = pager_->begin_transaction();
            if(!begin_status.ok()) {
                return begin_status;
            }
        }

        // Reserve ids and build metadata for the new table
        CatalogState next_catalog_state = visible_state();
        const TableId new_table_id = next_catalog_state.next_table_id_;
        next_catalog_state.next_table_id_ = TableId{ new_table_id.id+1 };

        std::vector<ColumnDescriptor> new_table_column_descriptors;
        new_table_column_descriptors.reserve(schema.column_count());
        ColumnId new_table_primary_key_column_id = INVALID_COLUMN_ID;

        for(const auto& new_table_column: schema.columns()) {

            const ColumnId new_table_column_id = next_catalog_state.next_column_id_;
            next_catalog_state.next_column_id_ = ColumnId{ new_table_column_id.id+1 };

            auto new_table_column_descriptor_result = ColumnDescriptor::create(
                new_table_column_id,
                new_table_id,
                new_table_column.name(),
                new_table_column.logical_type(),
                new_table_column.ordinal(),
                new_table_column.nullable(),
                new_table_column.pk(),
                new_table_column.unique()
            );
            if(!new_table_column_descriptor_result.ok()) {
                return handle_mutation_failure(new_table_column_descriptor_result.status(), owns_transaction);
            }

            if(new_table_column.pk()) {
                new_table_primary_key_column_id = new_table_column_id;
            }

            new_table_column_descriptors.push_back(std::move(new_table_column_descriptor_result.value()));

        }

        // Create storage for the new table
        auto new_table_storage_tree_result = btree::BTree::create_new(
            *pager_,
            static_cast<std::uint16_t>(schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(schema.row_size())
        );
        if(!new_table_storage_tree_result.ok()) {
            return handle_mutation_failure(new_table_storage_tree_result.status(), owns_transaction);
        }

        const auto new_table_storage_root_page_id = new_table_storage_tree_result.value().root_page_id();

        auto new_table_descriptor_result = TableDescriptor::create(new_table_id, name, new_table_storage_root_page_id);
        if(!new_table_descriptor_result.ok()) {
            return handle_mutation_failure(new_table_descriptor_result.status(), owns_transaction);
        }

        TableDescriptor new_table_descriptor = std::move(new_table_descriptor_result.value());

        const IndexId new_table_primary_index_id = next_catalog_state.next_index_id_;
        next_catalog_state.next_index_id_ = IndexId{ new_table_primary_index_id.id+1 };

        // The primary index shares the table storage tree
        auto new_table_primary_index_result = IndexDescriptor::create(
            new_table_primary_index_id,
            new_table_id,
            internal_primary_index_name(new_table_id),
            new_table_storage_root_page_id,
            true,
            true,
            true,
            new_table_primary_key_column_id
        );
        if(!new_table_primary_index_result.ok()) {
            return handle_mutation_failure(new_table_primary_index_result.status(), owns_transaction);
        }

        std::vector<IndexDescriptor> new_table_index_descriptors;
        new_table_index_descriptors.push_back(std::move(new_table_primary_index_result.value()));

        // Create an internal index for each other unique column
        for(const auto& new_table_column_descriptor: new_table_column_descriptors) {

            if(new_table_column_descriptor.primary_key() || !new_table_column_descriptor.unique()) continue;

            auto new_table_internal_unique_index_tree_result = btree::BTree::create_new(
                *pager_,
                static_cast<std::uint16_t>(new_table_column_descriptor.logical_type().fixed_size()),
                static_cast<std::uint16_t>(schema.primary_key_column().logical_type().fixed_size())
            );
            if(!new_table_internal_unique_index_tree_result.ok()) {
                return handle_mutation_failure(new_table_internal_unique_index_tree_result.status(), owns_transaction);
            }

            const IndexId new_table_internal_unique_index_id = next_catalog_state.next_index_id_;
            next_catalog_state.next_index_id_ = IndexId{ new_table_internal_unique_index_id.id+1 };

            auto new_table_internal_unique_index_result = IndexDescriptor::create(
                new_table_internal_unique_index_id,
                new_table_id,
                internal_unique_index_name(new_table_id, new_table_column_descriptor.column_id()),
                new_table_internal_unique_index_tree_result.value().root_page_id(),
                true,
                false,
                true,
                new_table_column_descriptor.column_id()
            );
            if(!new_table_internal_unique_index_result.ok()) {
                return handle_mutation_failure(new_table_internal_unique_index_result.status(), owns_transaction);
            }

            new_table_index_descriptors.push_back(std::move(new_table_internal_unique_index_result.value()));

        }

        // Write the new table metadata
        auto new_table_name_value_result = record::Value::string(name, CATALOG_NAME_CAPACITY);
        if(!new_table_name_value_result.ok()) {
            return handle_mutation_failure(new_table_name_value_result.status(), owns_transaction);
        }

        record::Row new_table_metadata_row(std::vector<record::Value>{
            record::Value::int64(static_cast<std::int64_t>(new_table_id.id)),
            std::move(new_table_name_value_result.value()),
            record::Value::int64(static_cast<std::int64_t>(new_table_storage_root_page_id.id))
        });

        auto insert_status = insert_row(tables_tree, tables_schema, new_table_metadata_row);
        if(!insert_status.ok()) {
            return handle_mutation_failure(insert_status, owns_transaction);
        }

        // Write the new column metadata
        for(const auto& new_table_column_descriptor: new_table_column_descriptors) {

            auto new_table_column_name_value_result = record::Value::string(new_table_column_descriptor.name(), CATALOG_NAME_CAPACITY);
            if(!new_table_column_name_value_result.ok()) {
                return handle_mutation_failure(new_table_column_name_value_result.status(), owns_transaction);
            }

            auto new_table_column_type_kind_result = record::Value::int8(
                record::LogicalTypeCodec::encode_kind(new_table_column_descriptor.logical_type().kind())
            );
            if(!new_table_column_type_kind_result.ok()) {
                return handle_mutation_failure(new_table_column_type_kind_result.status(), owns_transaction);
            }

            const auto type_capacity = new_table_column_descriptor.logical_type().capacity();
            record::Value type_capacity_value = type_capacity.has_value()
                ? record::Value::int64(static_cast<std::int64_t>(type_capacity.value()))
                : record::Value::null(record::LogicalType::int64());

            record::Row new_table_column_metadata_row(std::vector<record::Value>{
                record::Value::int64(static_cast<std::int64_t>(new_table_column_descriptor.column_id().id)),
                record::Value::int64(static_cast<std::int64_t>(new_table_id.id)),
                std::move(new_table_column_name_value_result.value()),
                std::move(new_table_column_type_kind_result.value()),
                std::move(type_capacity_value),
                record::Value::int64(static_cast<std::int64_t>(new_table_column_descriptor.ordinal())),
                record::Value::boolean(new_table_column_descriptor.nullable()),
                record::Value::boolean(new_table_column_descriptor.primary_key()),
                record::Value::boolean(new_table_column_descriptor.unique())
            });

            insert_status = insert_row(columns_tree, columns_schema, new_table_column_metadata_row);
            if(!insert_status.ok()) {
                return handle_mutation_failure(insert_status, owns_transaction);
            }

        }

        // Write the new index metadata
        for(const auto& new_table_index_descriptor: new_table_index_descriptors) {

            auto new_table_index_name_value_result = record::Value::string(new_table_index_descriptor.name(), CATALOG_NAME_CAPACITY);
            if(!new_table_index_name_value_result.ok()) {
                return handle_mutation_failure(new_table_index_name_value_result.status(), owns_transaction);
            }

            record::Row new_table_index_metadata_row(std::vector<record::Value>{
                record::Value::int64(static_cast<std::int64_t>(new_table_index_descriptor.index_id().id)),
                record::Value::int64(static_cast<std::int64_t>(new_table_id.id)),
                std::move(new_table_index_name_value_result.value()),
                record::Value::int64(static_cast<std::int64_t>(new_table_index_descriptor.root_page_id().id)),
                record::Value::boolean(new_table_index_descriptor.unique()),
                record::Value::boolean(new_table_index_descriptor.primary()),
                record::Value::boolean(new_table_index_descriptor.internal())
            });

            insert_status = insert_row(indexes_tree, indexes_schema, new_table_index_metadata_row);
            if(!insert_status.ok()) {
                return handle_mutation_failure(insert_status, owns_transaction);
            }

            // Link each index to its column
            record::Row new_table_index_column_metadata_row(std::vector<record::Value>{
                record::Value::int64(static_cast<std::int64_t>(new_table_index_descriptor.index_id().id)),
                record::Value::int64(static_cast<std::int64_t>(new_table_index_descriptor.indexed_column_id().id)),
                record::Value::int64(0)
            });

            insert_status = insert_row(
                index_columns_tree,
                index_columns_schema,
                new_table_index_column_metadata_row
            );
            if(!insert_status.ok()) {
                return handle_mutation_failure(insert_status, owns_transaction);
            }

        }

        // Add the new table to the next catalog state
        next_catalog_state.table_by_id_.emplace(
            new_table_id,
            TableInfo{
                std::move(new_table_descriptor),
                std::move(new_table_column_descriptors),
                std::move(new_table_index_descriptors)
            }
        );
        next_catalog_state.table_schema_by_id_.emplace(new_table_id, schema);
        next_catalog_state.table_id_by_name_.emplace(std::move(name), new_table_id);

        // Keep the state staged for the caller's transaction
        if(!owns_transaction) {
            staged_state_ = std::move(next_catalog_state);
            return core::Status::Ok();
        }

        // Commit before updating the catalog state
        auto commit_status = pager_->commit_transaction();
        if(!commit_status.ok()) {
            return commit_status;
        }

        committed_state_ = std::move(next_catalog_state);
        return core::Status::Ok();

    }

    core::Status Catalog::drop_table(std::string name) {

        const TableDescriptor* table_descriptor = find_table(name);
        if(table_descriptor == nullptr) {
            return core::Status::NotFound("Cannot drop table: table was not found");
        }

        const TableId table_id = table_descriptor->table_id();
        bool is_system_table = (
            table_id == DANDB_TABLES_ID ||
            table_id == DANDB_COLUMNS_ID ||
            table_id == DANDB_INDEXES_ID ||
            table_id == DANDB_INDEX_COLUMNS_ID
        );

        if(is_system_table) {
            return core::Status::InvalidArgument("Cannot drop table: system tables cannot be dropped");
        }

        // Get the system table schemas
        auto tables_schema_result = SystemTables::tables_schema();
        if(!tables_schema_result.ok()) {
            return tables_schema_result.status();
        }

        auto columns_schema_result = SystemTables::columns_schema();
        if(!columns_schema_result.ok()) {
            return columns_schema_result.status();
        }

        auto indexes_schema_result = SystemTables::indexes_schema();
        if(!indexes_schema_result.ok()) {
            return indexes_schema_result.status();
        }

        auto index_columns_schema_result = SystemTables::index_columns_schema();
        if(!index_columns_schema_result.ok()) {
            return index_columns_schema_result.status();
        }

        const auto& tables_schema = tables_schema_result.value();
        const auto& columns_schema = columns_schema_result.value();
        const auto& indexes_schema = indexes_schema_result.value();
        const auto& index_columns_schema = index_columns_schema_result.value();

        // Open the system B+ trees
        auto tables_tree_result = btree::BTree::open_existing(
            *pager_,
            pager_->database_header().system_tables_root_page_id(),
            static_cast<std::uint16_t>(tables_schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(tables_schema.row_size())
        );
        if(!tables_tree_result.ok()) {
            return tables_tree_result.status();
        }

        auto columns_tree_result = btree::BTree::open_existing(
            *pager_,
            pager_->database_header().system_columns_root_page_id(),
            static_cast<std::uint16_t>(columns_schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(columns_schema.row_size())
        );
        if(!columns_tree_result.ok()) {
            return columns_tree_result.status();
        }

        auto indexes_tree_result = btree::BTree::open_existing(
            *pager_,
            pager_->database_header().system_indexes_root_page_id(),
            static_cast<std::uint16_t>(indexes_schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(indexes_schema.row_size())
        );
        if(!indexes_tree_result.ok()) {
            return indexes_tree_result.status();
        }

        auto index_columns_tree_result = btree::BTree::open_existing(
            *pager_,
            pager_->database_header().system_index_columns_root_page_id(),
            static_cast<std::uint16_t>(index_columns_schema.primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(index_columns_schema.row_size())
        );
        if(!index_columns_tree_result.ok()) {
            return index_columns_tree_result.status();
        }

        btree::BTree tables_tree = std::move(tables_tree_result.value());
        btree::BTree columns_tree = std::move(columns_tree_result.value());
        btree::BTree indexes_tree = std::move(indexes_tree_result.value());
        btree::BTree index_columns_tree = std::move(index_columns_tree_result.value());

        // Start a transaction only if needed
        const bool owns_transaction = !pager_->in_transaction();
        if(owns_transaction) {
            auto begin_status = pager_->begin_transaction();
            if(!begin_status.ok()) {
                return begin_status;
            }
        }

        // Copy the visible state before removing metadata
        CatalogState next_catalog_state = visible_state();
        const auto table_it = next_catalog_state.table_by_id_.find(table_id);
        if(table_it == next_catalog_state.table_by_id_.end()) {
            return handle_mutation_failure(
                core::Status::InternalError("Cannot drop table: table is missing from the catalog state"),
                owns_transaction
            );
        }

        const auto& table_info = table_it->second;
        const std::string table_name = table_info.table_descriptor.name();

        // Remove index metadata before the table metadata it references
        for(const auto& index_descriptor: table_info.indexes) {
            auto erase_status = erase_row(index_columns_tree, index_descriptor.index_id().id);
            if(!erase_status.ok()) {
                return handle_mutation_failure(erase_status, owns_transaction);
            }

            erase_status = erase_row(indexes_tree, index_descriptor.index_id().id);
            if(!erase_status.ok()) {
                return handle_mutation_failure(erase_status, owns_transaction);
            }
        }

        // Remove column metadata
        for(const auto& column_descriptor: table_info.columns) {
            auto erase_status = erase_row(columns_tree, column_descriptor.column_id().id);
            if(!erase_status.ok()) {
                return handle_mutation_failure(erase_status, owns_transaction);
            }
        }

        // Remove table metadata
        auto erase_status = erase_row(tables_tree, table_id.id);
        if(!erase_status.ok()) {
            return handle_mutation_failure(erase_status, owns_transaction);
        }

        // Remove the table from the next catalog state
        next_catalog_state.table_id_by_name_.erase(table_name);
        next_catalog_state.table_schema_by_id_.erase(table_id);
        next_catalog_state.table_by_id_.erase(table_id);

        // Keep the state staged for the caller's transaction
        if(!owns_transaction) {
            staged_state_ = std::move(next_catalog_state);
            return core::Status::Ok();
        }

        // Commit before updating the catalog state
        auto commit_status = pager_->commit_transaction();
        if(!commit_status.ok()) {
            return commit_status;
        }

        committed_state_ = std::move(next_catalog_state);
        return core::Status::Ok();

    }

    core::Status Catalog::on_transaction_committed() {

        if(pager_->in_transaction()) {
            return core::Status::TransactionError("Cannot publish catalog state: transaction is still active");
        }

        if(staged_state_.has_value()) {
            committed_state_ = std::move(staged_state_.value());
            staged_state_.reset();
        }

        return core::Status::Ok();

    }

    core::Status Catalog::on_transaction_rolled_back() {

        if(pager_->in_transaction()) {
            return core::Status::TransactionError("Cannot discard catalog state: transaction is still active");
        }

        staged_state_.reset();
        return core::Status::Ok();

    }

    const TableDescriptor* Catalog::find_table(std::string_view name) const {

        const auto& state = visible_state();
        const auto table_id_it = state.table_id_by_name_.find(std::string(name));
        if(table_id_it == state.table_id_by_name_.end()) return nullptr;

        return find_table(table_id_it->second);

    }

    const TableDescriptor* Catalog::find_table(TableId table_id) const {

        const auto& state = visible_state();
        const auto table_it = state.table_by_id_.find(table_id);
        if(table_it == state.table_by_id_.end()) return nullptr;

        return &table_it->second.table_descriptor;

    }

    const record::Schema* Catalog::schema_for_table(TableId table_id) const {

        const auto& state = visible_state();
        const auto schema_it = state.table_schema_by_id_.find(table_id);
        if(schema_it == state.table_schema_by_id_.end()) return nullptr;

        return &schema_it->second;

    }

    const ColumnDescriptor* Catalog::find_column(TableId table_id, std::string_view name) const {

        const auto& state = visible_state();
        const auto table_it = state.table_by_id_.find(table_id);
        if(table_it == state.table_by_id_.end()) return nullptr;

        for(const auto& column: table_it->second.columns) {
            if(column.name() == name) return &column;
        }

        return nullptr;

    }

    std::span<const IndexDescriptor> Catalog::indexes_for_table(TableId table_id) const {

        const auto& state = visible_state();
        const auto table_it = state.table_by_id_.find(table_id);
        if(table_it == state.table_by_id_.end()) return {};

        return table_it->second.indexes;
        
    }

    const Catalog::CatalogState& Catalog::visible_state() const {
        if(staged_state_.has_value()) return staged_state_.value();

        return committed_state_;
    }

    core::Status Catalog::handle_mutation_failure(core::Status failure_status, bool owns_transaction) {

        if(!owns_transaction) return failure_status;

        auto rollback_status = pager_->rollback_transaction();
        if(!rollback_status.ok()) return rollback_status;

        staged_state_.reset();
        return failure_status;

    }

}
