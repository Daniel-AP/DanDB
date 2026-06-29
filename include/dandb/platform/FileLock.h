#pragma once

#include <dandb/core/Result.h>
#include <dandb/core/Status.h>

#include <filesystem>

namespace dandb::platform {

    class FileLock {
        public:
            FileLock(const FileLock&) = delete;
            FileLock& operator=(const FileLock&) = delete;
            FileLock(FileLock&& other) noexcept;
            FileLock& operator=(FileLock&& other) noexcept;
            ~FileLock();

            [[nodiscard]] static dandb::core::Result<FileLock> acquire_exclusive(const std::filesystem::path& path);

            [[nodiscard]] dandb::core::Status close();
            bool is_locked() const;
            const std::filesystem::path& path() const;

        private:
            FileLock(std::filesystem::path path, void* handle);

            void* handle_ = nullptr;
            std::filesystem::path path_;
    };

}