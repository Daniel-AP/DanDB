#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>

#include <cstdint>
#include <cstddef>
#include <span>

namespace dandb::wal {

    inline constexpr std::uint32_t WAL_COMMIT_RECORD_TYPE = 2;
    inline constexpr std::uint32_t WAL_COMMIT_RECORD_SIZE = 32;

    class WalCommitRecord {
        public:
            static core::Result<WalCommitRecord> decode(std::span<const std::byte> bytes);
            static WalCommitRecord create_new(
                std::uint64_t transaction_id,
                std::uint64_t frame_count
            );

            core::Status encode_into(std::span<std::byte> out) const;

            std::uint64_t transaction_id() const;
            std::uint64_t frame_count() const;

        private:
            WalCommitRecord(
                std::uint64_t transaction_id,
                std::uint64_t frame_count
            );

            std::uint64_t transaction_id_;
            std::uint64_t frame_count_;
    };

}