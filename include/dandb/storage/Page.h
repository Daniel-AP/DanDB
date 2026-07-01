#pragma once

#include <dandb/storage/PageId.h>
#include <dandb/core/Constants.h>

#include <array>
#include <cstddef>

namespace dandb::storage {

    class Page {
        public:
            Page(PageId id = INVALID_PAGE_ID);

            PageId id() const;
            void set_id(PageId new_id);
            
            std::array<std::byte, core::PAGE_SIZE>& data();
            const std::array<std::byte, core::PAGE_SIZE>& data() const;

        private:
            PageId id_ = INVALID_PAGE_ID;
            std::array<std::byte, core::PAGE_SIZE> data_;
    };

}