#pragma once

#include <filesystem>

namespace dandb::benchutil {

    class TempDir {
        public:
            TempDir();
            ~TempDir();

            const std::filesystem::path& path() const;
            std::filesystem::path database_path() const;
            std::filesystem::path wal_path() const;

        private:
            std::filesystem::path path_;
    };

}
