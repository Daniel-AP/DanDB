#include <catch_amalgamated.hpp>

#include <testutil/TempDir.h>
#include <dandb/platform/FileHandle.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

namespace {

    void write_file_bytes(const std::filesystem::path& path, std::string_view contents) {
        std::ofstream file(path, std::ios::binary);
        REQUIRE(file.is_open());

        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));

        REQUIRE(file.good());
    }

    unsigned char byte_value(std::byte value) {
        return std::to_integer<unsigned char>(value);
    }

    std::string read_file_bytes(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.is_open());

        return std::string(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()
        );
    }

}

TEST_CASE("FileHandle creates a new file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "created.bin";

    auto result = dandb::platform::FileHandle::create_new(path);

    REQUIRE(result.ok());
    REQUIRE(std::filesystem::exists(path));
}

TEST_CASE("FileHandle create_new rejects an existing file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "existing.bin";

    auto first = dandb::platform::FileHandle::create_new(path);
    REQUIRE(first.ok());

    auto second = dandb::platform::FileHandle::create_new(path);

    REQUIRE_FALSE(second.ok());
    REQUIRE(second.status().code() == dandb::core::StatusCode::AlreadyExists);
    REQUIRE(std::filesystem::exists(path));
}

TEST_CASE("FileHandle create_new rejects a path with a missing parent directory", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "missing" / "created.bin";

    auto result = dandb::platform::FileHandle::create_new(path);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE("FileHandle open_existing opens an existing file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "existing.bin";

    {
        auto created = dandb::platform::FileHandle::create_new(path);
        REQUIRE(created.ok());
    }

    auto opened = dandb::platform::FileHandle::open_existing(path);

    REQUIRE(opened.ok());
    REQUIRE(std::filesystem::exists(path));
}

TEST_CASE("FileHandle open_existing rejects a missing file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "missing.bin";

    auto result = dandb::platform::FileHandle::open_existing(path);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE("FileHandle open_or_create creates a missing file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "created_by_open_or_create.bin";

    auto result = dandb::platform::FileHandle::open_or_create(path);

    REQUIRE(result.ok());
    REQUIRE(std::filesystem::exists(path));
}

TEST_CASE("FileHandle open_or_create opens an existing file without truncating it", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "existing_open_or_create.bin";
    constexpr std::uintmax_t ORIGINAL_SIZE = 7;

    {
        auto created = dandb::platform::FileHandle::create_new(path);
        REQUIRE(created.ok());
    }

    std::filesystem::resize_file(path, ORIGINAL_SIZE);

    auto opened = dandb::platform::FileHandle::open_or_create(path);

    REQUIRE(opened.ok());
    REQUIRE(std::filesystem::file_size(path) == ORIGINAL_SIZE);
}

TEST_CASE("FileHandle open_or_create rejects a path with a missing parent directory", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "missing" / "created_by_open_or_create.bin";

    auto result = dandb::platform::FileHandle::open_or_create(path);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE("FileHandle read_at reads all requested bytes from a positioned offset", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "read.bin";
    write_file_bytes(path, "abcdef");

    auto opened = dandb::platform::FileHandle::open_existing(path);
    REQUIRE(opened.ok());

    std::array<std::byte, 3> out{};

    auto result = opened.value().read_at(2, out);

    REQUIRE(result.ok());
    REQUIRE(byte_value(out[0]) == static_cast<unsigned char>('c'));
    REQUIRE(byte_value(out[1]) == static_cast<unsigned char>('d'));
    REQUIRE(byte_value(out[2]) == static_cast<unsigned char>('e'));
}

TEST_CASE("FileHandle read_at returns an error for a short read at end of file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "short_read.bin";
    write_file_bytes(path, "abc");

    auto opened = dandb::platform::FileHandle::open_existing(path);
    REQUIRE(opened.ok());

    std::array<std::byte, 4> out{};

    auto result = opened.value().read_at(1, out);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.code() == dandb::core::StatusCode::IoError);
}

TEST_CASE("FileHandle read_at returns an error when reading beyond end of file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "beyond_eof.bin";
    write_file_bytes(path, "abc");

    auto opened = dandb::platform::FileHandle::open_existing(path);
    REQUIRE(opened.ok());

    std::array<std::byte, 2> out{};

    auto result = opened.value().read_at(100, out);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.code() == dandb::core::StatusCode::IoError);
}

TEST_CASE("FileHandle write_at writes all bytes at offset zero", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "write.bin";

    auto created = dandb::platform::FileHandle::create_new(path);
    REQUIRE(created.ok());

    const std::array data{
        static_cast<std::byte>('d'),
        static_cast<std::byte>('a'),
        static_cast<std::byte>('t'),
        static_cast<std::byte>('a')
    };

    const auto status = created.value().write_at(0, data);

    REQUIRE(status.ok());
    REQUIRE(read_file_bytes(path) == "data");
}

TEST_CASE("FileHandle write_at writes bytes at a later offset and grows the file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "write_later.bin";
    write_file_bytes(path, "abc");

    auto opened = dandb::platform::FileHandle::open_existing(path);
    REQUIRE(opened.ok());

    const std::array data{
        static_cast<std::byte>('x'),
        static_cast<std::byte>('y')
    };

    const auto status = opened.value().write_at(5, data);

    REQUIRE(status.ok());
    REQUIRE(std::filesystem::file_size(path) == 7);
}

TEST_CASE("FileHandle size returns zero for a new empty file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "empty_size.bin";

    auto created = dandb::platform::FileHandle::create_new(path);
    REQUIRE(created.ok());

    auto result = created.value().size();

    REQUIRE(result.ok());
    REQUIRE(result.value() == 0);
}

TEST_CASE("FileHandle size returns the current file length", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "existing_size.bin";
    write_file_bytes(path, "abcdef");

    auto opened = dandb::platform::FileHandle::open_existing(path);
    REQUIRE(opened.ok());

    auto result = opened.value().size();

    REQUIRE(result.ok());
    REQUIRE(result.value() == 6);
}

TEST_CASE("FileHandle size reflects writes at later offsets", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "write_size.bin";
    write_file_bytes(path, "abc");

    auto opened = dandb::platform::FileHandle::open_existing(path);
    REQUIRE(opened.ok());

    const std::array data{
        static_cast<std::byte>('x'),
        static_cast<std::byte>('y')
    };

    const auto write_status = opened.value().write_at(5, data);
    REQUIRE(write_status.ok());

    auto result = opened.value().size();

    REQUIRE(result.ok());
    REQUIRE(result.value() == 7);
}

TEST_CASE("FileHandle size returns an error after close", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "closed_size.bin";

    auto created = dandb::platform::FileHandle::create_new(path);
    REQUIRE(created.ok());

    const auto close_status = created.value().close();
    REQUIRE(close_status.ok());

    auto result = created.value().size();

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == dandb::core::StatusCode::InvalidArgument);
}

