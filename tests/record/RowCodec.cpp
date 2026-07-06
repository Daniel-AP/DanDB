#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/record/Column.h>
#include <dandb/record/LogicalType.h>
#include <dandb/record/Row.h>
#include <dandb/record/RowCodec.h>
#include <dandb/record/Schema.h>
#include <dandb/record/Value.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using dandb::core::StatusCode;
using dandb::record::Column;
using dandb::record::LogicalType;
using dandb::record::Row;
using dandb::record::RowCodec;
using dandb::record::Schema;
using dandb::record::Value;

namespace {

    Column make_column(
        std::string name,
        LogicalType logical_type,
        bool nullable,
        bool pk,
        bool unique
    ) {
        auto column = Column::create(
            std::move(name),
            logical_type,
            nullable,
            pk,
            unique
        );

        REQUIRE(column.ok());
        return std::move(column.value());
    }

    Schema make_string_schema() {
        auto name_type = LogicalType::string(4);
        REQUIRE(name_type.ok());

        auto schema = Schema::create({
            make_column("id", LogicalType::int64(), false, true, true),
            make_column("name", name_type.value(), true, false, false)
        });

        REQUIRE(schema.ok());
        return std::move(schema.value());
    }

    Schema make_all_types_schema() {
        auto name_type = LogicalType::string(5);
        REQUIRE(name_type.ok());

        auto schema = Schema::create({
            make_column("tiny", LogicalType::int8(), false, true, true),
            make_column("small", LogicalType::int16(), false, false, false),
            make_column("medium", LogicalType::int32(), false, false, false),
            make_column("big", LogicalType::int64(), false, false, false),
            make_column("score", LogicalType::float64(), false, false, false),
            make_column("active", LogicalType::boolean(), false, false, false),
            make_column("name", name_type.value(), false, false, false)
        });

        REQUIRE(schema.ok());
        return std::move(schema.value());
    }

}

TEST_CASE("RowCodec encodes and decodes all supported types in the documented fixed layout", "[record][row-codec]") {
    auto schema = make_all_types_schema();

    auto tiny = Value::int8(-5);
    auto small = Value::int16(-1234);
    auto medium = Value::int32(-123456);
    auto name = Value::string("dan", 5);

    REQUIRE(tiny.ok());
    REQUIRE(small.ok());
    REQUIRE(medium.ok());
    REQUIRE(name.ok());

    Row row(std::vector<Value>{
        tiny.value(),
        small.value(),
        medium.value(),
        Value::int64(0x0102030405060708LL),
        Value::float64(1.5),
        Value::boolean(true),
        name.value()
    });

    auto encoded = RowCodec::encode(schema, row);

    REQUIRE(encoded.ok());
    REQUIRE(encoded.value().size() == schema.row_size());
    REQUIRE(encoded.value() == std::vector<std::byte>{
        std::byte{ 0x00 },
        std::byte{ 0xFB },
        std::byte{ 0x2E },
        std::byte{ 0xFB },
        std::byte{ 0xC0 },
        std::byte{ 0x1D },
        std::byte{ 0xFE },
        std::byte{ 0xFF },
        std::byte{ 0x08 },
        std::byte{ 0x07 },
        std::byte{ 0x06 },
        std::byte{ 0x05 },
        std::byte{ 0x04 },
        std::byte{ 0x03 },
        std::byte{ 0x02 },
        std::byte{ 0x01 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0xF8 },
        std::byte{ 0x3F },
        std::byte{ 0x01 },
        std::byte{ 0x64 },
        std::byte{ 0x61 },
        std::byte{ 0x6E },
        std::byte{ 0x00 },
        std::byte{ 0x00 }
    });

    auto decoded = RowCodec::decode(schema, encoded.value());

    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().value(0).as_integer() == -5);
    REQUIRE(decoded.value().value(1).as_integer() == -1234);
    REQUIRE(decoded.value().value(2).as_integer() == -123456);
    REQUIRE(decoded.value().value(3).as_integer() == 0x0102030405060708LL);
    REQUIRE(decoded.value().value(4).as_float64() == 1.5);
    REQUIRE(decoded.value().value(5).as_boolean());
    REQUIRE(decoded.value().value(6).as_string() == "dan");
}

TEST_CASE("RowCodec decodes real null bitmap bits without rejecting unused bits that are zero", "[record][row-codec]") {
    auto schema = make_string_schema();

    Row row(std::vector<Value>{
        Value::int64(7),
        Value::null(schema.column(1).logical_type())
    });

    auto encoded = RowCodec::encode(schema, row);
    REQUIRE(encoded.ok());
    REQUIRE(encoded.value()[0] == std::byte{ 0x02 });

    auto decoded = RowCodec::decode(schema, encoded.value());

    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().value(0).as_integer() == 7);
    REQUIRE(decoded.value().value(1).is_null());
}

