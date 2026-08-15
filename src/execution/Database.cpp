#include <dandb/execution/Database.h>

#include <dandb/sql/Lexer.h>
#include <dandb/sql/Parser.h>

#include <cstddef>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

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

    std::vector<ExecutionResult> Database::execute(std::string_view sql_string) {

        sql::Lexer lexer(sql_string);
        auto tokens_result = lexer.tokenize();

        if(!tokens_result.ok()) {
            return { ExecutionResult{tokens_result.status()} };
        }

        sql::Parser parser(std::move(tokens_result.value()));
        auto statements_result = parser.parse();

        if(!statements_result.ok()) {
            return { ExecutionResult{statements_result.status()} };
        }

        std::vector<ExecutionResult> results;
        const auto& statements = statements_result.value();
        results.reserve(statements.size());

        for(const auto& statement: statements) {
            auto result = execute_statement(statement);
            const bool execution_succeeded = result.status.ok();
            results.push_back(std::move(result));

            if(!execution_succeeded) {
                break;
            }
        }

        return results;

    }

    ExecutionResult Database::execute_begin_statement(const sql::BeginStatement&) {

        const auto status = pager_->begin_transaction();

        if(!status.ok()) {
            return ExecutionResult{status};
        }

        return ExecutionResult{status, "Transaction started"};

    }

    ExecutionResult Database::execute_commit_statement(const sql::CommitStatement&) {

        const auto commit_status = pager_->commit_transaction();

        if(!commit_status.ok()) {
            return ExecutionResult{commit_status};
        }

        const auto catalog_status = catalog_.on_transaction_committed();

        if(!catalog_status.ok()) {
            return ExecutionResult{catalog_status};
        }

        return ExecutionResult{commit_status, "Transaction committed"};

    }

    ExecutionResult Database::execute_rollback_statement(const sql::RollbackStatement&) {

        const auto rollback_status = pager_->rollback_transaction();

        if(!rollback_status.ok()) {
            return ExecutionResult{rollback_status};
        }

        const auto catalog_status = catalog_.on_transaction_rolled_back();

        if(!catalog_status.ok()) {
            return ExecutionResult{catalog_status};
        }

        return ExecutionResult{rollback_status, "Transaction rolled back"};

    }

    ExecutionResult Database::execute_checkpoint_statement(const sql::CheckpointStatement&) {

        const auto status = pager_->checkpoint();

        if(!status.ok()) {
            return ExecutionResult{status};
        }

        return ExecutionResult{status, "Checkpoint completed"};

    }

    core::Status Database::close() {
        return pager_->close();
    }

}
