#pragma once

#include <dandb/core/Result.h>
#include <dandb/record/Value.h>
#include <dandb/record/Schema.h>
#include <dandb/record/Row.h>

#include <cstddef>
#include <vector>
#include <string_view>

namespace dandb::record {

    class RowHelpers {
        public:
            static core::Result<Value> value_by_ordinal(
                const Schema& schema,
                const Row& row,
                std::size_t ordinal
            );

            static core::Result<Value> value_by_name(
                const Schema& schema,
                const Row& row,
                std::string_view name
            );

            static core::Result<Row> build_row(
                const Schema& schema,
                std::vector<Value> values
            );

            static core::Result<Row> replace_non_primary_key_values(
                const Schema& schema,
                const Row& row,
                const std::vector<std::size_t>& ordinals,
                const std::vector<Value>& values
            );

            static core::Result<std::vector<std::byte>> primary_key_bytes(
                const Schema& schema,
                const Row& row
            );

            static core::Result<std::vector<std::byte>> indexed_key_bytes(
                const Schema& schema,
                const Row& row,
                std::size_t ordinal
            );
    };

}