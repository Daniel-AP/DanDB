#pragma once

#include <dandb/storage/Page.h>

namespace dandb::buffer {

    class BufferPoolManager;

    class PageGuard {
        public:
            ~PageGuard();

            PageGuard(const PageGuard&) = delete;
            PageGuard& operator=(const PageGuard&) = delete;
            PageGuard(PageGuard&& other) noexcept;
            PageGuard& operator=(PageGuard&& other) noexcept;

            storage::Page* page();
            const storage::Page* page() const;
            void mark_dirty();
            bool is_dirty() const;

        private:
            friend class BufferPoolManager;

            PageGuard(BufferPoolManager* bpm, storage::Page* page);

            void release();

            BufferPoolManager* bpm_;
            storage::Page* page_;
            bool is_dirty_ = false;
            bool owns_pin_ = true;
    };

}
