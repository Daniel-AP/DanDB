#include <dandb/platform/FileHandle.h>

#include <dandb/core/Status.h>
#include <dandb/platform/FileFaultInjector.h>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <utility>
#include <string>
#include <string_view>
#include <system_error>
#include <limits>

namespace {

    std::string windows_error_message(
        std::string_view action,
        const std::filesystem::path& path,
        DWORD error
    ) {
        const std::error_code error_code(
            static_cast<int>(error),
            std::system_category()
        );

        return std::string(action) +
            " '" + path.string() + "': " +
            error_code.message() +
            " (Windows error " + std::to_string(error) + ")";
    }

    dandb::core::Status status_from_windows_error(
        std::string_view action,
        const std::filesystem::path& path,
        DWORD error
    ) {
        const auto message = windows_error_message(action, path, error);

        switch(error) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                return dandb::core::Status::NotFound(message);

            case ERROR_FILE_EXISTS:
            case ERROR_ALREADY_EXISTS:
                return dandb::core::Status::AlreadyExists(message);

            default:
                return dandb::core::Status::IoError(message);
        }
    }

}

namespace dandb::platform {

    FileHandle::FileHandle(std::filesystem::path path, void* handle) : path_(std::move(path)), handle_(handle) {}
    
    FileHandle::~FileHandle() { static_cast<void>(close()); }

    FileHandle::FileHandle(FileHandle&& other) noexcept : path_(std::move(other.path_)), handle_(other.handle_), fault_injector_(other.fault_injector_) {
        other.handle_ = nullptr;
        other.fault_injector_ = nullptr;
    }

    FileHandle& FileHandle::operator=(FileHandle&& other) noexcept {
        if(this != &other) {
            static_cast<void>(close());
            path_ = std::move(other.path_);
            handle_ = other.handle_;
            fault_injector_ = other.fault_injector_;
            other.handle_ = nullptr;
            other.fault_injector_ = nullptr;
        }
        return *this;
    }

