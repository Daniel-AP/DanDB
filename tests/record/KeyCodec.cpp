#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/record/KeyCodec.h>
#include <dandb/record/LogicalType.h>
#include <dandb/record/Value.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

using dandb::core::Result;
using dandb::core::StatusCode;
using dandb::record::KeyCodec;
using dandb::record::LogicalType;
using dandb::record::Value;

namespace {

    Value require_value(Result<Value> value) {
        REQUIRE(value.ok());
        return value.value();
    }

    std::vector<std::byte> require_key(const Value& value) {
        auto key = KeyCodec::encode(value);
        REQUIRE(key.ok());
        return key.value();
    }

    bool bytes_less(const std::vector<std::byte>& left, const std::vector<std::byte>& right) {
        return std::lexicographical_compare(
            left.begin(),
            left.end(),
            right.begin(),
            right.end(),
            [](std::byte a, std::byte b) {
                return std::to_integer<unsigned int>(a) < std::to_integer<unsigned int>(b);
            }
        );
    }

}

TEST_CASE("KeyCodec encodes signed integers into bytewise sortable keys", "[record][key-codec]") {
    SECTION("INT8") {
        const auto negative = require_key(require_value(Value::int8(-1)));
        const auto zero = require_key(require_value(Value::int8(0)));
        const auto positive = require_key(require_value(Value::int8(1)));

        REQUIRE(negative == std::vector<std::byte>{ std::byte{ 0x7F } });
        REQUIRE(zero == std::vector<std::byte>{ std::byte{ 0x80 } });
        REQUIRE(positive == std::vector<std::byte>{ std::byte{ 0x81 } });

        REQUIRE(bytes_less(negative, zero));
        REQUIRE(bytes_less(zero, positive));
    }

    SECTION("INT16") {
        const auto negative = require_key(require_value(Value::int16(-1)));
        const auto zero = require_key(require_value(Value::int16(0)));
        const auto positive = require_key(require_value(Value::int16(1)));

        REQUIRE(negative == std::vector<std::byte>{ std::byte{ 0x7F }, std::byte{ 0xFF } });
        REQUIRE(zero == std::vector<std::byte>{ std::byte{ 0x80 }, std::byte{ 0x00 } });
        REQUIRE(positive == std::vector<std::byte>{ std::byte{ 0x80 }, std::byte{ 0x01 } });

        REQUIRE(bytes_less(negative, zero));
        REQUIRE(bytes_less(zero, positive));
    }

    SECTION("INT32") {
        const auto negative = require_key(require_value(Value::int32(-1)));
        const auto zero = require_key(require_value(Value::int32(0)));
        const auto positive = require_key(require_value(Value::int32(1)));

        REQUIRE(negative == std::vector<std::byte>{
            std::byte{ 0x7F }, std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF }
        });
        REQUIRE(zero == std::vector<std::byte>{
            std::byte{ 0x80 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }
        });
        REQUIRE(positive == std::vector<std::byte>{
            std::byte{ 0x80 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x01 }
        });

        REQUIRE(bytes_less(negative, zero));
        REQUIRE(bytes_less(zero, positive));
    }

    SECTION("INT64") {
        const auto negative = require_key(Value::int64(-1));
        const auto zero = require_key(Value::int64(0));
        const auto positive = require_key(Value::int64(1));

        REQUIRE(negative == std::vector<std::byte>{
            std::byte{ 0x7F }, std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF },
            std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF }
        });
        REQUIRE(zero == std::vector<std::byte>{
            std::byte{ 0x80 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }
        });
        REQUIRE(positive == std::vector<std::byte>{
            std::byte{ 0x80 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x01 }
        });

        REQUIRE(bytes_less(negative, zero));
        REQUIRE(bytes_less(zero, positive));
    }
}

TEST_CASE("KeyCodec encodes strings as fixed-size bytewise lexicographic keys", "[record][key-codec]") {
    auto a = Value::string("a", 4);
    auto aa = Value::string("aa", 4);
    auto b = Value::string("b", 4);

    REQUIRE(a.ok());
    REQUIRE(aa.ok());
    REQUIRE(b.ok());

    const auto a_key = require_key(a.value());
    const auto aa_key = require_key(aa.value());
    const auto b_key = require_key(b.value());

    REQUIRE(a_key == std::vector<std::byte>{
        std::byte{ 0x61 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }
    });

    REQUIRE(bytes_less(a_key, aa_key));
    REQUIRE(bytes_less(aa_key, b_key));
}

TEST_CASE("KeyCodec encodes bool false before true", "[record][key-codec]") {
    const auto false_key = require_key(Value::boolean(false));
    const auto true_key = require_key(Value::boolean(true));

    REQUIRE(false_key == std::vector<std::byte>{ std::byte{ 0x00 } });
    REQUIRE(true_key == std::vector<std::byte>{ std::byte{ 0x01 } });
    REQUIRE(bytes_less(false_key, true_key));
}

TEST_CASE("KeyCodec rejects values that cannot be index keys in v1", "[record][key-codec]") {
    SECTION("null") {
        const auto result = KeyCodec::encode(Value::null(LogicalType::int64()));

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("DOUBLE") {
        const auto result = KeyCodec::encode(Value::float64(1.5));

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("string containing null character") {
        auto value = Value::string(std::string("a\0b", 3), 4);
        REQUIRE(value.ok());

        const auto result = KeyCodec::encode(value.value());

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::InvalidArgument);
    }
}
