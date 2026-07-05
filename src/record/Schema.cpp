#include <dandb/record/Schema.h>

#include <dandb/core/Status.h>

#include <utility>
#include <unordered_set>
#include <string>

namespace dandb::record {

    Schema::Schema(
        std::vector<Column> columns,
        std::size_t primary_key_ordinal,
        std::size_t row_size,
        std::size_t null_bitmap_size
    ) :
        columns_(std::move(columns)),
        primary_key_ordinal_(primary_key_ordinal),
        row_size_(row_size),
        null_bitmap_size_(null_bitmap_size)
    {}

    core::Result<Schema> Schema::create(std::vector<Column> columns) {

        if(columns.empty()) {
            return core::Status::InvalidArgument("Cannot create schema: schema must have at least one column");
        }

        std::unordered_set<std::string> seen_names;

        for(const auto& col: columns) {
            if(seen_names.contains(col.name())) {
                return core::Status::InvalidArgument("Cannot create schema: duplicate column name");
            }
            seen_names.insert(col.name());
        }

        std::size_t primary_key_ordinal;
        std::size_t primary_key_count = 0;

        for(std::size_t i = 0; i < columns.size(); i++) {

            const Column& col = columns[i];
            if(col.pk() && primary_key_count > 0) {
                return core::Status::InvalidArgument("Cannot create schema: schema must have at most one primary key");
            }

            if(col.pk()) {
                primary_key_ordinal = i;
                primary_key_count++;
            }

        }

        if(primary_key_count == 0) {
            return core::Status::InvalidArgument("Cannot create schema: schema must have exactly one primary key");
        }

        std::size_t null_bitmap_size = (columns.size()+7)/8;
        std::size_t row_size = null_bitmap_size;

        for(std::size_t i = 0; i < columns.size(); i++) {

            Column& col = columns[i];

            col.set_layout(i, row_size);
            row_size += col.logical_type().fixed_size();

        }

        return Schema{
            std::move(columns),
            primary_key_ordinal,
            row_size,
            null_bitmap_size
        };

    }

    std::size_t Schema::column_count() const {
        return columns_.size();
    }

    const std::vector<Column>& Schema::columns() const {
        return columns_;
    }

    const Column& Schema::column(std::size_t ordinal) const {
        return columns_[ordinal];
    }

    std::size_t Schema::primary_key_ordinal() const {
        return primary_key_ordinal_;
    }

    const Column& Schema::primary_key_column() const {
        return columns_[primary_key_ordinal_];
    }

    std::size_t Schema::row_size() const {
        return row_size_;
    }

    std::size_t Schema::null_bitmap_size() const {
        return null_bitmap_size_;
    }

}
