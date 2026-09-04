#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace dandb::platform {

    std::string windows_error_message(
        std::string_view action,
        const std::filesystem::path& path,
        std::uint32_t error
    );

}
