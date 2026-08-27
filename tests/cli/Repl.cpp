#include <catch_amalgamated.hpp>

#include <dandb/execution/Database.h>

#include <testutil/TempDir.h>

#include <Repl.h>

#include <sstream>
#include <string>

namespace dandb::cli {

    TEST_CASE("Repl prints help without sending it to the database", "[cli][repl]") {
        const testutil::TempDir temp_dir;
        auto database_result = execution::Database::open_or_create(temp_dir.database_path());
        REQUIRE(database_result.ok());

        auto& database = database_result.value();

        std::istringstream input(".help\n.exit\n");
        std::ostringstream output;
        std::ostringstream error_output;

        Repl repl(database, input, output, error_output);
        repl.run();

        REQUIRE(output.str().find("Supported SQL:") != std::string::npos);
        REQUIRE(output.str().find("Commands: .help, .exit.") != std::string::npos);
        REQUIRE(error_output.str().empty());
        REQUIRE(database.close().ok());
    }

    TEST_CASE("Repl displays query rows and execution messages", "[cli][repl]") {
        const testutil::TempDir temp_dir;
        auto database_result = execution::Database::open_or_create(temp_dir.database_path());
        REQUIRE(database_result.ok());

        auto& database = database_result.value();

        std::istringstream input(
            "CREATE TABLE users (id INT64 PRIMARY KEY, name STRING(64));\n"
            "INSERT INTO users VALUES (1, 'Ada');\n"
            "SELECT * FROM users;\n"
            ".exit\n"
        );
        std::ostringstream output;
        std::ostringstream error_output;

        Repl repl(database, input, output, error_output);
        repl.run();

        REQUIRE(output.str().find("Table 'users' created") != std::string::npos);
        REQUIRE(output.str().find("1 row affected") != std::string::npos);
        REQUIRE(output.str().find("id, name") != std::string::npos);
        REQUIRE(output.str().find("1, \"Ada\"") != std::string::npos);
        REQUIRE(output.str().find("(1 row)") != std::string::npos);
        REQUIRE(error_output.str().empty());
        REQUIRE(database.close().ok());
    }

    TEST_CASE("Repl submits pending input unchanged after a blank continuation line", "[cli][repl]") {
        const testutil::TempDir temp_dir;
        auto database_result = execution::Database::open_or_create(temp_dir.database_path());
        REQUIRE(database_result.ok());

        auto& database = database_result.value();

        std::istringstream input("SELECT * FROM dandb_tables\n\n.exit\n");
        std::ostringstream output;
        std::ostringstream error_output;

        Repl repl(database, input, output, error_output);
        repl.run();

        REQUIRE(output.str().find("...> ") != std::string::npos);
        REQUIRE(error_output.str() == "SQL error at line 2, column 1: expected ';' after statement\n");
        REQUIRE(database.close().ok());
    }

    TEST_CASE("Repl displays an unknown-table error", "[cli][repl]") {
        const testutil::TempDir temp_dir;
        auto database_result = execution::Database::open_or_create(temp_dir.database_path());
        REQUIRE(database_result.ok());

        auto& database = database_result.value();

        std::istringstream input("SELECT * FROM missing;\n.exit\n");
        std::ostringstream output;
        std::ostringstream error_output;

        Repl repl(database, input, output, error_output);
        repl.run();

        REQUIRE(error_output.str() == "SQL error at line 1, column 15: Table 'missing' does not exist\n");
        REQUIRE(database.close().ok());
    }

    TEST_CASE("Repl stops the current paged result after nonempty pager input", "[cli][repl]") {
        const testutil::TempDir temp_dir;
        auto database_result = execution::Database::open_or_create(temp_dir.database_path());
        REQUIRE(database_result.ok());

        auto& database = database_result.value();

        const auto create_results = database.execute("CREATE TABLE users (id INT64 PRIMARY KEY);");
        REQUIRE(create_results.size() == 1);
        REQUIRE(create_results[0].status.ok());

        std::ostringstream setup_sql;
        for(int id = 1; id <= 101; id++) {
            setup_sql << "INSERT INTO users VALUES (" << id << ");";
        }

        const auto insert_results = database.execute(setup_sql.str());
        REQUIRE(insert_results.size() == 101);
        for(const auto& result: insert_results) {
            INFO(result.status.message());
            REQUIRE(result.status.ok());
        }

        std::istringstream input("SELECT * FROM users;\nstop\n.exit\n");
        std::ostringstream output;
        std::ostringstream error_output;

        Repl repl(database, input, output, error_output);
        repl.run();

        REQUIRE(output.str().find("--More--") != std::string::npos);
        REQUIRE(output.str().find("\n100\n") != std::string::npos);
        REQUIRE(output.str().find("\n101\n") == std::string::npos);
        REQUIRE(output.str().find("(100 of 101 rows shown)") != std::string::npos);
        REQUIRE(error_output.str().empty());
        REQUIRE(database.close().ok());
    }

}
