#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dandb::platform {

    class FileHandle {
        public:
            FileHandle(const FileHandle&) = delete;
            FileHandle& operator=(const FileHandle&) = delete;
            FileHandle(FileHandle&& other) noexcept;
            FileHandle& operator=(FileHandle&& other) noexcept;
            ~FileHandle();

            [[nodiscard]] static dandb::core::Result<FileHandle> open_existing(const std::filesystem::path& path);
            [[nodiscard]] static dandb::core::Result<FileHandle> create_new(const std::filesystem::path& path);
            [[nodiscard]] static dandb::core::Result<FileHandle> open_or_create(const std::filesystem::path& path);

            [[nodiscard]] dandb::core::Status read_at(std::uint64_t offset, std::span<std::byte> out);
            [[nodiscard]] dandb::core::Status write_at(std::uint64_t offset, std::span<const std::byte> data);

            [[nodiscard]] dandb::core::Status sync();
            [[nodiscard]] dandb::core::Result<std::uint64_t> size() const;
            [[nodiscard]] dandb::core::Status resize(std::uint64_t new_size);
            [[nodiscard]] dandb::core::Status close();
        private:
            explicit FileHandle(std::filesystem::path path, void* handle);

            std::filesystem::path path_;
            void* handle_ = nullptr;
    };

}