#pragma once

#include <dandb/core/Status.h>
#include <dandb/execution/ExecutionResult.h>

#include <cstddef>
#include <vector>

namespace dandb::benchutil {

    core::Status verify_successful_results(const std::vector<execution::ExecutionResult>& results, std::size_t expected_count);

}
