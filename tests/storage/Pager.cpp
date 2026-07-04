#include <catch_amalgamated.hpp>

#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/core/Constants.h>
#include <dandb/platform/FileFaultInjector.h>
#include <dandb/storage/DiskManager.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/PageHandle.h>
#include <dandb/storage/PageId.h>
#include <dandb/storage/Pager.h>
#include <dandb/wal/WalHeader.h>
#include <dandb/wal/WalManager.h>
#include <dandb/wal/WalScanner.h>
#include <testutil/TempDir.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <type_traits>
#include <utility>

using dandb::core::PAGE_SIZE;
using dandb::core::Result;
using dandb::core::Status;
using dandb::core::StatusCode;
using dandb::storage::DiskManager;
using dandb::storage::Page;
using dandb::storage::PageHandle;
using dandb::storage::PageId;
using dandb::storage::Pager;
using dandb::wal::WAL_HEADER_SIZE;
using dandb::wal::WalManager;

namespace {

    constexpr std::uint64_t TRANSACTION_ID = 42;
    constexpr PageId PAGE_ID{ 1 };

    Page make_page(PageId page_id, std::uint8_t seed) {
        Page page(page_id);

        for(std::size_t i = 0; i < PAGE_SIZE; i++) {
            page.data()[i] = static_cast<std::byte>((i + seed) % 251);
        }

        return page;
    }

    bool page_is_zero_filled(const Page& page) {
        for(const auto byte: page.data()) {
            if(byte != std::byte{ 0 }) {
                return false;
            }
        }

        return true;
    }

    class SyncFailureInjector final : public dandb::platform::FileFaultInjector {
        public:
            bool fail_sync = true;

            dandb::core::Status before_sync() override {
                if(fail_sync) {
                    return dandb::core::Status::IoError("injected sync failure");
                }

                return dandb::core::Status::Ok();
            }
    };

}

TEST_CASE("Pager exposes the stable Day 5 storage API", "[storage][pager]") {
    using CreateResult = decltype(Pager::create(std::filesystem::path{}, 1));
    using OpenResult = decltype(Pager::open(std::filesystem::path{}, 1));
    using GetPageResult = decltype(std::declval<Pager&>().get_page(PageId{ 1 }));
    using NewPageResult = decltype(std::declval<Pager&>().new_page());
    using BeginResult = decltype(std::declval<Pager&>().begin_transaction());
    using CommitResult = decltype(std::declval<Pager&>().commit_transaction());
    using RollbackResult = decltype(std::declval<Pager&>().rollback_transaction());
    using CheckpointResult = decltype(std::declval<Pager&>().checkpoint());
    using CloseResult = decltype(std::declval<Pager&>().close());

    STATIC_REQUIRE(std::is_same_v<CreateResult, Result<Pager>>);
    STATIC_REQUIRE(std::is_same_v<OpenResult, Result<Pager>>);
    STATIC_REQUIRE(std::is_same_v<GetPageResult, Result<PageHandle>>);
    STATIC_REQUIRE(std::is_same_v<NewPageResult, Result<PageHandle>>);
    STATIC_REQUIRE(std::is_same_v<BeginResult, Status>);
    STATIC_REQUIRE(std::is_same_v<CommitResult, Status>);
    STATIC_REQUIRE(std::is_same_v<RollbackResult, Status>);
    STATIC_REQUIRE(std::is_same_v<CheckpointResult, Status>);
    STATIC_REQUIRE(std::is_same_v<CloseResult, Status>);
}

TEST_CASE("Pager can create storage files and close them", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    auto created = Pager::create(temp_dir.database_path(), 2);

    REQUIRE(created.ok());
    REQUIRE(std::filesystem::exists(temp_dir.database_path()));
    REQUIRE(std::filesystem::exists(temp_dir.wal_path()));
    REQUIRE(created.value().close().ok());
}

