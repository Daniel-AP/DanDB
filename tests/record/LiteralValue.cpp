#include <catch_amalgamated.hpp>

#include <dandb/record/LiteralValue.h>

using dandb::record::LiteralValue;

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
