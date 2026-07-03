#pragma once

#include <dandb/buffer/PagePin.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Page.h>

namespace dandb::storage {

    class Pager;

    class PageHandle {
        public:
            PageHandle(const PageHandle&) = delete;
            PageHandle& operator=(const PageHandle&) = delete;
            PageHandle(PageHandle&&) noexcept = default;
            PageHandle& operator=(PageHandle&&) noexcept = default;

            Page* page();
            const Page* page() const;
            core::Status mark_dirty();
            bool is_dirty() const;

        private:
            friend class Pager;

            PageHandle(Pager* pager, buffer::PagePin page_pin);

            Pager* pager_;
            buffer::PagePin page_pin_;
    };

}