TEST_CASE("Pager can open an existing database", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    {
        auto created = Pager::create(temp_dir.database_path(), 2);
        REQUIRE(created.ok());
        REQUIRE(created.value().close().ok());
    }

    auto opened = Pager::open(temp_dir.database_path(), 2);

    REQUIRE(opened.ok());
    REQUIRE(opened.value().close().ok());
}

TEST_CASE("Pager create holds an exclusive database lock until close", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    auto first = Pager::create(temp_dir.database_path(), 2);
    REQUIRE(first.ok());

    auto second = Pager::open(temp_dir.database_path(), 2);
    REQUIRE_FALSE(second.ok());
    REQUIRE(second.status().code() == StatusCode::IoError);

    REQUIRE(first.value().close().ok());

    auto third = Pager::open(temp_dir.database_path(), 2);
    REQUIRE(third.ok());
    REQUIRE(third.value().close().ok());
}

TEST_CASE("Pager open holds an exclusive database lock until close", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    {
        auto created = Pager::create(temp_dir.database_path(), 2);
        REQUIRE(created.ok());
        REQUIRE(created.value().close().ok());
    }

    auto first = Pager::open(temp_dir.database_path(), 2);
    REQUIRE(first.ok());

    auto second = Pager::open(temp_dir.database_path(), 2);
    REQUIRE_FALSE(second.ok());
    REQUIRE(second.status().code() == StatusCode::IoError);

    REQUIRE(first.value().close().ok());

    auto third = Pager::open(temp_dir.database_path(), 2);
    REQUIRE(third.ok());
    REQUIRE(third.value().close().ok());
}

