#include <dandb/catalog/SystemTables.h>

#include <dandb/core/Status.h>
#include <dandb/record/Column.h>
#include <dandb/record/LogicalType.h>

#include <string>
#include <utility>
#include <vector>

namespace dandb::catalog {

    namespace {

        core::Status append_column(
            std::vector<record::Column>& columns,
            std::string name,
            record::LogicalType logical_type,
            bool nullable,
            bool pk,
            bool unique
        ) {

            auto column = record::Column::create(
                std::move(name),
                logical_type,
                nullable,
                pk,
                unique
            );

            if(!column.ok()) {
                return column.status();
            }

            columns.push_back(std::move(column.value()));

            return core::Status::Ok();

        }

    }

    core::Result<record::Schema> SystemTables::tables_schema() {

        std::vector<record::Column> columns;

        auto status = append_column(columns, "table_id", record::LogicalType::int64(), false, true, true);
        if(!status.ok()) {
            return status;
        }

        auto name_type_result = record::LogicalType::string(CATALOG_NAME_CAPACITY);
        if(!name_type_result.ok()) {
            return name_type_result.status();
        }

        status = append_column(columns, "name", name_type_result.value(), false, false, true);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "root_page_id", record::LogicalType::int64(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        return record::Schema::create(std::move(columns));

    }

    core::Result<record::Schema> SystemTables::columns_schema() {

        std::vector<record::Column> columns;

        auto status = append_column(columns, "column_id", record::LogicalType::int64(), false, true, true);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "table_id", record::LogicalType::int64(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        auto name_type_result = record::LogicalType::string(CATALOG_NAME_CAPACITY);
        if(!name_type_result.ok()) {
            return name_type_result.status();
        }

        status = append_column(columns, "name", name_type_result.value(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "type_kind", record::LogicalType::int8(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "type_capacity", record::LogicalType::int64(), true, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "ordinal", record::LogicalType::int64(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "nullable", record::LogicalType::boolean(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "primary_key", record::LogicalType::boolean(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "unique", record::LogicalType::boolean(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        return record::Schema::create(std::move(columns));

    }

    core::Result<record::Schema> SystemTables::indexes_schema() {

        std::vector<record::Column> columns;

        auto status = append_column(columns, "index_id", record::LogicalType::int64(), false, true, true);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "table_id", record::LogicalType::int64(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        auto name_type_result = record::LogicalType::string(CATALOG_NAME_CAPACITY);
        if(!name_type_result.ok()) {
            return name_type_result.status();
        }

        status = append_column(columns, "name", name_type_result.value(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "root_page_id", record::LogicalType::int64(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "unique", record::LogicalType::boolean(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "primary", record::LogicalType::boolean(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "internal", record::LogicalType::boolean(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        return record::Schema::create(std::move(columns));

    }

    core::Result<record::Schema> SystemTables::index_columns_schema() {

        std::vector<record::Column> columns;

        // Each index maps to exactly one column (no composite indexes yet), so index_id is enough as the row pk
        auto status = append_column(columns, "index_id", record::LogicalType::int64(), false, true, true);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "column_id", record::LogicalType::int64(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        status = append_column(columns, "ordinal", record::LogicalType::int64(), false, false, false);
        if(!status.ok()) {
            return status;
        }

        return record::Schema::create(std::move(columns));
        
    }

}
