#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>

using dandb::core::Status;
using dandb::core::StatusCode;

TEST_CASE("Status has the required status codes", "[core][status]") {
    const auto codes = std::array{
        StatusCode::Ok,
        StatusCode::InvalidArgument,
        StatusCode::IoError,
        StatusCode::Corruption,
        StatusCode::NotFound,
        StatusCode::AlreadyExists,
        StatusCode::ConstraintViolation,
        StatusCode::TransactionError,
        StatusCode::ParseError,
        StatusCode::InternalError,
    };

    REQUIRE(codes.size() == 10);
}

TEST_CASE("Status::Ok creates a successful status", "[core][status]") {
    const auto status = Status::Ok();

    REQUIRE(status.ok());
    REQUIRE(status.code() == StatusCode::Ok);
    REQUIRE(status.message().empty());
}

TEST_CASE("Status::InvalidArgument creates a readable failure status", "[core][status]") {
    const auto status = Status::InvalidArgument("table name must not be empty");

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);
    REQUIRE(status.message() == "table name must not be empty");
}

TEST_CASE("Status failure factories preserve their code and message", "[core][status]") {
    SECTION("InvalidArgument") {
        const auto status = Status::InvalidArgument("bad input");
        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
        REQUIRE(status.message() == "bad input");
    }

    SECTION("IoError") {
        const auto status = Status::IoError("could not read file");
        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::IoError);
        REQUIRE(status.message() == "could not read file");
    }

    SECTION("Corruption") {
        const auto status = Status::Corruption("page checksum mismatch");
        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::Corruption);
        REQUIRE(status.message() == "page checksum mismatch");
    }

    SECTION("NotFound") {
        const auto status = Status::NotFound("table users was not found");
        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::NotFound);
        REQUIRE(status.message() == "table users was not found");
    }

    SECTION("AlreadyExists") {
        const auto status = Status::AlreadyExists("table users already exists");
        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::AlreadyExists);
        REQUIRE(status.message() == "table users already exists");
    }

    SECTION("ConstraintViolation") {
        const auto status = Status::ConstraintViolation("primary key already exists");
        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::ConstraintViolation);
        REQUIRE(status.message() == "primary key already exists");
    }

    SECTION("TransactionError") {
        const auto status = Status::TransactionError("transaction is already failed");
        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::TransactionError);
        REQUIRE(status.message() == "transaction is already failed");
    }

    SECTION("ParseError") {
        const auto status = Status::ParseError("expected table name");
        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::ParseError);
        REQUIRE(status.message() == "expected table name");
    }

    SECTION("InternalError") {
        const auto status = Status::InternalError("unreachable planner state");
        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InternalError);
        REQUIRE(status.message() == "unreachable planner state");
    }
}
