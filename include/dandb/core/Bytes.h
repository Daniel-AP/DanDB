#pragma once

#include <span>
#include <cstddef>

namespace dandb::core {

    bool bytes_are_zero(std::span<std::byte> bytes);

}