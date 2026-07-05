#pragma once

#include <dandb/core/Result.h>

#include <cstddef>
#include <optional>
#include <string>

namespace dandb::record {

    inline constexpr const char* INT8_DISPLAY_NAME = "INT8";
    inline constexpr const char* INT16_DISPLAY_NAME = "INT16";
    inline constexpr const char* INT32_DISPLAY_NAME = "INT32";
    inline constexpr const char* INT64_DISPLAY_NAME = "INT64";
    inline constexpr const char* FLOAT64_DISPLAY_NAME = "DOUBLE";
    inline constexpr const char* STRING_DISPLAY_NAME = "STRING";
    inline constexpr const char* BOOLEAN_DISPLAY_NAME = "BOOL";

    class LogicalType {
        public:
            enum class Kind {
                Int8,
                Int16,
                Int32,
                Int64,
                Float64,
                String,
                Boolean
            };

            static LogicalType int8();
            static LogicalType int16();
            static LogicalType int32();
            static LogicalType int64();
            static LogicalType float64();
            static core::Result<LogicalType> string(std::size_t capacity);
            static LogicalType boolean();

            Kind kind() const;
            std::size_t fixed_size() const;
            bool can_be_indexed() const;
            std::string display_name() const;
            std::optional<std::size_t> capacity() const;

        private:
            LogicalType(Kind kind, std::optional<std::size_t> capacity);

            Kind kind_;
            std::optional<std::size_t> capacity_;
    };

}
