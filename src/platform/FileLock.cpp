#include <dandb/platform/FileLock.h>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <utility>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace {

    constexpr std::uint64_t DATABASE_LOCK_OFFSET = (1ULL<<63)-(1ULL<<16);
    constexpr DWORD DATABASE_LOCK_LENGTH_LOW = 1;
    constexpr DWORD DATABASE_LOCK_LENGTH_HIGH = 0;

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
        switch(error) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                return dandb::core::Status::NotFound(
                    windows_error_message(action, path, error)
                );

            case ERROR_LOCK_VIOLATION:
            case ERROR_SHARING_VIOLATION:
                return dandb::core::Status::IoError(
                    "Cannot lock database file '" + path.string() +
                    "': database is locked or already in use"
                );

            default:
                return dandb::core::Status::IoError(
                    windows_error_message(action, path, error)
                );
        }
    }

}

namespace dandb::platform {

    FileLock::FileLock(std::filesystem::path path, void* handle) : path_(std::move(path)), handle_(handle) {}

    FileLock::~FileLock() { static_cast<void>(close()); }

    FileLock::FileLock(FileLock&& other) noexcept : handle_(other.handle_), path_(std::move(other.path_)) {
        other.handle_ = nullptr;
    }

    FileLock& FileLock::operator=(FileLock&& other) noexcept {
        if(this != &other) {
            static_cast<void>(close());
            handle_ = other.handle_;
            path_ = std::move(other.path_);
            other.handle_ = nullptr;
        }
        return *this;
    }

    dandb::core::Result<FileLock> FileLock::acquire_exclusive(const std::filesystem::path& path) {

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

        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(DATABASE_LOCK_OFFSET&0xFFFFFFFFULL);
        overlapped.OffsetHigh = static_cast<DWORD>(DATABASE_LOCK_OFFSET>>32);

        BOOL ok = LockFileEx(
            handle,
            LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
            0,
            DATABASE_LOCK_LENGTH_LOW,
            DATABASE_LOCK_LENGTH_HIGH,
            &overlapped
        );

        if(!ok) {

            const DWORD lock_error = GetLastError();

            ok = CloseHandle(static_cast<HANDLE>(handle));

            if(!ok) {
                const DWORD close_handle_error = GetLastError();
                return status_from_windows_error("Cannot close database file lock handle after failed lock attempt", path, close_handle_error);
            }

            return status_from_windows_error("Cannot apply database file lock", path, lock_error);

        }

        return FileLock{path, handle};

    }

    dandb::core::Status FileLock::close() {

        if(handle_ == nullptr) return dandb::core::Status::Ok();

        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(DATABASE_LOCK_OFFSET&0xFFFFFFFFULL);
        overlapped.OffsetHigh = static_cast<DWORD>(DATABASE_LOCK_OFFSET>>32);

        BOOL ok = UnlockFileEx(
            static_cast<HANDLE>(handle_),
            0,
            DATABASE_LOCK_LENGTH_LOW,
            DATABASE_LOCK_LENGTH_HIGH,
            &overlapped
        );
        if(!ok) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot release database file lock", path_, error);
        }

        ok = CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;

        if(!ok) {
            const DWORD error = GetLastError();
            return status_from_windows_error("Cannot close database file lock handle after releasing lock", path_, error);
        }

        return dandb::core::Status::Ok();

    }

    bool FileLock::is_locked() const {
        return handle_ != nullptr;
    }

    const std::filesystem::path& FileLock::path() const {
        return path_;
    }

}