#include "Repl.h"

#include <dandb/execution/Database.h>

#include <filesystem>
#include <iostream>
#include <system_error>
#include <utility>

namespace {

    constexpr const char* USAGE_MESSAGE = "Usage: dandb_cli <database-path>\n";
    constexpr const char* CONNECTION_PREFIX = "Connected to ";
    constexpr const char* GOODBYE_MESSAGE = "Goodbye.\n";

}

int main(int argument_count, char* arguments[]) {

    if(argument_count != 2) {
        std::cerr << USAGE_MESSAGE;
        return 2;
    }

    std::error_code error_code;
    const std::filesystem::path database_path = std::filesystem::absolute(arguments[1], error_code);
    if(error_code) {
        std::cerr << "Cannot resolve database path: " << error_code.message() << '\n';
        return 1;
    }

    auto database_result = dandb::execution::Database::open_or_create(database_path);
    if(!database_result.ok()) {
        std::cerr << database_result.status().message() << '\n';
        return 1;
    }

    auto database = std::move(database_result.value());

    std::cout << CONNECTION_PREFIX << database_path.string() << '\n';

    dandb::cli::Repl repl(database, std::cin, std::cout, std::cerr);
    repl.run();

    const auto close_status = database.close();
    if(!close_status.ok()) {
        std::cerr << close_status.message() << '\n';
        return 1;
    }

    std::cout << GOODBYE_MESSAGE;
    return 0;

}
