#include <catch_amalgamated.hpp>

#include <dandb/core/Result.h>

#include <stdexcept>
#include <string>

using dandb::core::Result;
using dandb::core::Status;
using dandb::core::StatusCode;

TEST_CASE("Result stores a value when successful", "[core][result]") {
    const Result<int> result(42);

    REQUIRE(result.ok());
    REQUIRE(result.status().ok());
    REQUIRE(result.status().code() == StatusCode::Ok);
    REQUIRE(result.value() == 42);
}

TEST_CASE("Result exposes mutable value access when successful", "[core][result]") {
    Result<std::string> result(std::string{"old name"});

    result.value() = "new name";

    REQUIRE(result.ok());
    REQUIRE(result.value() == "new name");
}

TEST_CASE("Result stores a status when failed", "[core][result]") {
    const Result<int> result(Status::NotFound("table users was not found"));

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == StatusCode::NotFound);
    REQUIRE(result.status().message() == "table users was not found");
}

TEST_CASE("Result does not throw for normal database errors", "[core][result]") {
    REQUIRE_NOTHROW(Result<int>(Status::IoError("failed to read database header")));
}

TEST_CASE("Result rejects an ok status because it would not contain a value", "[core][result]") {
    REQUIRE_THROWS_AS(Result<int>(Status::Ok()), std::invalid_argument);
}
