#pragma once

#include <dandb/core/Status.h>

#include <string>

namespace dandb::cli {

    std::string format_status(const core::Status& status);

}
