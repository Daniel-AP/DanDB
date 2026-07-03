#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>

#include <list>
#include <unordered_map>
#include <cstddef>

namespace dandb::buffer {

    class LRUReplacer {
        public:
            explicit LRUReplacer(std::size_t capacity);

            LRUReplacer(const LRUReplacer&) = delete;
            LRUReplacer& operator=(const LRUReplacer&) = delete;
            LRUReplacer(LRUReplacer&&) noexcept = default;
            LRUReplacer& operator=(LRUReplacer&&) noexcept = default;

            core::Result<std::size_t> victim();

            core::Status mark_evictable(std::size_t frame_id);
            core::Status mark_non_evictable(std::size_t frame_id);

            std::size_t capacity() const;
            std::size_t size() const;

        private:
            std::list<std::size_t> lru_; // LRU = back, MRU = front
            std::unordered_map<std::size_t, std::list<std::size_t>::iterator> frame_positions_;
            std::size_t capacity_;
    };

}