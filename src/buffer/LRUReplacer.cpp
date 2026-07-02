#include <dandb/buffer/LRUReplacer.h>

#include <dandb/core/Status.h>

#include <string>

namespace dandb::buffer {

    LRUReplacer::LRUReplacer(std::size_t capacity) : capacity_(capacity) {}

    core::Result<std::size_t> LRUReplacer::victim() {

        if(lru_.empty()) {
            return core::Status::NotFound("Cannot choose LRU victim: no evictable frames");
        }

        std::size_t victim_frame_id = lru_.back();
        lru_.pop_back();
        frame_positions_.erase(victim_frame_id);

        return victim_frame_id;

    }

    core::Status LRUReplacer::mark_evictable(std::size_t frame_id) {

        if(frame_id >= capacity_) {
            return core::Status::InvalidArgument("Cannot mark frame "+std::to_string(frame_id)+" as evictable: frame does not exist");
        }

        if(frame_positions_.find(frame_id) != frame_positions_.end()) {
            return core::Status::Ok();
        }

        lru_.push_front(frame_id);
        frame_positions_[frame_id] = lru_.begin();

        return core::Status::Ok();

    }
 
    core::Status LRUReplacer::mark_non_evictable(std::size_t frame_id) {

        if(frame_id >= capacity_) {
            return core::Status::InvalidArgument("Cannot mark frame "+std::to_string(frame_id)+" as non-evictable: frame does not exist");
        }

        auto it = frame_positions_.find(frame_id);

        if(it == frame_positions_.end()) {
            return core::Status::Ok();
        }

        lru_.erase(it->second);
        frame_positions_.erase(frame_id);

        return core::Status::Ok();

    }
    
    std::size_t LRUReplacer::capacity() const {
        return capacity_;
    }
    
    std::size_t LRUReplacer::size() const {
        return lru_.size();
    }

}