#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dandb::platform {

    class FileFaultInjector;

    class FileHandle {
        public:
            FileHandle(const FileHandle&) = delete;
            FileHandle& operator=(const FileHandle&) = delete;
            FileHandle(FileHandle&& other) noexcept;
            FileHandle& operator=(FileHandle&& other) noexcept;
            ~FileHandle();

            [[nodiscard]] static core::Result<FileHandle> open_existing(const std::filesystem::path& path);
            [[nodiscard]] static core::Result<FileHandle> create_new(const std::filesystem::path& path);
            [[nodiscard]] static core::Result<FileHandle> open_or_create(const std::filesystem::path& path);

            [[nodiscard]] core::Status read_at(std::uint64_t offset, std::span<std::byte> out);
            [[nodiscard]] core::Status write_at(std::uint64_t offset, std::span<const std::byte> data);

            [[nodiscard]] core::Status sync();
            [[nodiscard]] core::Result<std::uint64_t> size() const;
            [[nodiscard]] core::Status resize(std::uint64_t new_size);
            [[nodiscard]] core::Status close();

            void set_fault_injector(FileFaultInjector* fault_injector);
        private:
            explicit FileHandle(std::filesystem::path path, void* handle);

            std::filesystem::path path_;
            void* handle_ = nullptr;

            FileFaultInjector* fault_injector_ = nullptr;
    };

}