TEST_CASE("RowCodec decode rejects non-zero unused null bitmap bits", "[record][row-codec]") {
    auto schema = make_string_schema();
    auto name = Value::string("a", 4);
    REQUIRE(name.ok());

    Row row(std::vector<Value>{
        Value::int64(7),
        name.value()
    });

    auto encoded = RowCodec::encode(schema, row);
    REQUIRE(encoded.ok());

    auto bytes = std::move(encoded.value());
    bytes[0] |= std::byte{ 0x04 };

    auto decoded = RowCodec::decode(schema, bytes);

    REQUIRE_FALSE(decoded.ok());
    REQUIRE(decoded.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("RowCodec decode rejects non-zero payload bytes for null values", "[record][row-codec]") {
    auto schema = make_string_schema();

    Row row(std::vector<Value>{
        Value::int64(7),
        Value::null(schema.column(1).logical_type())
    });

    auto encoded = RowCodec::encode(schema, row);
    REQUIRE(encoded.ok());

    auto bytes = std::move(encoded.value());
    bytes[schema.column(1).fixed_offset()] = std::byte{ 0x01 };

    auto decoded = RowCodec::decode(schema, bytes);

    REQUIRE_FALSE(decoded.ok());
    REQUIRE(decoded.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("RowCodec handles empty and full-capacity strings", "[record][row-codec]") {
    auto schema = make_string_schema();

    SECTION("empty string uses only zero padding") {
        auto name = Value::string("", 4);
        REQUIRE(name.ok());

        Row row(std::vector<Value>{
            Value::int64(7),
            name.value()
        });

        auto encoded = RowCodec::encode(schema, row);
        REQUIRE(encoded.ok());

        const auto offset = schema.column(1).fixed_offset();
        REQUIRE(encoded.value()[offset] == std::byte{ 0x00 });
        REQUIRE(encoded.value()[offset+1] == std::byte{ 0x00 });
        REQUIRE(encoded.value()[offset+2] == std::byte{ 0x00 });
        REQUIRE(encoded.value()[offset+3] == std::byte{ 0x00 });

        auto decoded = RowCodec::decode(schema, encoded.value());
        REQUIRE(decoded.ok());
        REQUIRE(decoded.value().value(1).as_string().empty());
    }

    SECTION("full-capacity string uses the whole fixed region") {
        auto name = Value::string("full", 4);
        REQUIRE(name.ok());

        Row row(std::vector<Value>{
            Value::int64(7),
            name.value()
        });

        auto encoded = RowCodec::encode(schema, row);
        REQUIRE(encoded.ok());

        const auto offset = schema.column(1).fixed_offset();
        REQUIRE(encoded.value()[offset] == std::byte{ 0x66 });
        REQUIRE(encoded.value()[offset+1] == std::byte{ 0x75 });
        REQUIRE(encoded.value()[offset+2] == std::byte{ 0x6C });
        REQUIRE(encoded.value()[offset+3] == std::byte{ 0x6C });

        auto decoded = RowCodec::decode(schema, encoded.value());
        REQUIRE(decoded.ok());
        REQUIRE(decoded.value().value(1).as_string() == "full");
    }
}

TEST_CASE("RowCodec encode rejects strings containing null characters", "[record][row-codec]") {
    auto schema = make_string_schema();
    auto name = Value::string(std::string("a\0b", 3), 4);
    REQUIRE(name.ok());

    Row row(std::vector<Value>{
        Value::int64(7),
        name.value()
    });

    auto encoded = RowCodec::encode(schema, row);

    REQUIRE_FALSE(encoded.ok());
    REQUIRE(encoded.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("RowCodec decode rejects non-zero string padding bytes", "[record][row-codec]") {
    auto schema = make_string_schema();
    auto name = Value::string("a", 4);
    REQUIRE(name.ok());

    Row row(std::vector<Value>{
        Value::int64(7),
        name.value()
    });

    auto encoded = RowCodec::encode(schema, row);
    REQUIRE(encoded.ok());

    auto bytes = std::move(encoded.value());
    bytes[schema.column(1).fixed_offset()+2] = std::byte{ 0x58 };

    auto decoded = RowCodec::decode(schema, bytes);

    REQUIRE_FALSE(decoded.ok());
    REQUIRE(decoded.status().code() == StatusCode::InvalidArgument);
}
