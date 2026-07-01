#pragma once

#include <dandb/core/Result.h>
#include <dandb/core/Status.h>

#include <array>
#include <span>
#include <cstddef>
#include <cstdint>

namespace dandb::wal {

    inline constexpr std::array<std::byte, 4> WAL_MAGIC_BYTES{
        std::byte{ 'D' },
        std::byte{ 'W' },
        std::byte{ 'A' },
        std::byte{ 'L' },
    };

    inline constexpr std::uint32_t WAL_FORMAT_VERSION = 1;
    inline constexpr std::uint32_t WAL_HEADER_SIZE = 64;

    class WalHeader {
        public:
            static core::Result<WalHeader> decode(std::span<const std::byte> bytes);
            static WalHeader create_new(std::uint64_t database_id);

            core::Status encode_into(std::span<std::byte> out) const;

            std::uint64_t database_id() const;
        private:
            WalHeader(std::uint64_t database_id);

            std::uint64_t database_id_;
    };

}
