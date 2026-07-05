#pragma once

#include <dandb/record/Column.h>
#include <dandb/core/Result.h>

#include <vector>
#include <cstddef>

namespace dandb::record {

    class Schema {
        public:
            static core::Result<Schema> create(std::vector<Column> columns);

            std::size_t column_count() const;
            const std::vector<Column>& columns() const;
            const Column& column(std::size_t ordinal) const;

            std::size_t primary_key_ordinal() const;
            const Column& primary_key_column() const;

            std::size_t row_size() const;
            std::size_t null_bitmap_size() const;

        private:
            Schema(
                std::vector<Column> columns,
                std::size_t primary_key_ordinal,
                std::size_t row_size,
                std::size_t null_bitmap_size
            );

            std::vector<Column> columns_;
            std::size_t primary_key_ordinal_;
            std::size_t row_size_;
            std::size_t null_bitmap_size_;
    };

}