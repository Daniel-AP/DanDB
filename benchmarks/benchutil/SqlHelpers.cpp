#include "SqlHelpers.h"

#include <algorithm>
#include <string>

namespace dandb::benchutil {

    std::string make_insert_batch(std::size_t first_entry_index, std::size_t entry_count) {

        std::string statements = "BEGIN;";

        for(std::size_t entry_offset = 0; entry_offset < entry_count; entry_offset++) {

            const auto entry_index = first_entry_index+entry_offset;
            const auto value = std::to_string(entry_index);
            statements += "INSERT INTO benchmark_rows VALUES ("+value+", "+value+");";

        }

        statements += "COMMIT;";
        return statements;

    }

    core::Status populate_benchmark_table(execution::Database& database, std::size_t entry_count) {

        constexpr std::size_t SQL_INSERT_BATCH_SIZE = 10;

        const auto create_results = database.execute(
            "CREATE TABLE benchmark_rows ("
            "id INT64 PRIMARY KEY, "
            "value INT64 NOT NULL"
            ");"
        );
        const auto create_status = verify_successful_results(create_results, 1);
        if(!create_status.ok()) return create_status;

        for(std::size_t first_entry_index = 0; first_entry_index < entry_count; first_entry_index += SQL_INSERT_BATCH_SIZE) {

            const auto remaining_entry_count = entry_count-first_entry_index;
            const auto batch_entry_count = std::min(SQL_INSERT_BATCH_SIZE, remaining_entry_count);
            const auto insert_results = database.execute(make_insert_batch(first_entry_index, batch_entry_count));
            const auto expected_result_count = batch_entry_count+2;
            const auto insert_status = verify_successful_results(insert_results, expected_result_count);
            if(!insert_status.ok()) {

                return core::Status::InternalError(
                    "SQL benchmark row load failed at entry "+std::to_string(first_entry_index)+": "+insert_status.message()
                );

            }

        }

        const auto checkpoint_results = database.execute("CHECKPOINT;");
        return verify_successful_results(checkpoint_results, 1);

    }

    core::Status verify_successful_results(const std::vector<execution::ExecutionResult>& results, std::size_t expected_count) {

        if(results.size() != expected_count) {

            if(!results.empty() && !results.back().status.ok()) return results.back().status;

            return core::Status::InternalError("SQL benchmark returned an unexpected number of execution results");
        }

        for(const auto& result: results) {
            if(!result.status.ok()) return result.status;
        }

        return core::Status::Ok();

    }

}
