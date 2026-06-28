#include <dandb/core/Checksum.h>

#include <cstddef>
#include <cstdint>

namespace dandb::core {

    // FNV-1a hash

    std::uint64_t checksum(std::span<const std::byte> bytes, std::uint64_t seed) {

        std::uint64_t hash = seed;
        const std::uint64_t fnv_prime = 0x100000001b3ULL;

        for(const std::byte& byte: bytes) {
            hash ^= std::to_integer<std::uint64_t>(byte);
            hash *= fnv_prime;
        }

        return hash;

    }

}