#include <dandb/execution/Database.h>

#include <cstddef>
#include <memory>
#include <system_error>
#include <utility>

namespace {

    constexpr std::size_t DEFAULT_BUFFER_POOL_CAPACITY = 10;

}

namespace dandb::execution {

    Database::Database(std::unique_ptr<storage::Pager> pager, catalog::Catalog catalog) :
        pager_(std::move(pager)),
        catalog_(std::move(catalog))
    {}

    core::Result<Database> Database::open_or_create(std::filesystem::path path) {

        std::error_code error_code;
        const bool database_exists = std::filesystem::exists(path, error_code);

        if(error_code) {
            return core::Status::IoError("Cannot inspect database path: "+error_code.message());
        }

        auto pager_result = database_exists
            ? storage::Pager::open(path, DEFAULT_BUFFER_POOL_CAPACITY)
            : storage::Pager::create(path, DEFAULT_BUFFER_POOL_CAPACITY);

        if(!pager_result.ok()) {
            return pager_result.status();
        }

        auto pager = std::make_unique<storage::Pager>(std::move(pager_result.value()));
        auto catalog_result = catalog::Catalog::load(*pager);

        if(!catalog_result.ok()) {
            return catalog_result.status();
        }

        return Database{std::move(pager), std::move(catalog_result.value())};

    }

    core::Status Database::close() {
        return pager_->close();
    }

}
