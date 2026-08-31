#pragma once

namespace dandb::testutil {

    class ScopedHandle {
        public:
            ScopedHandle() = default;
            explicit ScopedHandle(void* handle);
            ScopedHandle(const ScopedHandle&) = delete;
            ScopedHandle& operator=(const ScopedHandle&) = delete;
            ScopedHandle(ScopedHandle&& other) noexcept;
            ScopedHandle& operator=(ScopedHandle&& other) noexcept;
            ~ScopedHandle();

            void* get() const;
            void close();
            void* release();

        private:
            void* handle_ = nullptr;
    };

}
