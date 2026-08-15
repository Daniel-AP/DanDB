#include <dandb/execution/Database.h>

#include <dandb/sql/Binder.h>
#include <dandb/sql/Lexer.h>
#include <dandb/sql/Parser.h>
#include <dandb/record/Column.h>
#include <dandb/record/Schema.h>

#include <cstddef>
#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <variant>

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

            if(!result.status.ok() && pager_->in_transaction()) {
                pager_->mark_transaction_failed();
            }

            const bool execution_succeeded = result.status.ok();
            results.push_back(std::move(result));

            if(!execution_succeeded) {
                break;
            }
        }

        return results;

    }

    ExecutionResult Database::execute_statement(const sql::Statement& statement) {

        const bool rollback_statement = std::holds_alternative<sql::RollbackStatement>(statement);

        if(pager_->transaction_failed() && !rollback_statement) {
            return ExecutionResult{
                core::Status::TransactionError("Cannot execute statement: transaction is failed; rollback is required")
            };
        }

        const sql::Binder binder(catalog_);
        auto bound_statement_result = binder.bind(statement);

        if(!bound_statement_result.ok()) {
            return ExecutionResult{bound_statement_result.status()};
        }

        return std::visit(
            [this](const auto& bound_statement) -> ExecutionResult {
                using BoundStatementType = std::decay_t<decltype(bound_statement)>;

                if constexpr(std::is_same_v<BoundStatementType, sql::CreateTableStatement>) {
                    return execute_create_table_statement(bound_statement);
                } else if constexpr(std::is_same_v<BoundStatementType, sql::BeginStatement>) {
                    return execute_begin_statement(bound_statement);
                } else if constexpr(std::is_same_v<BoundStatementType, sql::CommitStatement>) {
                    return execute_commit_statement(bound_statement);
                } else if constexpr(std::is_same_v<BoundStatementType, sql::RollbackStatement>) {
                    return execute_rollback_statement(bound_statement);
                } else if constexpr(std::is_same_v<BoundStatementType, sql::CheckpointStatement>) {
                    return execute_checkpoint_statement(bound_statement);
                } else {
                    return ExecutionResult{
                        core::Status::InvalidArgument("Statement execution is not implemented")
                    };
                }
            },
            bound_statement_result.value()
        );

    }

    ExecutionResult Database::execute_create_table_statement(const sql::CreateTableStatement& statement) {

        std::vector<record::Column> columns;
        columns.reserve(statement.columns.size());

        for(const auto& column_definition: statement.columns) {

            const bool nullable = !(
                column_definition.constraints.primary_key ||
                column_definition.constraints.unique ||
                column_definition.constraints.not_null
            );

            auto column_result = record::Column::create(
                column_definition.name.text,
                column_definition.type.logical_type,
                nullable,
                column_definition.constraints.primary_key,
                column_definition.constraints.unique
            );
            if(!column_result.ok()) {
                return ExecutionResult{column_result.status()};
            }

            columns.push_back(std::move(column_result.value()));

        }

        auto schema_result = record::Schema::create(std::move(columns));
        if(!schema_result.ok()) {
            return ExecutionResult{schema_result.status()};
        }

        const auto status = catalog_.create_table(statement.table_name.text, schema_result.value());
        if(!status.ok()) {
            return ExecutionResult{status};
        }

        return ExecutionResult{status, "Table '"+statement.table_name.text+"' created"};

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
