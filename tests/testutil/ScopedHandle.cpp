#include <testutil/ScopedHandle.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <utility>

namespace dandb::testutil {

    ScopedHandle::ScopedHandle(void* handle) : handle_(handle) {}

    ScopedHandle::ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.release()) {}

    ScopedHandle& ScopedHandle::operator=(ScopedHandle&& other) noexcept {

        if(this == &other) return *this;

        close();
        handle_ = other.release();

        return *this;
        
    }

    ScopedHandle::~ScopedHandle() { close(); }

    void* ScopedHandle::get() const { return handle_; }

    void ScopedHandle::close() {

        if(handle_ == nullptr) return;

        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;

    }

    void* ScopedHandle::release() { return std::exchange(handle_, nullptr); }

}
