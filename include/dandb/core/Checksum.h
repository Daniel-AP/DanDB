#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

namespace dandb::core {

    std::uint64_t checksum(std::span<const std::byte> bytes, std::uint64_t seed = 0xcbf29ce484222325ULL);

}