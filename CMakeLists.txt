cmake_minimum_required(VERSION 3.20)

project(
    DanDB
    VERSION 0.0.0
    DESCRIPTION "A learning-focused mini relational database engine"
    LANGUAGES CXX
)

add_library(dandb_core STATIC
    src/core/Status.cpp
    src/core/Endian.cpp
    src/core/Checksum.cpp
    src/platform/FileHandle.cpp
    src/platform/FileLock.cpp
    src/platform/DatabasePath.cpp
    src/storage/DatabaseHeader.cpp
    src/core/Bytes.cpp
    src/storage/Page.cpp
    src/storage/DiskManager.cpp
    src/wal/WalHeader.cpp
)

target_compile_features(dandb_core PUBLIC cxx_std_20)

target_include_directories(dandb_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_executable(dandb_cli
    src/cli/Main.cpp
)

target_link_libraries(dandb_cli
    PRIVATE
        dandb_core
)

enable_testing()

add_subdirectory(third_party/catch2)

add_executable(dandb_tests
    tests/core/Result.cpp
    tests/core/Status.cpp
    tests/core/Constants.cpp
    tests/core/Endian.cpp
    tests/core/Checksum.cpp
    tests/catalog/TableId.cpp
    tests/catalog/IndexId.cpp
    tests/storage/PageId.cpp
    tests/testutil/TempDir.cpp
    tests/testutil/TempDirTests.cpp
    tests/platform/DatabasePath.cpp
    tests/platform/FileHandle.cpp
    tests/platform/FileLock.cpp
    tests/storage/DatabaseHeader.cpp
    tests/storage/Page.cpp
    tests/storage/DiskManager.cpp
    tests/wal/WalHeader.cpp
)

target_link_libraries(dandb_tests
    PRIVATE
        dandb_core
        Catch2::Catch2WithMain
)

target_include_directories(dandb_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
)

target_compile_features(dandb_tests PRIVATE cxx_std_20)

add_test(NAME dandb_tests COMMAND dandb_tests)
