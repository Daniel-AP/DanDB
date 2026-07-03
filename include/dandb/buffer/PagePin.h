#pragma once

#include <dandb/storage/Page.h>

namespace dandb::buffer {

    class BufferPoolManager;

    class PagePin {
        public:
            ~PagePin();

            PagePin(const PagePin&) = delete;
            PagePin& operator=(const PagePin&) = delete;
            PagePin(PagePin&& other) noexcept;
            PagePin& operator=(PagePin&& other) noexcept;

            storage::Page* page();
            const storage::Page* page() const;
            void mark_dirty();
            bool is_dirty() const;

        private:
            friend class BufferPoolManager;

            PagePin(BufferPoolManager* bpm, storage::Page* page);

            void release();

            BufferPoolManager* bpm_;
            storage::Page* page_;
            bool is_dirty_ = false;
            bool owns_pin_ = true;
    };

}
