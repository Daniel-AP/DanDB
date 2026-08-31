#include <testutil/CrashTestHarness.h>

#include <testutil/ScopedHandle.h>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace {

    constexpr std::chrono::milliseconds POLL_INTERVAL{ 10 };

    dandb::core::Status windows_error(std::string_view action) {

        const DWORD error = GetLastError();
        const std::error_code error_code(static_cast<int>(error), std::system_category());

        return dandb::core::Status::IoError(
            std::string(action)+": "+error_code.message()+" (Windows error "+std::to_string(error)+")"
        );

    }

}

namespace dandb::testutil {

    CrashTestHarness::CrashTestHarness(void* process_handle, void* output_read_handle)
        : process_handle_(process_handle), output_read_handle_(output_read_handle) {}

    CrashTestHarness::CrashTestHarness(CrashTestHarness&& other) noexcept
        : process_handle_(std::exchange(other.process_handle_, nullptr)),
          output_read_handle_(std::exchange(other.output_read_handle_, nullptr)) {}

    CrashTestHarness& CrashTestHarness::operator=(CrashTestHarness&& other) noexcept {

        if(this == &other) return *this;

        close_handles();
        process_handle_ = std::exchange(other.process_handle_, nullptr);
        output_read_handle_ = std::exchange(other.output_read_handle_, nullptr);

        return *this;
        
    }

    CrashTestHarness::~CrashTestHarness() { close_handles(); }

    void CrashTestHarness::close_handles() {

        if(process_handle_ != nullptr) CloseHandle(static_cast<HANDLE>(process_handle_));
        if(output_read_handle_ != nullptr) CloseHandle(static_cast<HANDLE>(output_read_handle_));

        process_handle_ = nullptr;
        output_read_handle_ = nullptr;

    }

    core::Result<CrashTestHarness> CrashTestHarness::start(
        std::filesystem::path worker_path,
        std::filesystem::path database_directory,
        std::string_view sql
    ) {

        // Create the input pipe (for worker)
        // The parent sends SQL and the worker reads it

        SECURITY_ATTRIBUTES security_attributes{};
        security_attributes.nLength = sizeof(security_attributes);
        security_attributes.bInheritHandle = TRUE;

        HANDLE worker_input_read_raw = nullptr;
        HANDLE parent_input_write_raw = nullptr;

        const BOOL pipe_created = CreatePipe(
            &worker_input_read_raw,
            &parent_input_write_raw,
            &security_attributes,
            0
        );
        if(!pipe_created) return windows_error("Cannot create crash worker input pipe");

        ScopedHandle worker_input_read{ worker_input_read_raw };
        ScopedHandle parent_input_write{ parent_input_write_raw };

        const BOOL parent_write_configured = SetHandleInformation(
            static_cast<HANDLE>(parent_input_write.get()),
            HANDLE_FLAG_INHERIT,
            0
        );
        if(!parent_write_configured) return windows_error("Cannot configure crash worker input pipe");

        // Create the output pipe (from worker)
        // The worker writes READY and the parent reads it

        HANDLE parent_output_read_raw = nullptr;
        HANDLE worker_output_write_raw = nullptr;

        const BOOL output_pipe_created = CreatePipe(
            &parent_output_read_raw,
            &worker_output_write_raw,
            &security_attributes,
            0
        );
        if(!output_pipe_created) return windows_error("Cannot create crash worker output pipe");

        ScopedHandle parent_output_read{ parent_output_read_raw };
        ScopedHandle worker_output_write{ worker_output_write_raw };

        const BOOL parent_read_configured = SetHandleInformation(
            static_cast<HANDLE>(parent_output_read.get()),
            HANDLE_FLAG_INHERIT,
            0
        );
        if(!parent_read_configured) return windows_error("Cannot configure crash worker output pipe");

        // Start the worker process
        // The worker inherits the input and output pipes

        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESTDHANDLES;
        startup_info.hStdInput = static_cast<HANDLE>(worker_input_read.get());
        startup_info.hStdOutput = static_cast<HANDLE>(worker_output_write.get());
        startup_info.hStdError = static_cast<HANDLE>(worker_output_write.get());

        PROCESS_INFORMATION process_info{};
        const BOOL worker_started = CreateProcessW(
            worker_path.c_str(),
            nullptr,
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            database_directory.c_str(),
            &startup_info,
            &process_info
        );
        if(!worker_started) return windows_error("Cannot start crash worker");

        ScopedHandle process_handle{ process_info.hProcess };
        ScopedHandle worker_thread{ process_info.hThread };

        // Close the parent copies of the worker-only pipe ends
        worker_input_read.close();
        worker_output_write.close();

        core::Status write_status = core::Status::Ok();
        std::size_t written_total = 0;

        while(written_total < sql.size()) {

            const std::size_t bytes_remaining = sql.size()-written_total;
            const DWORD requested_size = static_cast<DWORD>(std::min(
                bytes_remaining,
                static_cast<std::size_t>(std::numeric_limits<DWORD>::max())
            ));

            DWORD bytes_written = 0;
            const BOOL wrote = WriteFile(
                static_cast<HANDLE>(parent_input_write.get()),
                sql.data()+written_total,
                requested_size,
                &bytes_written,
                nullptr
            );

            if(!wrote) {
                write_status = windows_error("Cannot send SQL to crash worker");
                break;
            }

            if(bytes_written == 0) {
                write_status = core::Status::IoError("Cannot send SQL to crash worker: wrote zero bytes");
                break;
            }

            written_total += bytes_written;

        }

        parent_input_write.close();

        if(!write_status.ok()) {

            TerminateProcess(static_cast<HANDLE>(process_handle.get()), 1);
            WaitForSingleObject(static_cast<HANDLE>(process_handle.get()), INFINITE);

            return write_status;
        }

        return CrashTestHarness(process_handle.release(), parent_output_read.release());

    }

