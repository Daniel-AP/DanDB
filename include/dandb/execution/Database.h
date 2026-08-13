#pragma once

#include <dandb/catalog/Catalog.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Pager.h>

#include <filesystem>
#include <memory>
#include <string_view>

namespace dandb::execution {

    class Database {
        public:
            Database(const Database&) = delete;
            Database& operator=(const Database&) = delete;
            Database(Database&&) = default;
            Database& operator=(Database&&) = default;

            static core::Result<Database> open_or_create(std::filesystem::path path);

            core::Status execute(std::string_view sql_string);
            core::Status close();

        private:
            Database(std::unique_ptr<storage::Pager> pager, catalog::Catalog catalog);

            std::unique_ptr<storage::Pager> pager_;
            catalog::Catalog catalog_;
    };

}
