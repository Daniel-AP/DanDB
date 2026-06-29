#include <dandb/platform/DatabasePath.h>

#include <utility>

namespace dandb::platform {

    DatabasePath::DatabasePath(std::filesystem::path path) : path_(std::move(path)) {}

    std::filesystem::path DatabasePath::main_path() const {
        return path_;
    }

    std::filesystem::path DatabasePath::wal_path() const {
        std::filesystem::path res_path = path_;
        res_path += ".wal";
        return res_path;
    }

    std::string DatabasePath::display_name() const {
        return path_.generic_string();
    }

}