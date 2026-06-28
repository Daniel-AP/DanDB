#include <dandb/core/Endian.h>

#include <cstdint>
#include <cstddef>

namespace dandb::core {

    Status write_u16_le(std::span<std::byte> buffer, std::size_t offset, std::uint16_t value) {
        
        if(offset >= buffer.size() || sizeof(std::uint16_t) > buffer.size()-offset) {
            return Status::InvalidArgument("Cannot write 16-bit integer to buffer: write bounds failure");
        }

        for(std::size_t i = 0; i < sizeof(std::uint16_t); i++) {
            buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
        }

        return Status::Ok();

    }

    Status write_u32_le(std::span<std::byte> buffer, std::size_t offset, std::uint32_t value) {
        
        if(offset >= buffer.size() || sizeof(std::uint32_t) > buffer.size()-offset) {
            return Status::InvalidArgument("Cannot write 32-bit integer to buffer: write bounds failure");
        }

        for(std::size_t i = 0; i < sizeof(std::uint32_t); i++) {
            buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
        }

        return Status::Ok();

    }

    Status write_u64_le(std::span<std::byte> buffer, std::size_t offset, std::uint64_t value) {
        
        if(offset >= buffer.size() || sizeof(std::uint64_t) > buffer.size()-offset) {
            return Status::InvalidArgument("Cannot write 64-bit integer to buffer: write bounds failure");
        }

        for(std::size_t i = 0; i < sizeof(std::uint64_t); i++) {
            buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
        }

        return Status::Ok();

    }

    Result<std::uint16_t> read_u16_le(std::span<const std::byte> buffer, std::size_t offset) {

        if(offset >= buffer.size() || sizeof(std::uint16_t) > buffer.size()-offset) {
            return Status::InvalidArgument("Cannot read 16-bit integer to buffer: read bounds failure");
        }

        std::uint16_t res = 0;

        for(std::size_t i = 0; i < sizeof(std::uint16_t); i++) {
            res |= std::to_integer<std::uint16_t>(buffer[offset+i])<<(8*i);
        }

        return res;

    }

    Result<std::uint32_t> read_u32_le(std::span<const std::byte> buffer, std::size_t offset) {

        if(offset >= buffer.size() || sizeof(std::uint32_t) > buffer.size()-offset) {
            return Status::InvalidArgument("Cannot read 32-bit integer to buffer: read bounds failure");
        }

        std::uint32_t res = 0;

        for(std::size_t i = 0; i < sizeof(std::uint32_t); i++) {
            res |= std::to_integer<std::uint32_t>(buffer[offset+i])<<(8*i);
        }

        return res;

    }

    Result<std::uint64_t> read_u64_le(std::span<const std::byte> buffer, std::size_t offset) {

        if(offset >= buffer.size() || sizeof(std::uint64_t) > buffer.size()-offset) {
            return Status::InvalidArgument("Cannot read 64-bit integer to buffer: read bounds failure");
        }

        std::uint64_t res = 0;

        for(std::size_t i = 0; i < sizeof(std::uint64_t); i++) {
            res |= std::to_integer<std::uint64_t>(buffer[offset+i])<<(8*i);
        }

        return res;

    }

}