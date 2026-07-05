#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/record/LogicalType.h>

using dandb::core::StatusCode;
using dandb::record::LogicalType;

TEST_CASE("LogicalType factories expose supported type kinds", "[record][logical-type]") {
    REQUIRE(LogicalType::int8().kind() == LogicalType::Kind::Int8);
    REQUIRE(LogicalType::int16().kind() == LogicalType::Kind::Int16);
    REQUIRE(LogicalType::int32().kind() == LogicalType::Kind::Int32);
    REQUIRE(LogicalType::int64().kind() == LogicalType::Kind::Int64);
    REQUIRE(LogicalType::float64().kind() == LogicalType::Kind::Float64);
    REQUIRE(LogicalType::boolean().kind() == LogicalType::Kind::Boolean);

    const auto string_type_result = LogicalType::string(64);

    REQUIRE(string_type_result.ok());
    REQUIRE(string_type_result.value().kind() == LogicalType::Kind::String);
}

TEST_CASE("LogicalType stores capacity only for strings", "[record][logical-type]") {
    REQUIRE_FALSE(LogicalType::int8().capacity().has_value());
    REQUIRE_FALSE(LogicalType::int16().capacity().has_value());
    REQUIRE_FALSE(LogicalType::int32().capacity().has_value());
    REQUIRE_FALSE(LogicalType::int64().capacity().has_value());
    REQUIRE_FALSE(LogicalType::float64().capacity().has_value());
    REQUIRE_FALSE(LogicalType::boolean().capacity().has_value());

    const auto string_type_result = LogicalType::string(64);

    REQUIRE(string_type_result.ok());
    REQUIRE(string_type_result.value().capacity().has_value());
    REQUIRE(string_type_result.value().capacity().value() == 64);
}

TEST_CASE("LogicalType reports fixed storage size for each supported type", "[record][logical-type]") {
    REQUIRE(LogicalType::int8().fixed_size() == 1);
    REQUIRE(LogicalType::int16().fixed_size() == 2);
    REQUIRE(LogicalType::int32().fixed_size() == 4);
    REQUIRE(LogicalType::int64().fixed_size() == 8);
    REQUIRE(LogicalType::float64().fixed_size() == 8);
    REQUIRE(LogicalType::boolean().fixed_size() == 1);

    const auto string_type_result = LogicalType::string(64);

    REQUIRE(string_type_result.ok());
    REQUIRE(string_type_result.value().fixed_size() == 64);
}

TEST_CASE("LogicalType rejects zero string capacity", "[record][logical-type]") {
    const auto string_type_result = LogicalType::string(0);

    REQUIRE_FALSE(string_type_result.ok());
    REQUIRE(string_type_result.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("LogicalType reports indexability rules", "[record][logical-type]") {
    REQUIRE(LogicalType::int8().can_be_indexed());
    REQUIRE(LogicalType::int16().can_be_indexed());
    REQUIRE(LogicalType::int32().can_be_indexed());
    REQUIRE(LogicalType::int64().can_be_indexed());
    REQUIRE(LogicalType::boolean().can_be_indexed());

    const auto string_type_result = LogicalType::string(64);

    REQUIRE(string_type_result.ok());
    REQUIRE(string_type_result.value().can_be_indexed());
    REQUIRE_FALSE(LogicalType::float64().can_be_indexed());
}

TEST_CASE("LogicalType display names use SQL spelling", "[record][logical-type]") {
    REQUIRE(LogicalType::int8().display_name() == "INT8");
    REQUIRE(LogicalType::int16().display_name() == "INT16");
    REQUIRE(LogicalType::int32().display_name() == "INT32");
    REQUIRE(LogicalType::int64().display_name() == "INT64");
    REQUIRE(LogicalType::float64().display_name() == "DOUBLE");
    REQUIRE(LogicalType::boolean().display_name() == "BOOL");

    const auto string_type_result = LogicalType::string(64);

    REQUIRE(string_type_result.ok());
    REQUIRE(string_type_result.value().display_name() == "STRING(64)");
}
