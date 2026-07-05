#include <dandb/record/Row.h>

#include <utility>

namespace dandb::record {

    Row::Row(std::vector<Value> values) : values_(std::move(values)) {}

    std::size_t Row::value_count() const {
        return values_.size();
    }

    const std::vector<Value>& Row::values() const {
        return values_;
    }

    const Value& Row::value(std::size_t ordinal) const {
        return values_[ordinal];
    }

    bool Row::is_null(std::size_t ordinal) const {
        return value(ordinal).is_null();
    }

}
