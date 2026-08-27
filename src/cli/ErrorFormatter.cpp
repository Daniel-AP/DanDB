#include "ErrorFormatter.h"

namespace dandb::cli {

    std::string format_status(const core::Status& status) {

        if(status.code() == core::StatusCode::IoError) {
            return "Database I/O error: "+status.message();
        }

        if(status.code() == core::StatusCode::Corruption) {
            return "Database corruption: "+status.message();
        }

        return status.message();

    }

}
