#pragma once

#include <span>
#include <cstddef>

namespace dandb::core {

    bool bytes_are_zero(std::span<const std::byte> bytes);

}