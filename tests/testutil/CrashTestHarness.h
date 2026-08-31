#pragma once

#include <dandb/core/Result.h>
#include <dandb/core/Status.h>

#include <chrono>
#include <filesystem>
#include <string_view>

namespace dandb::testutil {

    class CrashTestHarness {
        public:
            CrashTestHarness(const CrashTestHarness&) = delete;
            CrashTestHarness& operator=(const CrashTestHarness&) = delete;
            CrashTestHarness(CrashTestHarness&& other) noexcept;
            CrashTestHarness& operator=(CrashTestHarness&& other) noexcept;
            ~CrashTestHarness();

            static core::Result<CrashTestHarness> start(
                std::filesystem::path worker_path,
                std::filesystem::path database_directory,
                std::string_view sql
            );

            core::Status wait_for_ready(std::chrono::milliseconds timeout);
            core::Status terminate(std::chrono::milliseconds timeout);

        private:
            CrashTestHarness(void* process_handle, void* output_read_handle);
            void close_handles();

            void* process_handle_ = nullptr;
            void* output_read_handle_ = nullptr;
    };

}
