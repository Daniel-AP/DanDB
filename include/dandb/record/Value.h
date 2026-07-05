#pragma once

#include <dandb/core/Result.h>
#include <dandb/record/LogicalType.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace dandb::record {

    class Value {
        public:
            static core::Result<Value> int8(std::int64_t value);
            static core::Result<Value> int16(std::int64_t value);
            static core::Result<Value> int32(std::int64_t value);
            static Value int64(std::int64_t value);
            static Value float64(double value);
            static core::Result<Value> string(std::string value, std::size_t capacity);
            static Value boolean(bool value);
            static Value null(LogicalType type);

            LogicalType type() const;
            bool is_null() const;

            std::int64_t as_integer() const;
            double as_float64() const;
            const std::string& as_string() const;
            bool as_boolean() const;

        private:
            using Payload = std::variant<
                std::monostate,
                std::int64_t,
                double,
                std::string,
                bool
            >;

            Value(LogicalType type, Payload payload);

            LogicalType type_;
            Payload payload_;
    };

}
