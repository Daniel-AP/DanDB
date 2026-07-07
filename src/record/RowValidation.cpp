#include "RowValidation.h"

#include <dandb/record/LogicalType.h>

#include <string>

namespace dandb::record {

    core::Status validate_row_against_schema(const Schema& schema, const Row& row) {

        if(schema.column_count() != row.value_count()) {
            return core::Status::InvalidArgument("Cannot validate row: row value count does not match schema column count");
        }

        for(std::size_t i = 0; i < schema.column_count(); i++) {

            const auto& col = schema.column(i);
            const auto& val = row.value(i);

            if(col.logical_type().kind() != val.type().kind()) {
                return core::Status::InvalidArgument("Cannot validate row: row value type does not match schema column type");
            }

            if(val.type().kind() == LogicalType::Kind::String && col.logical_type().capacity() != val.type().capacity()) {
                return core::Status::InvalidArgument("Cannot validate row: row value type string capacity does not match schema column type string capacity");
            }

            if(val.is_null()) {
                if(!col.nullable()) {
                    return core::Status::InvalidArgument("Cannot validate row: row value is null but column is not nullable");
                }
                continue;
            }

            if(col.logical_type().kind() != LogicalType::Kind::String) {
                continue;
            }

            if(val.as_string().size() > *col.logical_type().capacity()) {
                return core::Status::InvalidArgument("Cannot validate row: row value string length surpasses allowed capacity by column");
            }

            if(val.as_string().find('\0') != std::string::npos) {
                return core::Status::InvalidArgument("Cannot validate row: string value cannot contain a null character");
            }

        }

        return core::Status::Ok();

    }

}