TEST_CASE("Pager rejects page allocation without an active transaction", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    auto created = Pager::create(temp_dir.database_path(), 1);
    REQUIRE(created.ok());

    Pager& pager = created.value();

    const auto missing_transaction = pager.new_page();
    REQUIRE_FALSE(missing_transaction.ok());
    REQUIRE(missing_transaction.status().code() == StatusCode::TransactionError);

    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated = pager.new_page();
        REQUIRE(allocated.ok());
        REQUIRE(allocated.value().page()->id() == PAGE_ID);
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager allocates append-only zero-filled pages", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    auto created = Pager::create(temp_dir.database_path(), 2);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    REQUIRE(pager.begin_transaction().ok());

    {
        auto first_page = pager.new_page();
        REQUIRE(first_page.ok());
        REQUIRE(first_page.value().page() != nullptr);
        REQUIRE(first_page.value().page()->id() == PageId{ 1 });
        REQUIRE(page_is_zero_filled(*first_page.value().page()));
        REQUIRE(first_page.value().is_dirty());

        auto second_page = pager.new_page();
        REQUIRE(second_page.ok());
        REQUIRE(second_page.value().page() != nullptr);
        REQUIRE(second_page.value().page()->id() == PageId{ 2 });
        REQUIRE(page_is_zero_filled(*second_page.value().page()));
        REQUIRE(second_page.value().is_dirty());
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager reads committed WAL page images after open", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page wal_page = make_page(PAGE_ID, 7);

    {
        auto created = Pager::create(temp_dir.database_path(), 2);
        REQUIRE(created.ok());
        REQUIRE(created.value().close().ok());
    }

    std::uint64_t database_id = 0;

    {
        auto disk_manager_result = DiskManager::open_existing(temp_dir.database_path());
        REQUIRE(disk_manager_result.ok());

        auto header_result = disk_manager_result.value().read_header();
        REQUIRE(header_result.ok());

        database_id = header_result.value().database_id();
    }

    {
        auto wal_manager_result = WalManager::open_or_create(temp_dir.wal_path(), database_id);
        REQUIRE(wal_manager_result.ok());

        const std::array pages{ wal_page };
        REQUIRE(wal_manager_result.value().commit_transaction(TRANSACTION_ID, pages).ok());
        REQUIRE(wal_manager_result.value().close().ok());
    }

    auto opened = Pager::open(temp_dir.database_path(), 2);
    REQUIRE(opened.ok());

    auto recovered = opened.value().get_page(PAGE_ID);
    REQUIRE(recovered.ok());
    REQUIRE(recovered.value().page()->data() == wal_page.data());
    REQUIRE(opened.value().close().ok());
}

TEST_CASE("Pager restores committed page count from WAL header after open", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    {
        auto created = Pager::create(temp_dir.database_path(), 2);
        REQUIRE(created.ok());

        Pager& pager = created.value();
        REQUIRE(pager.begin_transaction().ok());

        {
            auto allocated = pager.new_page();
            REQUIRE(allocated.ok());
            REQUIRE(allocated.value().page()->id() == PageId{ 1 });
        }

        REQUIRE(pager.commit_transaction().ok());
        REQUIRE(pager.close().ok());
    }

    auto opened = Pager::open(temp_dir.database_path(), 2);
    REQUIRE(opened.ok());

    Pager& pager = opened.value();
    REQUIRE(pager.begin_transaction().ok());

    auto allocated = pager.new_page();
    REQUIRE(allocated.ok());
    REQUIRE(allocated.value().page()->id() == PageId{ 2 });

    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager commit makes dirty page data visible after reopen", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page expected_page = make_page(PAGE_ID, 11);

    {
        auto created = Pager::create(temp_dir.database_path(), 2);
        REQUIRE(created.ok());

        Pager& pager = created.value();
        REQUIRE(pager.begin_transaction().ok());

        {
            auto allocated = pager.new_page();
            REQUIRE(allocated.ok());
            REQUIRE(allocated.value().page()->id() == PAGE_ID);

            auto mutable_page = allocated.value().mutable_page();
            REQUIRE(mutable_page.ok());
            mutable_page.value()->data() = expected_page.data();
        }

        REQUIRE(pager.commit_transaction().ok());
        REQUIRE(pager.close().ok());
    }

    auto opened = Pager::open(temp_dir.database_path(), 2);
    REQUIRE(opened.ok());

    auto recovered = opened.value().get_page(PAGE_ID);
    REQUIRE(recovered.ok());
    REQUIRE(recovered.value().page()->data() == expected_page.data());
    REQUIRE(opened.value().close().ok());
}

TEST_CASE("Pager rejects marking a page dirty without an active transaction", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page committed_page = make_page(PAGE_ID, 20);

    auto created = Pager::create(temp_dir.database_path(), 2);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated = pager.new_page();
        REQUIRE(allocated.ok());
        REQUIRE(allocated.value().page()->id() == PAGE_ID);

        auto mutable_page = allocated.value().mutable_page();
        REQUIRE(mutable_page.ok());
        mutable_page.value()->data() = committed_page.data();
    }

    REQUIRE(pager.commit_transaction().ok());

    {
        auto page = pager.get_page(PAGE_ID);
        REQUIRE(page.ok());

        const auto dirty_status = page.value().mark_dirty();
        REQUIRE_FALSE(dirty_status.ok());
        REQUIRE(dirty_status.code() == StatusCode::TransactionError);
    }

    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager commit rejects dirty pinned pages", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page expected_page = make_page(PAGE_ID, 21);

    auto created = Pager::create(temp_dir.database_path(), 2);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated = pager.new_page();
        REQUIRE(allocated.ok());
        REQUIRE(allocated.value().page()->id() == PAGE_ID);

        auto mutable_page = allocated.value().mutable_page();
        REQUIRE(mutable_page.ok());
        mutable_page.value()->data() = expected_page.data();

        const auto commit_status = pager.commit_transaction();
        REQUIRE_FALSE(commit_status.ok());
        REQUIRE(commit_status.code() == StatusCode::TransactionError);
        REQUIRE(pager.in_transaction());
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE_FALSE(pager.in_transaction());
    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager does not recover dirty page data without commit", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page committed_page = make_page(PAGE_ID, 12);
    const Page uncommitted_page = make_page(PAGE_ID, 13);

    {
        auto created = Pager::create(temp_dir.database_path(), 2);
        REQUIRE(created.ok());

        Pager& pager = created.value();
        REQUIRE(pager.begin_transaction().ok());

        {
            auto allocated = pager.new_page();
            REQUIRE(allocated.ok());
            REQUIRE(allocated.value().page()->id() == PAGE_ID);

            auto mutable_page = allocated.value().mutable_page();
            REQUIRE(mutable_page.ok());
            mutable_page.value()->data() = committed_page.data();
        }

        REQUIRE(pager.commit_transaction().ok());
        REQUIRE(pager.close().ok());
    }

    {
        auto opened = Pager::open(temp_dir.database_path(), 2);
        REQUIRE(opened.ok());

        Pager& pager = opened.value();
        REQUIRE(pager.begin_transaction().ok());

        {
            auto page = pager.get_page(PAGE_ID);
            REQUIRE(page.ok());

            auto mutable_page = page.value().mutable_page();
            REQUIRE(mutable_page.ok());
            mutable_page.value()->data() = uncommitted_page.data();
        }

        REQUIRE(pager.close().ok());
    }

    auto reopened = Pager::open(temp_dir.database_path(), 2);
    REQUIRE(reopened.ok());

    auto recovered = reopened.value().get_page(PAGE_ID);
    REQUIRE(recovered.ok());
    REQUIRE(recovered.value().page()->data() == committed_page.data());
    REQUIRE(reopened.value().close().ok());
}

