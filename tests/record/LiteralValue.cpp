#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/record/LiteralValue.h>
#include <dandb/record/LogicalType.h>

#include <cstdint>
#include <limits>

using dandb::core::StatusCode;
using dandb::record::LiteralValue;
using dandb::record::LogicalType;

TEST_CASE("LiteralValue represents null literals", "[record][literal-value]") {
    const auto value = LiteralValue::null();

    REQUIRE(value.kind() == LiteralValue::Kind::Null);
    REQUIRE(value.is_null());
}

TEST_CASE("LiteralValue represents integer literals", "[record][literal-value]") {
    const auto value = LiteralValue::integer(42);

    REQUIRE(value.kind() == LiteralValue::Kind::Integer);
    REQUIRE_FALSE(value.is_null());
    REQUIRE(value.as_integer() == 42);
}

TEST_CASE("LiteralValue represents float literals", "[record][literal-value]") {
    const auto value = LiteralValue::real(3.5);

    REQUIRE(value.kind() == LiteralValue::Kind::Real);
    REQUIRE_FALSE(value.is_null());
    REQUIRE(value.as_real() == 3.5);
}

TEST_CASE("LiteralValue represents string literals", "[record][literal-value]") {
    const auto value = LiteralValue::string("dan");

    REQUIRE(value.kind() == LiteralValue::Kind::String);
    REQUIRE_FALSE(value.is_null());
    REQUIRE(value.as_string() == "dan");
}

TEST_CASE("LiteralValue represents boolean literals", "[record][literal-value]") {
    const auto value = LiteralValue::boolean(true);

    REQUIRE(value.kind() == LiteralValue::Kind::Boolean);
    REQUIRE_FALSE(value.is_null());
    REQUIRE(value.as_boolean());
}

TEST_CASE("LiteralValue converts integer literals to integer values", "[record][literal-value]") {
    const auto int8_value = LiteralValue::integer(42).convert_to(LogicalType::int8(), false);
    const auto int16_value = LiteralValue::integer(300).convert_to(LogicalType::int16(), false);
    const auto int32_value = LiteralValue::integer(70000).convert_to(LogicalType::int32(), false);
    const auto int64_value = LiteralValue::integer(5000000000).convert_to(LogicalType::int64(), false);

    REQUIRE(int8_value.ok());
    REQUIRE(int8_value.value().type().kind() == LogicalType::Kind::Int8);
    REQUIRE(int8_value.value().as_integer() == 42);

    REQUIRE(int16_value.ok());
    REQUIRE(int16_value.value().type().kind() == LogicalType::Kind::Int16);
    REQUIRE(int16_value.value().as_integer() == 300);

    REQUIRE(int32_value.ok());
    REQUIRE(int32_value.value().type().kind() == LogicalType::Kind::Int32);
    REQUIRE(int32_value.value().as_integer() == 70000);

    REQUIRE(int64_value.ok());
    REQUIRE(int64_value.value().type().kind() == LogicalType::Kind::Int64);
    REQUIRE(int64_value.value().as_integer() == 5000000000);
}

