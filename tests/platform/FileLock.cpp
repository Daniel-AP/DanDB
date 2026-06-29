#include <catch_amalgamated.hpp>

#include <testutil/TempDir.h>
#include <dandb/core/Status.h>
#include <dandb/platform/FileHandle.h>
#include <dandb/platform/FileLock.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

namespace {

    void create_database_file(const std::filesystem::path& path) {
        auto created = dandb::platform::FileHandle::create_new(path);
        REQUIRE(created.ok());
    }

}

TEST_CASE("FileLock rejects a second exclusive lock on the same database file", "[platform][file_lock]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.database_path();

    create_database_file(path);

    auto first = dandb::platform::FileLock::acquire_exclusive(path);
    REQUIRE(first.ok());

    auto second = dandb::platform::FileLock::acquire_exclusive(path);

    REQUIRE_FALSE(second.ok());
    REQUIRE(second.status().code() == dandb::core::StatusCode::IoError);
    REQUIRE(second.status().message().find("locked") != std::string::npos);
}

TEST_CASE("FileLock reports locked state and path while held", "[platform][file_lock]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.database_path();
    create_database_file(path);

    auto lock = dandb::platform::FileLock::acquire_exclusive(path);

    REQUIRE(lock.ok());
    REQUIRE(lock.value().is_locked());
    REQUIRE(lock.value().path() == path);
}

TEST_CASE("FileLock close releases an exclusive lock", "[platform][file_lock]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.database_path();
    create_database_file(path);

    auto first = dandb::platform::FileLock::acquire_exclusive(path);
    REQUIRE(first.ok());

    const auto close_status = first.value().close();
    REQUIRE(close_status.ok());
    REQUIRE_FALSE(first.value().is_locked());

    auto second = dandb::platform::FileLock::acquire_exclusive(path);
    REQUIRE(second.ok());
}

TEST_CASE("FileLock close is safe to call more than once", "[platform][file_lock]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.database_path();
    create_database_file(path);

    auto lock = dandb::platform::FileLock::acquire_exclusive(path);
    REQUIRE(lock.ok());

    const auto first_close = lock.value().close();
    REQUIRE(first_close.ok());

    const auto second_close = lock.value().close();
    REQUIRE(second_close.ok());
    REQUIRE_FALSE(lock.value().is_locked());
}

TEST_CASE("FileLock destructor releases an exclusive lock", "[platform][file_lock]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.database_path();
    create_database_file(path);

    {
        auto first = dandb::platform::FileLock::acquire_exclusive(path);
        REQUIRE(first.ok());
    }

    auto second = dandb::platform::FileLock::acquire_exclusive(path);
    REQUIRE(second.ok());
}

TEST_CASE("FileLock rejects a missing database file", "[platform][file_lock]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.database_path();

    auto lock = dandb::platform::FileLock::acquire_exclusive(path);

    REQUIRE_FALSE(lock.ok());
    REQUIRE(lock.status().code() == dandb::core::StatusCode::NotFound);
}

TEST_CASE("FileLock move constructor transfers the exclusive lock", "[platform][file_lock]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.database_path();
    create_database_file(path);

    auto original = dandb::platform::FileLock::acquire_exclusive(path);
    REQUIRE(original.ok());

    auto moved = std::move(original.value());

    REQUIRE_FALSE(original.value().is_locked());
    REQUIRE(moved.is_locked());

    auto second = dandb::platform::FileLock::acquire_exclusive(path);
    REQUIRE_FALSE(second.ok());
    REQUIRE(second.status().code() == dandb::core::StatusCode::IoError);
}

TEST_CASE("FileLock move assignment releases the previous lock and transfers the new one", "[platform][file_lock]") {
    const dandb::testutil::TempDir temp_dir;
    const auto first_path = temp_dir.path() / "first.dandb";
    const auto second_path = temp_dir.path() / "second.dandb";
    create_database_file(first_path);
    create_database_file(second_path);

    auto first = dandb::platform::FileLock::acquire_exclusive(first_path);
    REQUIRE(first.ok());

    auto second = dandb::platform::FileLock::acquire_exclusive(second_path);
    REQUIRE(second.ok());

    second.value() = std::move(first.value());

    REQUIRE_FALSE(first.value().is_locked());
    REQUIRE(second.value().is_locked());
    REQUIRE(second.value().path() == first_path);

    auto old_second_path_lock = dandb::platform::FileLock::acquire_exclusive(second_path);
    REQUIRE(old_second_path_lock.ok());

    auto old_first_path_lock = dandb::platform::FileLock::acquire_exclusive(first_path);
    REQUIRE_FALSE(old_first_path_lock.ok());
    REQUIRE(old_first_path_lock.status().code() == dandb::core::StatusCode::IoError);
}

TEST_CASE("FileLock does not block normal file handle I/O", "[platform][file_lock]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.database_path();
    create_database_file(path);

    auto lock = dandb::platform::FileLock::acquire_exclusive(path);
    REQUIRE(lock.ok());

    auto file = dandb::platform::FileHandle::open_existing(path);
    REQUIRE(file.ok());

    const std::array data{
        static_cast<std::byte>('d'),
        static_cast<std::byte>('b')
    };

    const auto write_status = file.value().write_at(0, data);
    REQUIRE(write_status.ok());

    std::array<std::byte, 2> out{};
    const auto read_status = file.value().read_at(0, out);

    REQUIRE(read_status.ok());
    REQUIRE(out == data);
}
