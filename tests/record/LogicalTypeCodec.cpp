#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/record/LogicalType.h>
#include <dandb/record/LogicalTypeCodec.h>

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

using dandb::core::StatusCode;
using dandb::record::LogicalType;
using dandb::record::LogicalTypeCodec;

TEST_CASE("LogicalTypeCodec preserves logical type kind codes", "[record][logical-type-codec]") {
    const std::array kind_codes{
        std::pair{ LogicalType::Kind::Int8, std::uint8_t{ 1 } },
        std::pair{ LogicalType::Kind::Int16, std::uint8_t{ 2 } },
        std::pair{ LogicalType::Kind::Int32, std::uint8_t{ 3 } },
        std::pair{ LogicalType::Kind::Int64, std::uint8_t{ 4 } },
        std::pair{ LogicalType::Kind::Float64, std::uint8_t{ 5 } },
        std::pair{ LogicalType::Kind::String, std::uint8_t{ 6 } },
        std::pair{ LogicalType::Kind::Boolean, std::uint8_t{ 7 } }
    };

    for(const auto& [kind, code]: kind_codes) {
        REQUIRE(LogicalTypeCodec::encode_kind(kind) == code);

        auto decoded_kind_result = LogicalTypeCodec::decode_kind(code);

        REQUIRE(decoded_kind_result.ok());
        REQUIRE(decoded_kind_result.value() == kind);
    }
}

TEST_CASE("LogicalTypeCodec rejects unknown type codes", "[record][logical-type-codec]") {
    auto decoded_kind_result = LogicalTypeCodec::decode_kind(0);

    REQUIRE_FALSE(decoded_kind_result.ok());
    REQUIRE(decoded_kind_result.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("LogicalTypeCodec decodes complete non-string logical types", "[record][logical-type-codec]") {
    auto logical_type_result = LogicalTypeCodec::decode(4, std::nullopt);

    REQUIRE(logical_type_result.ok());
    REQUIRE(logical_type_result.value().kind() == LogicalType::Kind::Int64);
    REQUIRE_FALSE(logical_type_result.value().capacity().has_value());
}

TEST_CASE("LogicalTypeCodec decodes string logical types with capacity", "[record][logical-type-codec]") {
    auto logical_type_result = LogicalTypeCodec::decode(6, 256);

    REQUIRE(logical_type_result.ok());
    REQUIRE(logical_type_result.value().kind() == LogicalType::Kind::String);
    REQUIRE(logical_type_result.value().capacity() == 256);
}

TEST_CASE("LogicalTypeCodec requires capacity for string logical types", "[record][logical-type-codec]") {
    auto logical_type_result = LogicalTypeCodec::decode(6, std::nullopt);

    REQUIRE_FALSE(logical_type_result.ok());
    REQUIRE(logical_type_result.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("LogicalTypeCodec rejects capacity for non-string logical types", "[record][logical-type-codec]") {
    auto logical_type_result = LogicalTypeCodec::decode(4, 256);

    REQUIRE_FALSE(logical_type_result.ok());
    REQUIRE(logical_type_result.status().code() == StatusCode::InvalidArgument);
}
