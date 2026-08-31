#include <dandb/execution/Database.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>

namespace {
    constexpr const char* DATABASE_FILE_NAME = "test.dandb";
}

int main() {

    // Read SQL until the parent closes the input pipe
    std::string sql;
    std::string line;

    while(std::getline(std::cin, line)) {
        sql += line;
        sql += '\n';
    }

    if(sql.empty()) return 2;

    std::error_code error_code;
    const auto working_directory = std::filesystem::current_path(error_code);

    if(error_code) {
        std::cerr << "Cannot resolve crash worker working directory: " << error_code.message() << '\n';
        return 1;
    }

    const auto database_path = working_directory/DATABASE_FILE_NAME;
    auto database_result = dandb::execution::Database::open_or_create(database_path);

    if(!database_result.ok()) {
        std::cerr << database_result.status().message() << '\n';
        return 1;
    }

    auto database = std::move(database_result.value());
    const auto results = database.execute(sql);

    for(const auto& result: results) {

        if(result.status.ok()) continue;

        std::cerr << result.status.message() << '\n';
        return 1;
    }

    // Tell the parent that the SQL completed before the crash point
    std::cout << "READY\n" << std::flush;

    // Keep the database open until the parent force-terminates this process
    Sleep(INFINITE);
}
