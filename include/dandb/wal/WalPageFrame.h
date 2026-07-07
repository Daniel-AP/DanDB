#pragma once

#include <dandb/core/Constants.h>
#include <dandb/storage/PageId.h>
#include <dandb/core/Status.h>
#include <dandb/core/Result.h>

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace dandb::wal {

    inline constexpr std::uint32_t WAL_PAGE_FRAME_RECORD_TYPE = 1;
    inline constexpr std::uint32_t WAL_PAGE_FRAME_RECORD_METADATA_SIZE = 40;
    inline constexpr std::uint32_t WAL_PAGE_FRAME_RECORD_SIZE = core::PAGE_SIZE+WAL_PAGE_FRAME_RECORD_METADATA_SIZE;

    class WalPageFrame {
        public:
            static core::Result<WalPageFrame> decode(std::span<const std::byte> bytes);
            static WalPageFrame create_new(
                std::uint64_t transaction_id,
                storage::PageId page_id,
                const std::array<std::byte, core::PAGE_SIZE>& page_image
            );

            core::Status encode_into(std::span<std::byte> out) const;

            std::uint64_t transaction_id() const;
            storage::PageId page_id() const;
            const std::array<std::byte, core::PAGE_SIZE>& page_image() const;

        private:
            WalPageFrame(
                std::uint64_t transaction_id,
                storage::PageId page_id,
                std::array<std::byte, core::PAGE_SIZE> page_image
            );

            std::uint64_t transaction_id_;
            storage::PageId page_id_;
            std::array<std::byte, core::PAGE_SIZE> page_image_;
    };

}