    core::Result<FileHandle> FileHandle::create_new(const std::filesystem::path& path) {
        
        HANDLE handle = CreateFileW(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if(handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot create new file", path, error);
        }

        return FileHandle{path, handle};

    }

    core::Result<FileHandle> FileHandle::open_existing(const std::filesystem::path& path) {

        HANDLE handle = CreateFileW(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if(handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot open existing file", path, error);
        }

        return FileHandle{path, handle};

    }

    core::Result<FileHandle> FileHandle::open_or_create(const std::filesystem::path& path) {

        HANDLE handle = CreateFileW(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if(handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot open or create file", path, error);
        }

        return FileHandle{path, handle};

    }

    core::Status FileHandle::read_at(std::uint64_t offset, std::span<std::byte> out) {

        if(handle_ == nullptr) {
            return core::Status::InvalidArgument("Cannot read file '"+path_.string()+"': file handle is closed");
        }

        if(out.empty()) {
            return core::Status::Ok();
        }

        if(out.size() > MAXDWORD) {
            return core::Status::InvalidArgument("Cannot read file '"+path_.string()+"': requested size is too big");
        }

        HANDLE handle = static_cast<HANDLE>(handle_);
        DWORD requested_size = static_cast<DWORD>(out.size());
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(offset&0xFFFFFFFFULL);
        overlapped.OffsetHigh = static_cast<DWORD>(offset>>32);
        DWORD bytes_read = 0;

        BOOL ok = ReadFile(
            handle,
            out.data(),
            requested_size,
            &bytes_read,
            &overlapped
        );

        if(!ok) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot read file at offset "+std::to_string(offset), path_, error);
        }

        if(bytes_read != requested_size) {
            return core::Status::IoError(
                "Cannot read file '" + path_.string() +
                "' at offset " + std::to_string(offset) +
                ": short read, requested " + std::to_string(requested_size) +
                " bytes but read " + std::to_string(bytes_read)
            );
        }

        return core::Status::Ok();

    }

    core::Status FileHandle::write_at(std::uint64_t offset, std::span<const std::byte> data) {

        if(handle_ == nullptr) {
            return core::Status::InvalidArgument("Cannot write file '"+path_.string()+"': file handle is closed");
        }

        if(data.empty()) {
            return core::Status::Ok();
        }

        if(fault_injector_ != nullptr) {
            auto status = fault_injector_->before_write(offset, data.size());

            if(!status.ok()) {
                return status;
            }
        }

        HANDLE handle = static_cast<HANDLE>(handle_);
        std::size_t total_written = 0;

        while(total_written < data.size()) {
            const std::size_t remaining = data.size()-total_written;
            DWORD requested_size = MAXDWORD;

            if(remaining < MAXDWORD) {
                requested_size = static_cast<DWORD>(remaining);
            }

            const std::uint64_t current_offset = offset+total_written;

            OVERLAPPED overlapped{};
            overlapped.Offset = static_cast<DWORD>(current_offset&0xFFFFFFFFULL);
            overlapped.OffsetHigh = static_cast<DWORD>(current_offset>>32);
            DWORD bytes_written = 0;

            BOOL ok = WriteFile(
                handle,
                data.data()+total_written,
                requested_size,
                &bytes_written,
                &overlapped
            );

            if(!ok) {
                const DWORD error = GetLastError();
                return status_from_windows_error("Cannot write file at offset "+std::to_string(current_offset), path_, error);
            }

            if(bytes_written == 0) {
                return core::Status::IoError(
                    "Cannot write file '" + path_.string() +
                    "' at offset " + std::to_string(current_offset) +
                    ": wrote 0 bytes"
                );
            }

            total_written += bytes_written;
        }

        return core::Status::Ok();

    }

    core::Status FileHandle::sync() {

        if(handle_ == nullptr) {
            return core::Status::InvalidArgument("Cannot sync file: file handle is closed");
        }

        if(fault_injector_ != nullptr) {
            auto status = fault_injector_->before_sync();

            if(!status.ok()) {
                return status;
            }
        }

        BOOL ok = FlushFileBuffers(static_cast<HANDLE>(handle_));

        if(!ok) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot sync file", path_, error);
        }

        if(fault_injector_ != nullptr) {
            auto status = fault_injector_->after_sync();

            if(!status.ok()) {
                return status;
            }
        }

        return core::Status::Ok();

    }

    core::Result<std::uint64_t> FileHandle::size() const {

        if(handle_ == nullptr) {
            return core::Status::InvalidArgument("Cannot get file size: file handle is closed");
        }

        LARGE_INTEGER file_size{};
        BOOL ok = GetFileSizeEx(
            static_cast<HANDLE>(handle_),
            &file_size
        );

        if(!ok) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot get file size", path_, error);
        }

        return static_cast<std::uint64_t>(file_size.QuadPart);

    }

    core::Status FileHandle::resize(std::uint64_t new_size) {

        if(handle_ == nullptr) {
            return core::Status::InvalidArgument("Cannot resize file: file handle is closed");
        }

        if(new_size > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) {
            return core::Status::InvalidArgument("Cannot resize file: new size is too big");
        }

        if(fault_injector_ != nullptr) {
            auto status = fault_injector_->before_resize(new_size);

            if(!status.ok()) {
                return status;
            }
        }

        LARGE_INTEGER distance{};
        distance.QuadPart = static_cast<LONGLONG>(new_size);

        BOOL ok = SetFilePointerEx(
            static_cast<HANDLE>(handle_),
            distance,
            nullptr,
            FILE_BEGIN
        );
        if(!ok) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot resize file", path_, error);
        }

        if(fault_injector_ != nullptr) {
            auto status = fault_injector_->after_resize_file_pointer(new_size);

            if(!status.ok()) {
                return status;
            }
        }

        ok = SetEndOfFile(static_cast<HANDLE>(handle_));
        if(!ok) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot resize file", path_, error);
        }

        if(fault_injector_ != nullptr) {
            auto status = fault_injector_->after_resize_end_of_file(new_size);

            if(!status.ok()) {
                return status;
            }
        }

        return core::Status::Ok();

    }

    core::Status FileHandle::close() {

        if(handle_ == nullptr) return core::Status::Ok();

        BOOL ok = CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;

        if(!ok) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot close file", path_, error);
        }

        return core::Status::Ok();

    }

    void FileHandle::set_fault_injector(FileFaultInjector* fault_injector) {
        fault_injector_ = fault_injector;
    }

}
