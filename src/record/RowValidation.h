#pragma once

#include <dandb/core/Status.h>
#include <dandb/record/Row.h>
#include <dandb/record/Schema.h>

namespace dandb::record {

    core::Status validate_row_against_schema(const Schema& schema, const Row& row);

}
