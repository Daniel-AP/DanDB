#pragma once

#include <dandb/catalog/Catalog.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/execution/ExecutionResult.h>
#include <dandb/sql/Ast.h>
#include <dandb/storage/Pager.h>

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace dandb::execution {

    class Database {
        public:
            Database(const Database&) = delete;
            Database& operator=(const Database&) = delete;
            Database(Database&&) = default;
            Database& operator=(Database&&) = default;

            static core::Result<Database> open_or_create(std::filesystem::path path);

            std::vector<ExecutionResult> execute(std::string_view sql_string);
            core::Status close();

        private:
            Database(std::unique_ptr<storage::Pager> pager, catalog::Catalog catalog);

            ExecutionResult execute_statement(const sql::Statement& statement);

            ExecutionResult execute_create_table_statement(const sql::CreateTableStatement& statement);
            ExecutionResult execute_begin_statement(const sql::BeginStatement& statement);
            ExecutionResult execute_commit_statement(const sql::CommitStatement& statement);
            ExecutionResult execute_rollback_statement(const sql::RollbackStatement& statement);
            ExecutionResult execute_checkpoint_statement(const sql::CheckpointStatement& statement);

            std::unique_ptr<storage::Pager> pager_;
            catalog::Catalog catalog_;
    };

}
