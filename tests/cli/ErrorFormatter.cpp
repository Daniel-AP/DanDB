#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>

#include <ErrorFormatter.h>

namespace dandb::cli {

    TEST_CASE("CLI error formatter identifies serious database failures", "[cli][errors]") {

        const auto io_error = core::Status::IoError("Cannot write database file");
        const auto corruption = core::Status::Corruption("Database header checksum does not match");
        const auto sql_error = core::Status::ParseError("SQL error at line 1, column 1: unexpected character '@'");

        REQUIRE(format_status(io_error) == "Database I/O error: Cannot write database file");
        REQUIRE(format_status(corruption) == "Database corruption: Database header checksum does not match");
        REQUIRE(format_status(sql_error) == "SQL error at line 1, column 1: unexpected character '@'");

    }

}
