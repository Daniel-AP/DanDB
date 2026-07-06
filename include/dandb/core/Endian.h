#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>

#include <span>
#include <cstddef>
#include <cstdint>

namespace dandb::core {

    Status write_u16_le(std::span<std::byte> buffer, std::size_t offset, std::uint16_t value);
    Status write_u32_le(std::span<std::byte> buffer, std::size_t offset, std::uint32_t value);
    Status write_u64_le(std::span<std::byte> buffer, std::size_t offset, std::uint64_t value);
    Status write_u16_be(std::span<std::byte> buffer, std::size_t offset, std::uint16_t value);
    Status write_u32_be(std::span<std::byte> buffer, std::size_t offset, std::uint32_t value);
    Status write_u64_be(std::span<std::byte> buffer, std::size_t offset, std::uint64_t value);

    Result<std::uint16_t> read_u16_le(std::span<const std::byte> buffer, std::size_t offset);
    Result<std::uint32_t> read_u32_le(std::span<const std::byte> buffer, std::size_t offset);
    Result<std::uint64_t> read_u64_le(std::span<const std::byte> buffer, std::size_t offset);

}
