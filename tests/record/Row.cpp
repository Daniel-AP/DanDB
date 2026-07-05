#include <catch_amalgamated.hpp>

#include <dandb/record/LogicalType.h>
#include <dandb/record/Row.h>
#include <dandb/record/Value.h>

#include <utility>
#include <vector>

using dandb::record::LogicalType;
using dandb::record::Row;
using dandb::record::Value;

TEST_CASE("Row exposes values and null state by ordinal", "[record][row]") {
    std::vector<Value> values;
    values.push_back(Value::int64(42));
    values.push_back(Value::null(LogicalType::boolean()));

    Row row(std::move(values));

    REQUIRE(row.value_count() == 2);
    REQUIRE(row.values().size() == 2);
    REQUIRE(row.value(0).as_integer() == 42);
    REQUIRE_FALSE(row.is_null(0));
    REQUIRE(row.is_null(1));
}
