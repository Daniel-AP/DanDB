#include <dandb/buffer/BufferPoolManager.h>

#include <dandb/storage/Page.h>
#include <dandb/storage/PageId.h>
#include <dandb/buffer/BufferFrame.h>

#include <cstddef>

namespace dandb::buffer {

    BufferPoolManager::BufferPoolManager(std::size_t capacity) :
        capacity_(capacity),
        free_frame_ids_(capacity_),
        frames_(capacity_),
        lru_(capacity_)
    {
        for(std::size_t i = 0; i < capacity_; i++) free_frame_ids_[i] = i;
    }

    core::Status BufferPoolManager::pin_page(storage::PageId page_id) {

        if(page_id == storage::INVALID_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot pin page: invalid page id");
        }

        auto it = page_frames_.find(page_id);

        if(it == page_frames_.end()) {
            return core::Status::NotFound("Cannot pin page: page is not cached");
        }
        
        auto mark_non_evictable_status = lru_.mark_non_evictable(it->second);
        if(!mark_non_evictable_status.ok()) {
            return mark_non_evictable_status;
        }

        frames_[it->second].pin();
        
        return core::Status::Ok();

    }

    core::Status BufferPoolManager::unpin_page(storage::PageId page_id, bool is_dirty) {

        if(page_id == storage::INVALID_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot unpin page: invalid page id");
        }

        auto it = page_frames_.find(page_id);

        if(it == page_frames_.end()) {
            return core::Status::NotFound("Cannot unpin page: page is not cached");
        }

        auto unpin_status = frames_[it->second].unpin();
        if(!unpin_status.ok()) {
            return unpin_status;
        }

        if(is_dirty) frames_[it->second].mark_dirty();

        if(!frames_[it->second].is_pinned() && !frames_[it->second].is_dirty()) {
            return lru_.mark_evictable(it->second);
        }

        return core::Status::Ok();

    }

    core::Result<PagePin> BufferPoolManager::get_page(storage::PageId page_id) {

        if(page_id == storage::INVALID_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot get page: invalid page id");
        }

        auto it = page_frames_.find(page_id);

        if(it == page_frames_.end()) {
            return core::Status::NotFound("Cannot get page: page is not cached");
        }

        auto pin_page_status = pin_page(page_id);
        if(!pin_page_status.ok()) {
            return pin_page_status;
        }

        storage::Page& page = frames_[it->second].page();

        return PagePin{
            this,
            &page
        };

    }

    core::Result<PagePin> BufferPoolManager::cache_page(const storage::Page& page) {

        if(page.id() == storage::INVALID_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot cache page: invalid page id");
        }

        if(page_frames_.find(page.id()) != page_frames_.end()) {
            return core::Status::AlreadyExists("Cannot cache page: page is already cached");
        }

        std::size_t victim_frame_id;

        if(!free_frame_ids_.empty()) {
            victim_frame_id = free_frame_ids_.back();
            free_frame_ids_.pop_back();
        } else {
            auto victim_result = lru_.victim();
            if(!victim_result.ok()) {
                return victim_result.status();
            }
            victim_frame_id = victim_result.value();
        }

        BufferFrame& buffer_frame = frames_[victim_frame_id];
        storage::Page& buffer_page = buffer_frame.page();

        page_frames_.erase(buffer_page.id());

        buffer_frame.reset();
        buffer_page = page;
        buffer_frame.pin();

        page_frames_[buffer_page.id()] = victim_frame_id;

        return PagePin{
            this,
            &buffer_page
        };

    }

    core::Status BufferPoolManager::discard_page(storage::PageId page_id) {

        if(page_id == storage::INVALID_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot discard page: invalid page id");
        }

        auto it = page_frames_.find(page_id);

        if(it == page_frames_.end()) {
            return core::Status::NotFound("Cannot discard page: page is not cached");
        }

        std::size_t frame_id = it->second;
        BufferFrame& buffer_frame = frames_[frame_id];

        if(buffer_frame.is_pinned()) {
            return core::Status::InternalError("Cannot discard page: page is pinned");
        }

        auto mark_non_evictable_status = lru_.mark_non_evictable(frame_id);
        if(!mark_non_evictable_status.ok()) {
            return mark_non_evictable_status;
        }

        page_frames_.erase(it);
        buffer_frame.reset();
        free_frame_ids_.push_back(frame_id);

        return core::Status::Ok();

    }

}
