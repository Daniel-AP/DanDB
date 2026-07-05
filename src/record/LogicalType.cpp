#include <dandb/record/LogicalType.h>

#include <cstdint>
#include <limits>
#include <string>

namespace dandb::record {

    static_assert(sizeof(double) == 8, "DanDB DOUBLE requires an 8-byte double");
    static_assert(std::numeric_limits<double>::is_iec559, "DanDB DOUBLE requires an IEEE 754 double");
    static_assert(sizeof(bool) == 1, "DanDB BOOL requires a 1-byte bool");

    LogicalType LogicalType::int8() {
        return LogicalType(Kind::Int8, std::nullopt);
    }

    LogicalType LogicalType::int16() {
        return LogicalType(Kind::Int16, std::nullopt);
    }

    LogicalType LogicalType::int32() {
        return LogicalType(Kind::Int32, std::nullopt);
    }

    LogicalType LogicalType::int64() {
        return LogicalType(Kind::Int64, std::nullopt);
    }

    LogicalType LogicalType::float64() {
        return LogicalType(Kind::Float64, std::nullopt);
    }

    core::Result<LogicalType> LogicalType::string(std::size_t capacity) {
        if(capacity == 0) {
            return core::Status::InvalidArgument(std::string(STRING_DISPLAY_NAME)+" capacity must be greater than 0");
        }

        return LogicalType(Kind::String, capacity);
    }

    LogicalType LogicalType::boolean() {
        return LogicalType(Kind::Boolean, std::nullopt);
    }

    LogicalType::Kind LogicalType::kind() const {
        return kind_;
    }

    std::size_t LogicalType::fixed_size() const {
        switch(kind_) {
            case Kind::Int8:
                return sizeof(std::int8_t);
            case Kind::Int16:
                return sizeof(std::int16_t);
            case Kind::Int32:
                return sizeof(std::int32_t);
            case Kind::Int64:
                return sizeof(std::int64_t);
            case Kind::Float64:
                return sizeof(double);
            case Kind::String:
                return capacity_.value();
            case Kind::Boolean:
                return sizeof(bool);
        }

        return 0;
    }

    bool LogicalType::can_be_indexed() const {
        switch(kind_) {
            case Kind::Int8:
            case Kind::Int16:
            case Kind::Int32:
            case Kind::Int64:
            case Kind::String:
            case Kind::Boolean:
                return true;
            case Kind::Float64:
                return false;
        }

        return false;
    }

    std::string LogicalType::display_name() const {
        switch(kind_) {
            case Kind::Int8:
                return INT8_DISPLAY_NAME;
            case Kind::Int16:
                return INT16_DISPLAY_NAME;
            case Kind::Int32:
                return INT32_DISPLAY_NAME;
            case Kind::Int64:
                return INT64_DISPLAY_NAME;
            case Kind::Float64:
                return FLOAT64_DISPLAY_NAME;
            case Kind::String:
                return std::string(STRING_DISPLAY_NAME)+"("+std::to_string(capacity_.value())+")";
            case Kind::Boolean:
                return BOOLEAN_DISPLAY_NAME;
        }

        return "UNKNOWN";
    }

    std::optional<std::size_t> LogicalType::capacity() const {
        return capacity_;
    }

    LogicalType::LogicalType(Kind kind, std::optional<std::size_t> capacity) :
        kind_(kind),
        capacity_(capacity)
    {}

}
