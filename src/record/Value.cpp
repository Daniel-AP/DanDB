#include <dandb/record/Value.h>

#include <dandb/core/Status.h>

#include <limits>
#include <string>
#include <utility>

namespace dandb::record {
    namespace {

        template<typename Target>
        bool fits_integer(std::int64_t value) {
            return value >= static_cast<std::int64_t>(std::numeric_limits<Target>::min())
                && value <= static_cast<std::int64_t>(std::numeric_limits<Target>::max());
        }

        core::Status integer_out_of_range(const char* type_name, std::int64_t value) {
            return core::Status::InvalidArgument(std::string(type_name)+" value out of range: "+std::to_string(value));
        }

    }

    core::Result<Value> Value::int8(std::int64_t value) {
        if(!fits_integer<std::int8_t>(value)) {
            return integer_out_of_range(INT8_DISPLAY_NAME, value);
        }

        return Value(LogicalType::int8(), value);
    }

    core::Result<Value> Value::int16(std::int64_t value) {
        if(!fits_integer<std::int16_t>(value)) {
            return integer_out_of_range(INT16_DISPLAY_NAME, value);
        }

        return Value(LogicalType::int16(), value);
    }

    core::Result<Value> Value::int32(std::int64_t value) {
        if(!fits_integer<std::int32_t>(value)) {
            return integer_out_of_range(INT32_DISPLAY_NAME, value);
        }

        return Value(LogicalType::int32(), value);
    }

    Value Value::int64(std::int64_t value) {
        return Value(LogicalType::int64(), value);
    }

    Value Value::float64(double value) {
        return Value(LogicalType::float64(), value);
    }

    core::Result<Value> Value::string(std::string value, std::size_t capacity) {
        auto type = LogicalType::string(capacity);
        if(!type.ok()) {
            return type.status();
        }

        if(value.size() > capacity) {
            return core::Status::InvalidArgument("STRING("+std::to_string(capacity)+") value length exceeds capacity");
        }

        return Value(type.value(), std::move(value));
    }

    Value Value::boolean(bool value) {
        return Value(LogicalType::boolean(), value);
    }

    Value Value::null(LogicalType type) {
        return Value(type, std::monostate{});
    }

    LogicalType Value::type() const {
        return type_;
    }

    bool Value::is_null() const {
        return std::holds_alternative<std::monostate>(payload_);
    }

    std::int64_t Value::as_integer() const {
        return std::get<std::int64_t>(payload_);
    }

    double Value::as_float64() const {
        return std::get<double>(payload_);
    }

    const std::string& Value::as_string() const {
        return std::get<std::string>(payload_);
    }

    bool Value::as_boolean() const {
        return std::get<bool>(payload_);
    }

    Value::Value(LogicalType type, Payload payload) :
        type_(type),
        payload_(std::move(payload))
    {}

}
