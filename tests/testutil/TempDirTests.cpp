#include <catch_amalgamated.hpp>

#include <testutil/TempDir.h>

#include <filesystem>
#include <fstream>

TEST_CASE("TempDir creates a temporary directory", "[testutil][tempdir]") {
    const dandb::testutil::TempDir temp_dir;

    REQUIRE(std::filesystem::exists(temp_dir.path()));
    REQUIRE(std::filesystem::is_directory(temp_dir.path()));
}

TEST_CASE("TempDir creates a unique directory for each instance", "[testutil][tempdir]") {
    const dandb::testutil::TempDir first;
    const dandb::testutil::TempDir second;

    REQUIRE(first.path() != second.path());
    REQUIRE(std::filesystem::exists(first.path()));
    REQUIRE(std::filesystem::exists(second.path()));
}

TEST_CASE("TempDir cleanup removes files created inside the temporary directory", "[testutil][tempdir]") {
    std::filesystem::path temp_path;

    {
        const dandb::testutil::TempDir temp_dir;
        temp_path = temp_dir.path();

        const auto file_path = temp_path / "created.txt";

        {
            std::ofstream file(file_path);
            REQUIRE(file.is_open());

            file << "temporary data";
        }

        REQUIRE(std::filesystem::exists(file_path));
    }

    REQUIRE_FALSE(std::filesystem::exists(temp_path));
}

TEST_CASE("TempDir provides standard database and WAL paths", "[testutil][tempdir]") {
    const dandb::testutil::TempDir temp_dir;

    REQUIRE(temp_dir.database_path() == temp_dir.path() / "test.dandb");
    REQUIRE(temp_dir.wal_path() == temp_dir.path() / "test.dandb.wal");
}