TEST_CASE("Pager commit sync failure leaves transaction unresolved", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page expected_page = make_page(PAGE_ID, 14);

    auto created = Pager::create(temp_dir.database_path(), 2);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    SyncFailureInjector injector;
    pager.set_wal_fault_injector(&injector);

    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated = pager.new_page();
        REQUIRE(allocated.ok());
        REQUIRE(allocated.value().page()->id() == PAGE_ID);

        auto mutable_page = allocated.value().mutable_page();
        REQUIRE(mutable_page.ok());
        mutable_page.value()->data() = expected_page.data();
    }

    const auto failed_commit = pager.commit_transaction();
    REQUIRE_FALSE(failed_commit.ok());
    REQUIRE(failed_commit.code() == StatusCode::IoError);
    REQUIRE(pager.in_transaction());

    injector.fail_sync = false;

    const auto retry_commit = pager.commit_transaction();
    REQUIRE_FALSE(retry_commit.ok());
    REQUIRE(retry_commit.code() == StatusCode::TransactionError);

    const auto rollback_status = pager.rollback_transaction();
    REQUIRE_FALSE(rollback_status.ok());
    REQUIRE(rollback_status.code() == StatusCode::TransactionError);

    const auto new_page_result = pager.new_page();
    REQUIRE_FALSE(new_page_result.ok());
    REQUIRE(new_page_result.status().code() == StatusCode::TransactionError);

    REQUIRE(pager.in_transaction());
    REQUIRE(pager.close().ok());

    auto opened = Pager::open(temp_dir.database_path(), 2);
    REQUIRE(opened.ok());

    auto recovered = opened.value().get_page(PAGE_ID);
    REQUIRE(recovered.ok());
    REQUIRE(recovered.value().page()->data() == expected_page.data());
    REQUIRE(opened.value().close().ok());
}

