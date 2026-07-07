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
    src/storage/PageHandle.cpp
    src/storage/Pager.cpp
    src/transaction/TransactionState.cpp
    src/wal/WalHeader.cpp
    src/wal/WalPageFrame.cpp
    src/wal/WalCommitRecord.cpp
    src/wal/WalManager.cpp
    src/wal/WalScanner.cpp
    src/buffer/LRUReplacer.cpp
    src/buffer/PagePin.cpp
    src/buffer/BufferPoolManager.cpp
    src/record/Schema.cpp
    src/record/Column.cpp
    src/record/Row.cpp
    src/record/RowValidation.cpp
    src/record/RowCodec.cpp
    src/record/KeyCodec.cpp
    src/record/RowHelpers.cpp
    src/record/LiteralValue.cpp
    src/record/LogicalType.cpp
    src/record/Value.cpp
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
    tests/record/Column.cpp
    tests/record/LiteralValue.cpp
    tests/record/LogicalType.cpp
    tests/record/KeyCodec.cpp
    tests/record/Row.cpp
    tests/record/RowCodec.cpp
    tests/record/RowHelpers.cpp
    tests/record/Schema.cpp
    tests/record/Value.cpp
    tests/storage/PageId.cpp
    tests/testutil/TempDir.cpp
    tests/testutil/TempDirTests.cpp
    tests/platform/DatabasePath.cpp
    tests/platform/FileHandle.cpp
    tests/platform/FileLock.cpp
    tests/storage/DatabaseHeader.cpp
    tests/storage/Page.cpp
    tests/storage/DiskManager.cpp
    tests/storage/Pager.cpp
    tests/buffer/BufferFrame.cpp
    tests/buffer/LRUReplacer.cpp
    tests/buffer/BufferPoolManager.cpp
    tests/wal/WalHeader.cpp
    tests/wal/WalPageFrame.cpp
    tests/wal/WalCommitRecord.cpp
    tests/wal/WalManager.cpp
    tests/wal/WalScanner.cpp
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
