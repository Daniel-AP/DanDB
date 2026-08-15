#pragma once

#include <dandb/core/Status.h>
#include <dandb/record/Row.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace dandb::execution {

    struct ExecutionResult {
        struct RowSet {
            std::vector<std::string> column_names;
            std::vector<record::Row> rows;
        };

        core::Status status;
        std::optional<std::string> success_message;
        std::optional<RowSet> row_set;
        std::optional<std::size_t> rows_affected;
    };

}