TEST_CASE("Pager rollback restores modified existing page data", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page committed_page = make_page(PAGE_ID, 15);
    const Page uncommitted_page = make_page(PAGE_ID, 16);

    auto created = Pager::create(temp_dir.database_path(), 2);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated = pager.new_page();
        REQUIRE(allocated.ok());
        REQUIRE(allocated.value().page()->id() == PAGE_ID);

        auto mutable_page = allocated.value().mutable_page();
        REQUIRE(mutable_page.ok());
        mutable_page.value()->data() = committed_page.data();
    }

    REQUIRE(pager.commit_transaction().ok());
    REQUIRE(pager.begin_transaction().ok());

    {
        auto page = pager.get_page(PAGE_ID);
        REQUIRE(page.ok());

        auto mutable_page = page.value().mutable_page();
        REQUIRE(mutable_page.ok());
        mutable_page.value()->data() = uncommitted_page.data();
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE_FALSE(pager.in_transaction());

    auto restored = pager.get_page(PAGE_ID);
    REQUIRE(restored.ok());
    REQUIRE(restored.value().page()->data() == committed_page.data());

    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager rollback makes restored pages evictable", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page committed_page = make_page(PAGE_ID, 21);
    const Page uncommitted_page = make_page(PAGE_ID, 22);

    auto created = Pager::create(temp_dir.database_path(), 1);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated = pager.new_page();
        REQUIRE(allocated.ok());
        REQUIRE(allocated.value().page()->id() == PAGE_ID);

        auto mutable_page = allocated.value().mutable_page();
        REQUIRE(mutable_page.ok());
        mutable_page.value()->data() = committed_page.data();
    }

    REQUIRE(pager.commit_transaction().ok());
    REQUIRE(pager.begin_transaction().ok());

    {
        auto page = pager.get_page(PAGE_ID);
        REQUIRE(page.ok());

        auto mutable_page = page.value().mutable_page();
        REQUIRE(mutable_page.ok());
        mutable_page.value()->data() = uncommitted_page.data();
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated_after_rollback = pager.new_page();
        REQUIRE(allocated_after_rollback.ok());
        REQUIRE(allocated_after_rollback.value().page()->id() == PageId{ 2 });
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager rollback restores page count after page allocation", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    auto created = Pager::create(temp_dir.database_path(), 2);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated = pager.new_page();
        REQUIRE(allocated.ok());
        REQUIRE(allocated.value().page()->id() == PAGE_ID);
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE_FALSE(pager.in_transaction());

    REQUIRE(pager.begin_transaction().ok());

    auto allocated_after_rollback = pager.new_page();
    REQUIRE(allocated_after_rollback.ok());
    REQUIRE(allocated_after_rollback.value().page()->id() == PAGE_ID);

    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager rollback clears failed transaction state", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    auto created = Pager::create(temp_dir.database_path(), 2);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    REQUIRE(pager.begin_transaction().ok());
    REQUIRE(pager.mark_transaction_failed().ok());

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE_FALSE(pager.in_transaction());
    REQUIRE(pager.begin_transaction().ok());

    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager checkpoint persists committed pages to the main database and resets WAL", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page expected_page = make_page(PAGE_ID, 17);
    std::uint64_t database_id = 0;

    {
        auto created = Pager::create(temp_dir.database_path(), 2);
        REQUIRE(created.ok());

        Pager& pager = created.value();
        REQUIRE(pager.begin_transaction().ok());

        {
            auto allocated = pager.new_page();
            REQUIRE(allocated.ok());
            REQUIRE(allocated.value().page()->id() == PAGE_ID);

            auto mutable_page = allocated.value().mutable_page();
            REQUIRE(mutable_page.ok());
            mutable_page.value()->data() = expected_page.data();
        }

        REQUIRE(pager.commit_transaction().ok());
        REQUIRE(pager.checkpoint().ok());
        REQUIRE(pager.close().ok());
    }

    auto disk_manager_result = DiskManager::open_existing(temp_dir.database_path());
    REQUIRE(disk_manager_result.ok());

    auto header_result = disk_manager_result.value().read_header();
    REQUIRE(header_result.ok());
    REQUIRE(header_result.value().page_count() == 2);
    database_id = header_result.value().database_id();

    auto disk_page = disk_manager_result.value().read_page(PAGE_ID);
    REQUIRE(disk_page.ok());
    REQUIRE(disk_page.value().data() == expected_page.data());

    auto wal_scan_result = dandb::wal::WalScanner::scan(temp_dir.wal_path(), database_id);
    REQUIRE(wal_scan_result.ok());
    REQUIRE(wal_scan_result.value().latest_committed_frame_offsets.empty());
    REQUIRE(wal_scan_result.value().valid_wal_end_offset == WAL_HEADER_SIZE);
}

