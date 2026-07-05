#include <dandb/record/LiteralValue.h>

#include <utility>

namespace dandb::record {

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

    LiteralValue::LiteralValue(Kind kind, Payload payload) :
        kind_(kind),
        payload_(std::move(payload))
    {}

}
