#include "SqlHelpers.h"

namespace dandb::benchutil {

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
