#pragma once

#include <filesystem>
#include <string>

namespace dandb::platform {

    class DatabasePath {
        public:
            explicit DatabasePath(std::filesystem::path path);

            std::filesystem::path main_path() const;
            std::filesystem::path wal_path() const;
            std::string display_name() const;
        private:
            std::filesystem::path path_;
    };

}