TEST_CASE("LiteralValue converts integer boundary literals to integer values", "[record][literal-value]") {
    const auto min_int8 = LiteralValue::integer(std::numeric_limits<std::int8_t>::min())
        .convert_to(LogicalType::int8(), false);
    const auto max_int8 = LiteralValue::integer(std::numeric_limits<std::int8_t>::max())
        .convert_to(LogicalType::int8(), false);
    const auto min_int16 = LiteralValue::integer(std::numeric_limits<std::int16_t>::min())
        .convert_to(LogicalType::int16(), false);
    const auto max_int16 = LiteralValue::integer(std::numeric_limits<std::int16_t>::max())
        .convert_to(LogicalType::int16(), false);
    const auto min_int32 = LiteralValue::integer(std::numeric_limits<std::int32_t>::min())
        .convert_to(LogicalType::int32(), false);
    const auto max_int32 = LiteralValue::integer(std::numeric_limits<std::int32_t>::max())
        .convert_to(LogicalType::int32(), false);
    const auto min_int64 = LiteralValue::integer(std::numeric_limits<std::int64_t>::min())
        .convert_to(LogicalType::int64(), false);
    const auto max_int64 = LiteralValue::integer(std::numeric_limits<std::int64_t>::max())
        .convert_to(LogicalType::int64(), false);

    REQUIRE(min_int8.ok());
    REQUIRE(min_int8.value().as_integer() == std::numeric_limits<std::int8_t>::min());
    REQUIRE(max_int8.ok());
    REQUIRE(max_int8.value().as_integer() == std::numeric_limits<std::int8_t>::max());

    REQUIRE(min_int16.ok());
    REQUIRE(min_int16.value().as_integer() == std::numeric_limits<std::int16_t>::min());
    REQUIRE(max_int16.ok());
    REQUIRE(max_int16.value().as_integer() == std::numeric_limits<std::int16_t>::max());

    REQUIRE(min_int32.ok());
    REQUIRE(min_int32.value().as_integer() == std::numeric_limits<std::int32_t>::min());
    REQUIRE(max_int32.ok());
    REQUIRE(max_int32.value().as_integer() == std::numeric_limits<std::int32_t>::max());

    REQUIRE(min_int64.ok());
    REQUIRE(min_int64.value().as_integer() == std::numeric_limits<std::int64_t>::min());
    REQUIRE(max_int64.ok());
    REQUIRE(max_int64.value().as_integer() == std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("LiteralValue rejects integer overflow during conversion", "[record][literal-value]") {
    const auto too_large_int8 = LiteralValue::integer(static_cast<std::int64_t>(std::numeric_limits<std::int8_t>::max()) + 1)
        .convert_to(LogicalType::int8(), false);
    const auto too_small_int8 = LiteralValue::integer(static_cast<std::int64_t>(std::numeric_limits<std::int8_t>::min()) - 1)
        .convert_to(LogicalType::int8(), false);
    const auto too_large_int16 = LiteralValue::integer(static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::max()) + 1)
        .convert_to(LogicalType::int16(), false);
    const auto too_small_int16 = LiteralValue::integer(static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::min()) - 1)
        .convert_to(LogicalType::int16(), false);
    const auto too_large_int32 = LiteralValue::integer(static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1)
        .convert_to(LogicalType::int32(), false);
    const auto too_small_int32 = LiteralValue::integer(static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) - 1)
        .convert_to(LogicalType::int32(), false);

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

TEST_CASE("LiteralValue converts real literals only to float64 values", "[record][literal-value]") {
    const auto value = LiteralValue::real(3.5).convert_to(LogicalType::float64(), false);
    const auto invalid = LiteralValue::real(3.5).convert_to(LogicalType::int64(), false);

    REQUIRE(value.ok());
    REQUIRE(value.value().type().kind() == LogicalType::Kind::Float64);
    REQUIRE(value.value().as_float64() == 3.5);

    REQUIRE_FALSE(invalid.ok());
    REQUIRE(invalid.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("LiteralValue converts string literals only when they fit the target capacity", "[record][literal-value]") {
    const auto string_type = LogicalType::string(4);

    REQUIRE(string_type.ok());

    const auto value = LiteralValue::string("abcd").convert_to(string_type.value(), false);
    const auto overflow = LiteralValue::string("abcde").convert_to(string_type.value(), false);
    const auto invalid = LiteralValue::string("dan").convert_to(LogicalType::int64(), false);

    REQUIRE(value.ok());
    REQUIRE(value.value().type().kind() == LogicalType::Kind::String);
    REQUIRE(value.value().as_string() == "abcd");

    REQUIRE_FALSE(overflow.ok());
    REQUIRE(overflow.status().code() == StatusCode::InvalidArgument);

    REQUIRE_FALSE(invalid.ok());
    REQUIRE(invalid.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("LiteralValue converts boolean literals only to boolean values", "[record][literal-value]") {
    const auto value = LiteralValue::boolean(true).convert_to(LogicalType::boolean(), false);
    const auto invalid = LiteralValue::integer(1).convert_to(LogicalType::boolean(), false);

    REQUIRE(value.ok());
    REQUIRE(value.value().type().kind() == LogicalType::Kind::Boolean);
    REQUIRE(value.value().as_boolean());

    REQUIRE_FALSE(invalid.ok());
    REQUIRE(invalid.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("LiteralValue converts null only when nullable is allowed", "[record][literal-value]") {
    const auto allowed = LiteralValue::null().convert_to(LogicalType::int64(), true);
    const auto rejected = LiteralValue::null().convert_to(LogicalType::int64(), false);

    REQUIRE(allowed.ok());
    REQUIRE(allowed.value().is_null());
    REQUIRE(allowed.value().type().kind() == LogicalType::Kind::Int64);

    REQUIRE_FALSE(rejected.ok());
    REQUIRE(rejected.status().code() == StatusCode::ConstraintViolation);
}