TEST_CASE("Pager checkpoint rejects active transactions", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    auto created = Pager::create(temp_dir.database_path(), 2);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    REQUIRE(pager.begin_transaction().ok());

    const auto checkpoint_status = pager.checkpoint();

    REQUIRE_FALSE(checkpoint_status.ok());
    REQUIRE(checkpoint_status.code() == StatusCode::TransactionError);

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager commit makes cached committed pages evictable before checkpoint", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page expected_page = make_page(PAGE_ID, 18);

    auto created = Pager::create(temp_dir.database_path(), 1);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated = pager.new_page();
        REQUIRE(allocated.ok());
        REQUIRE(allocated.value().page()->id() == PAGE_ID);

        auto mutable_page = allocated.value().mutable_page();
        REQUIRE(mutable_page.ok());
        mutable_page.value()->data() = expected_page.data();
    }

    REQUIRE(pager.commit_transaction().ok());

    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated_after_checkpoint = pager.new_page();
        REQUIRE(allocated_after_checkpoint.ok());
        REQUIRE(allocated_after_checkpoint.value().page()->id() == PageId{ 2 });
    }

    REQUIRE(pager.rollback_transaction().ok());
    REQUIRE(pager.close().ok());
}

TEST_CASE("Pager checkpoint keeps WAL when main database sync fails", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;
    const Page expected_page = make_page(PAGE_ID, 19);

    auto created = Pager::create(temp_dir.database_path(), 2);
    REQUIRE(created.ok());

    Pager& pager = created.value();
    REQUIRE(pager.begin_transaction().ok());

    {
        auto allocated = pager.new_page();
        REQUIRE(allocated.ok());
        REQUIRE(allocated.value().page()->id() == PAGE_ID);

        auto mutable_page = allocated.value().mutable_page();
        REQUIRE(mutable_page.ok());
        mutable_page.value()->data() = expected_page.data();
    }

    REQUIRE(pager.commit_transaction().ok());

    auto disk_manager_result = DiskManager::open_existing(temp_dir.database_path());
    REQUIRE(disk_manager_result.ok());

    auto header_result = disk_manager_result.value().read_header();
    REQUIRE(header_result.ok());
    const std::uint64_t database_id = header_result.value().database_id();

    SyncFailureInjector injector;
    pager.set_disk_fault_injector(&injector);

    const auto checkpoint_status = pager.checkpoint();

    REQUIRE_FALSE(checkpoint_status.ok());
    REQUIRE(checkpoint_status.code() == StatusCode::IoError);

    auto wal_scan_result = dandb::wal::WalScanner::scan(temp_dir.wal_path(), database_id);
    REQUIRE(wal_scan_result.ok());
    REQUIRE(wal_scan_result.value().latest_committed_frame_offsets.find(PAGE_ID) != wal_scan_result.value().latest_committed_frame_offsets.end());
}

TEST_CASE("Pager rejects zero buffer pool capacity before touching storage", "[storage][pager]") {
    const dandb::testutil::TempDir temp_dir;

    auto created = Pager::create(temp_dir.database_path(), 0);
    auto opened = Pager::open(temp_dir.database_path(), 0);

    REQUIRE_FALSE(created.ok());
    REQUIRE(created.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(std::filesystem::exists(temp_dir.database_path()));
    REQUIRE_FALSE(std::filesystem::exists(temp_dir.wal_path()));
}
