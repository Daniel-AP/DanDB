#include <dandb/record/RowHelpers.h>

#include "RowValidation.h"

#include <dandb/core/Status.h>
#include <dandb/record/Column.h>
#include <dandb/record/Value.h>
#include <dandb/record/KeyCodec.h>

#include <string>
#include <utility>

namespace dandb::record {

    core::Result<Value> RowHelpers::value_by_ordinal(
        const Schema& schema,
        const Row& row,
        std::size_t ordinal
    ) {

        auto row_status = validate_row_against_schema(schema, row);
        if(!row_status.ok()) {
            return row_status;
        }

        if(ordinal >= schema.column_count()) {
            return core::Status::InvalidArgument("Cannot get value by ordinal: ordinal is out of bounds");
        }

        return row.value(ordinal);

    }

    core::Result<Value> RowHelpers::value_by_name(
        const Schema& schema,
        const Row& row,
        std::string_view name
    ) {

        auto row_status = validate_row_against_schema(schema, row);
        if(!row_status.ok()) {
            return row_status;
        }

        for(std::size_t i = 0; i < schema.column_count(); i++) {

            const Column& col = schema.column(i);
            if(col.name() != name) continue;

            return row.value(i);

        }

        return core::Status::NotFound("Cannot get value by name: no column with name "+std::string(name)+" exists");

    }

    core::Result<Row> RowHelpers::build_row(
        const Schema& schema,
        std::vector<Value> values
    ) {

        Row row(std::move(values));

        auto row_status = validate_row_against_schema(schema, row);
        if(!row_status.ok()) {
            return row_status;
        }

        return row;

    }

    core::Result<Row> RowHelpers::replace_non_primary_key_values(
        const Schema& schema,
        const Row& row,
        const std::vector<std::size_t>& ordinals,
        const std::vector<Value>& values
    ) {

        auto row_status = validate_row_against_schema(schema, row);
        if(!row_status.ok()) {
            return row_status;
        }

        if(ordinals.size() != values.size()) {
            return core::Status::InvalidArgument("Cannot replace row values: ordinals and values size differ");
        }

        for(std::size_t ord: ordinals) {
            if(ord == schema.primary_key_ordinal()) {
                return core::Status::InvalidArgument("Cannot replace row values: primary key value cannot be replaced");
            }
        }

        for(std::size_t ord: ordinals) {
            if(ord >= row.value_count()) {
                return core::Status::InvalidArgument("Cannot replace row values: ordinal is out of bounds");
            }
        }

        std::vector<Value> replaced_values = row.values();

        for(std::size_t i = 0; i < ordinals.size(); i++) {
            replaced_values[ordinals[i]] = values[i];
        }

        Row replaced_row(std::move(replaced_values));

        auto replaced_row_status = validate_row_against_schema(schema, replaced_row);
        if(!replaced_row_status.ok()) {
            return replaced_row_status;
        }

        return replaced_row;

    }

    core::Result<std::vector<std::byte>> RowHelpers::primary_key_bytes(
        const Schema& schema,
        const Row& row
    ) {
        return indexed_key_bytes(schema, row, schema.primary_key_ordinal());
    }

    core::Result<std::vector<std::byte>> RowHelpers::indexed_key_bytes(
        const Schema& schema,
        const Row& row,
        std::size_t ordinal
    ) {

        auto row_status = validate_row_against_schema(schema, row);
        if(!row_status.ok()) {
            return row_status;
        }

        if(ordinal >= schema.column_count()) {
            return core::Status::InvalidArgument("Cannot encode indexed key bytes: ordinal is out of bounds");
        }

        return KeyCodec::encode(row.value(ordinal));

    }

}
