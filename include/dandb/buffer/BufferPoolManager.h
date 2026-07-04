#pragma once

#include <dandb/buffer/BufferFrame.h>
#include <dandb/buffer/LRUReplacer.h>
#include <dandb/buffer/PagePin.h>
#include <dandb/storage/PageId.h>
#include <dandb/storage/Page.h>
#include <dandb/core/Status.h>
#include <dandb/core/Result.h>

#include <vector>
#include <unordered_map>
#include <cstddef>

namespace dandb::buffer {

    class BufferPoolManager {
        public:
            explicit BufferPoolManager(std::size_t capacity);

            BufferPoolManager(const BufferPoolManager&) = delete;
            BufferPoolManager& operator=(const BufferPoolManager&) = delete;
            BufferPoolManager(BufferPoolManager&&) noexcept = default;
            BufferPoolManager& operator=(BufferPoolManager&&) noexcept = default;

            core::Result<PagePin> get_page(storage::PageId page_id);
            core::Result<PagePin> cache_page(const storage::Page& page);
            core::Status can_discard_page(storage::PageId page_id);
            core::Status discard_page(storage::PageId page_id);
            core::Status can_restore_page(const storage::Page& page);
            core::Status restore_page(const storage::Page& page);

        private:
            friend class PagePin;

            core::Status pin_page(storage::PageId page_id);
            core::Status unpin_page(storage::PageId page_id, bool is_dirty);

            std::size_t capacity_;
            std::vector<std::size_t> free_frame_ids_;
            std::vector<BufferFrame> frames_;
            std::unordered_map<storage::PageId, std::size_t> page_frames_;
            LRUReplacer lru_;
    };

}