TEST_CASE("FileHandle resize shrinks an existing file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "resize_shrink.bin";
    write_file_bytes(path, "abcdef");

    auto opened = dandb::platform::FileHandle::open_existing(path);
    REQUIRE(opened.ok());

    const auto status = opened.value().resize(3);

    REQUIRE(status.ok());
    REQUIRE(std::filesystem::file_size(path) == 3);
    REQUIRE(read_file_bytes(path) == "abc");
}

TEST_CASE("FileHandle resize extends an existing file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "resize_extend.bin";
    write_file_bytes(path, "abc");

    auto opened = dandb::platform::FileHandle::open_existing(path);
    REQUIRE(opened.ok());

    const auto status = opened.value().resize(6);

    REQUIRE(status.ok());
    REQUIRE(std::filesystem::file_size(path) == 6);
}

TEST_CASE("FileHandle resize to zero empties the file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "resize_zero.bin";
    write_file_bytes(path, "abc");

    auto opened = dandb::platform::FileHandle::open_existing(path);
    REQUIRE(opened.ok());

    const auto status = opened.value().resize(0);

    REQUIRE(status.ok());
    REQUIRE(std::filesystem::file_size(path) == 0);
    REQUIRE(read_file_bytes(path).empty());
}

TEST_CASE("FileHandle resize returns an error after close", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "resize_closed.bin";

    auto created = dandb::platform::FileHandle::create_new(path);
    REQUIRE(created.ok());

    const auto close_status = created.value().close();
    REQUIRE(close_status.ok());

    const auto status = created.value().resize(10);

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == dandb::core::StatusCode::InvalidArgument);
}

TEST_CASE("FileHandle resize rejects sizes that do not fit Windows file offsets", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "resize_too_large.bin";

    auto created = dandb::platform::FileHandle::create_new(path);
    REQUIRE(created.ok());

    const auto status = created.value().resize(std::numeric_limits<std::uint64_t>::max());

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == dandb::core::StatusCode::InvalidArgument);
}

TEST_CASE("FileHandle sync succeeds for an open writable file", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "sync.bin";

    auto created = dandb::platform::FileHandle::create_new(path);
    REQUIRE(created.ok());

    const std::array data{
        static_cast<std::byte>('w'),
        static_cast<std::byte>('a'),
        static_cast<std::byte>('l')
    };

    const auto write_status = created.value().write_at(0, data);
    REQUIRE(write_status.ok());

    const auto sync_status = created.value().sync();

    REQUIRE(sync_status.ok());
}

TEST_CASE("FileHandle sync returns an error after close", "[platform][file_handle]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "sync_closed.bin";

    auto created = dandb::platform::FileHandle::create_new(path);
    REQUIRE(created.ok());

    const auto close_status = created.value().close();
    REQUIRE(close_status.ok());

    const auto sync_status = created.value().sync();

    REQUIRE_FALSE(sync_status.ok());
    REQUIRE(sync_status.code() == dandb::core::StatusCode::InvalidArgument);
}
