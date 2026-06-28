#include <testutil/TempDir.h>

#include <filesystem>
#include <stdexcept>
#include <random>
#include <chrono>
#include <string>
#include <system_error>

namespace {

    std::filesystem::path create_unique_temp_dir() {

        std::filesystem::path root = std::filesystem::temp_directory_path()/"dandb_test";

        std::error_code error;
        std::filesystem::create_directories(root, error);

        if(error) {
            throw std::runtime_error("Cannot create DanDB test temp root folder");
        }

        std::random_device random_device;

        for(int i = 0; i < 100; i++) {

            const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto candidate = root/(
                "temp_"+
                std::to_string(timestamp)+
                "_"+
                std::to_string(random_device())+
                "_"+
                std::to_string(i)
            );

            error.clear();

            if(std::filesystem::create_directory(candidate, error)) return candidate;
            if(error) throw std::runtime_error("Cannot create DanDB test temp subdirectory");

        }

        throw std::runtime_error("Cannot create DanDB test temp subdirectory: no unique path found");

    }

}

namespace dandb::testutil {

    TempDir::TempDir() : path_(create_unique_temp_dir()) {}

    TempDir::~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& TempDir::path() const {
        return path_;
    }

    std::filesystem::path TempDir::database_path() const {
        return path_/"test.dandb";
    }

    std::filesystem::path TempDir::wal_path() const {
        return path_/"test.dandb.wal";
    }

}