    core::Status CrashTestHarness::wait_for_ready(std::chrono::milliseconds timeout) {

        const HANDLE output_read_handle = static_cast<HANDLE>(output_read_handle_);
        if(output_read_handle == nullptr) {
            return core::Status::InternalError("Crash worker output pipe is closed");
        }

        std::string output;
        const auto deadline = std::chrono::steady_clock::now()+timeout;

        while(true) {

            DWORD bytes_available = 0;
            const BOOL output_peeked = PeekNamedPipe(
                output_read_handle,
                nullptr,
                0,
                nullptr,
                &bytes_available,
                nullptr
            );
            if(!output_peeked) return windows_error("Cannot inspect crash worker output");

            if(bytes_available > 0) {

                char character = '\0';
                DWORD bytes_read = 0;
                const BOOL output_read = ReadFile(
                    output_read_handle,
                    &character,
                    1,
                    &bytes_read,
                    nullptr
                );
                if(!output_read) return windows_error("Cannot read crash worker output");

                if(bytes_read == 0) {
                    return core::Status::IoError("Cannot read crash worker output: read zero bytes");
                }

                output += character;

                if(output == "READY") return core::Status::Ok();

                continue;

            }

            const auto now = std::chrono::steady_clock::now();
            if(now >= deadline) return core::Status::IoError("Timed out waiting for crash worker readiness");

            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now);
            std::this_thread::sleep_for(std::min(POLL_INTERVAL, remaining));

        }

    }

    core::Status CrashTestHarness::terminate(std::chrono::milliseconds timeout) {

        const HANDLE process_handle = static_cast<HANDLE>(process_handle_);
        if(process_handle == nullptr) return core::Status::Ok();

        if(timeout < std::chrono::milliseconds::zero()) {
            return core::Status::InvalidArgument("Crash worker termination timeout must not be negative");
        }

        const BOOL worker_terminated = TerminateProcess(process_handle, 1);
        if(!worker_terminated) return windows_error("Cannot terminate crash worker");

        const std::chrono::milliseconds maximum_timeout{ std::numeric_limits<DWORD>::max()-1 };
        const DWORD wait_timeout = static_cast<DWORD>(std::min(timeout, maximum_timeout).count());
        const DWORD wait_result = WaitForSingleObject(process_handle, wait_timeout);

        if(wait_result == WAIT_OBJECT_0) {
            close_handles();
            return core::Status::Ok();
        }

        if(wait_result == WAIT_TIMEOUT) {
            return core::Status::IoError("Timed out waiting for crash worker termination");
        }

        return windows_error("Cannot wait for crash worker termination");

    }

}
