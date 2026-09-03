#pragma once

#include <dandb/core/Status.h>
#include <dandb/execution/Database.h>
#include <dandb/execution/ExecutionResult.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dandb::benchutil {

    std::string make_insert_statement(std::size_t entry_index);
    std::string make_insert_batch(std::size_t first_entry_index, std::size_t entry_count);
    std::string make_primary_key_range_scan_statement(std::size_t first_entry_index);
    std::string make_value_range_scan_statement(std::size_t first_value);
    std::string make_point_lookup_statement(std::size_t lookup_value);
    std::string make_update_value_statement(std::size_t entry_id, std::size_t new_value);
    std::string make_delete_by_primary_key_statement(std::size_t entry_id);
    core::Status populate_benchmark_table(execution::Database& database, std::size_t entry_count);
    core::Status verify_point_lookup_result(const std::vector<execution::ExecutionResult>& results, std::int64_t expected_entry_id);
    core::Status verify_successful_results(const std::vector<execution::ExecutionResult>& results, std::size_t expected_count);
    core::Status verify_rows_affected(const std::vector<execution::ExecutionResult>& results, std::size_t expected_rows_affected);
    core::Status verify_range_scan_result(const std::vector<execution::ExecutionResult>& results, std::size_t expected_row_count);

}
