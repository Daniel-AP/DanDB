#include "windows_error.h"

#include <system_error>

namespace dandb::platform {

    std::string windows_error_message(
        std::string_view action,
        const std::filesystem::path& path,
        std::uint32_t error
    ) {

        const std::error_code error_code(
            static_cast<int>(error),
            std::system_category()
        );

        return std::string(action) +
            " '" + path.string() + "': " +
            error_code.message() +
            " (Windows error " + std::to_string(error) + ")";

    }

}
