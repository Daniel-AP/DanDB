#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/record/Value.h>

#include <cstdint>
#include <limits>

using dandb::core::StatusCode;
using dandb::record::LogicalType;
using dandb::record::Value;

TEST_CASE("Value stores typed integer values", "[record][value]") {
    const auto int8_value = Value::int8(42);
    const auto int16_value = Value::int16(300);
    const auto int32_value = Value::int32(70000);
    const auto int64_value = Value::int64(5000000000);

    REQUIRE(int8_value.ok());
    REQUIRE(int8_value.value().type().kind() == LogicalType::Kind::Int8);
    REQUIRE(int8_value.value().as_integer() == 42);

    REQUIRE(int16_value.ok());
    REQUIRE(int16_value.value().type().kind() == LogicalType::Kind::Int16);
    REQUIRE(int16_value.value().as_integer() == 300);

    REQUIRE(int32_value.ok());
    REQUIRE(int32_value.value().type().kind() == LogicalType::Kind::Int32);
    REQUIRE(int32_value.value().as_integer() == 70000);

    REQUIRE(int64_value.type().kind() == LogicalType::Kind::Int64);
    REQUIRE(int64_value.as_integer() == 5000000000);
}

TEST_CASE("Value rejects integer values outside their logical type range", "[record][value]") {
    const auto too_large_int8 = Value::int8(static_cast<std::int64_t>(std::numeric_limits<std::int8_t>::max()) + 1);
    const auto too_small_int8 = Value::int8(static_cast<std::int64_t>(std::numeric_limits<std::int8_t>::min()) - 1);
    const auto too_large_int16 = Value::int16(static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::max()) + 1);
    const auto too_small_int16 = Value::int16(static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::min()) - 1);
    const auto too_large_int32 = Value::int32(static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1);
    const auto too_small_int32 = Value::int32(static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) - 1);

    REQUIRE_FALSE(too_large_int8.ok());
    REQUIRE(too_large_int8.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(too_small_int8.ok());
    REQUIRE(too_small_int8.status().code() == StatusCode::InvalidArgument);

    REQUIRE_FALSE(too_large_int16.ok());
    REQUIRE(too_large_int16.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(too_small_int16.ok());
    REQUIRE(too_small_int16.status().code() == StatusCode::InvalidArgument);

    REQUIRE_FALSE(too_large_int32.ok());
    REQUIRE(too_large_int32.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(too_small_int32.ok());
    REQUIRE(too_small_int32.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("Value stores float64 values", "[record][value]") {
    const auto value = Value::float64(3.5);

    REQUIRE(value.type().kind() == LogicalType::Kind::Float64);
    REQUIRE(value.as_float64() == 3.5);
}

TEST_CASE("Value stores string values with capacity", "[record][value]") {
    const auto value = Value::string("dan", 8);

    REQUIRE(value.ok());
    REQUIRE(value.value().type().kind() == LogicalType::Kind::String);
    REQUIRE(value.value().type().capacity().has_value());
    REQUIRE(value.value().type().capacity().value() == 8);
    REQUIRE(value.value().as_string() == "dan");
}

TEST_CASE("Value rejects string values longer than capacity", "[record][value]") {
    const auto value = Value::string("daniel", 3);

    REQUIRE_FALSE(value.ok());
    REQUIRE(value.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("Value stores boolean values", "[record][value]") {
    const auto value = Value::boolean(true);

    REQUIRE(value.type().kind() == LogicalType::Kind::Boolean);
    REQUIRE(value.as_boolean());
}

TEST_CASE("Value stores null with its target logical type", "[record][value]") {
    const auto value = Value::null(LogicalType::int64());

    REQUIRE(value.is_null());
    REQUIRE(value.type().kind() == LogicalType::Kind::Int64);
}
