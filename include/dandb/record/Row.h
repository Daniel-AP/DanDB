#pragma once

#include <dandb/record/Value.h>

#include <vector>
#include <cstddef>

namespace dandb::record {

    class Row {
        public:
            explicit Row(std::vector<Value> values);

            std::size_t value_count() const;
            const std::vector<Value>& values() const;
            const Value& value(std::size_t ordinal) const;
            bool is_null(std::size_t ordinal) const;
        private:
            std::vector<Value> values_;
    };

}