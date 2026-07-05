#include <dandb/record/LiteralValue.h>

#include <dandb/core/Status.h>

#include <string>
#include <utility>

namespace dandb::record {
    
    namespace {

        core::Status incompatible_literal(std::string literal_kind, const LogicalType& type) {
            return core::Status::InvalidArgument(
                std::move(literal_kind)+" literal cannot be converted to "+type.display_name()
            );
        }

        core::Result<Value> convert_integer_to(std::int64_t value, LogicalType type) {
            switch(type.kind()) {
                case LogicalType::Kind::Int8:
                    return Value::int8(value);
                case LogicalType::Kind::Int16:
                    return Value::int16(value);
                case LogicalType::Kind::Int32:
                    return Value::int32(value);
                case LogicalType::Kind::Int64:
                    return Value::int64(value);
                case LogicalType::Kind::Float64:
                case LogicalType::Kind::String:
                case LogicalType::Kind::Boolean:
                    return incompatible_literal("integer", type);
            }

            return core::Status::InternalError("Unhandled logical type while converting integer literal");
        }

        core::Result<Value> convert_real_to(double value, LogicalType type) {
            if(type.kind() == LogicalType::Kind::Float64) {
                return Value::float64(value);
            }

            return incompatible_literal("real", type);
        }

        core::Result<Value> convert_string_to(const std::string& value, LogicalType type) {
            if(type.kind() != LogicalType::Kind::String) {
                return incompatible_literal("string", type);
            }

            return Value::string(value, type.capacity().value());
        }

        core::Result<Value> convert_boolean_to(bool value, LogicalType type) {
            if(type.kind() == LogicalType::Kind::Boolean) {
                return Value::boolean(value);
            }

            return incompatible_literal("boolean", type);
        }

        core::Result<Value> convert_null_to(LogicalType type, bool nullable) {
            if(!nullable) {
                return core::Status::ConstraintViolation("NULL is not allowed for non-nullable columns");
            }

            return Value::null(type);
        }

    }

    LiteralValue LiteralValue::null() {
        return LiteralValue(Kind::Null, std::monostate{});
    }

    LiteralValue LiteralValue::integer(std::int64_t value) {
        return LiteralValue(Kind::Integer, value);
    }

    LiteralValue LiteralValue::real(double value) {
        return LiteralValue(Kind::Real, value);
    }

    LiteralValue LiteralValue::string(std::string value) {
        return LiteralValue(Kind::String, std::move(value));
    }

    LiteralValue LiteralValue::boolean(bool value) {
        return LiteralValue(Kind::Boolean, value);
    }

    LiteralValue::Kind LiteralValue::kind() const {
        return kind_;
    }

    bool LiteralValue::is_null() const {
        return kind_ == Kind::Null;
    }

    std::int64_t LiteralValue::as_integer() const {
        return std::get<std::int64_t>(payload_);
    }

    double LiteralValue::as_real() const {
        return std::get<double>(payload_);
    }

    const std::string& LiteralValue::as_string() const {
        return std::get<std::string>(payload_);
    }

    bool LiteralValue::as_boolean() const {
        return std::get<bool>(payload_);
    }

    core::Result<Value> LiteralValue::convert_to(LogicalType type, bool nullable) const {
        switch(kind_) {
            case Kind::Null:
                return convert_null_to(type, nullable);
            case Kind::Integer:
                return convert_integer_to(as_integer(), type);
            case Kind::Real:
                return convert_real_to(as_real(), type);
            case Kind::String:
                return convert_string_to(as_string(), type);
            case Kind::Boolean:
                return convert_boolean_to(as_boolean(), type);
        }

        return core::Status::InternalError("Unhandled literal value kind");
    }

    LiteralValue::LiteralValue(Kind kind, Payload payload) :
        kind_(kind),
        payload_(std::move(payload))
    {}

}
