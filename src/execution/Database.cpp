#include <dandb/execution/Database.h>

#include <dandb/btree/BTree.h>
#include <dandb/record/RowCodec.h>
#include <dandb/record/RowHelpers.h>
#include <dandb/sql/Binder.h>
#include <dandb/sql/Lexer.h>
#include <dandb/sql/Parser.h>
#include <dandb/record/Column.h>
#include <dandb/record/Schema.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <variant>

namespace {

    constexpr std::size_t DEFAULT_BUFFER_POOL_CAPACITY = 10;

    dandb::core::Result<int> compare_values(const dandb::record::Value& left, const dandb::record::Value& right) {

        if(left.type().kind() != right.type().kind()) {
            return dandb::core::Status::InternalError("Cannot compare SELECT predicate values with different types");
        }

        switch(left.type().kind()) {
            case dandb::record::LogicalType::Kind::Int8:
            case dandb::record::LogicalType::Kind::Int16:
            case dandb::record::LogicalType::Kind::Int32:
            case dandb::record::LogicalType::Kind::Int64:
                if(left.as_integer() < right.as_integer()) return -1;
                if(left.as_integer() > right.as_integer()) return 1;
                return 0;

            case dandb::record::LogicalType::Kind::Float64:
                if(left.as_float64() < right.as_float64()) return -1;
                if(left.as_float64() > right.as_float64()) return 1;
                return 0;

            case dandb::record::LogicalType::Kind::String:
                if(left.as_string() < right.as_string()) return -1;
                if(left.as_string() > right.as_string()) return 1;
                return 0;

            case dandb::record::LogicalType::Kind::Boolean:
                if(left.as_boolean() == right.as_boolean()) return 0;
                return left.as_boolean() ? 1 : -1;
        }

        return dandb::core::Status::InternalError("Cannot compare SELECT predicate values with an unknown type");

    }

    bool comparison_matches(int comparison, dandb::sql::ComparisonOperator comparison_operator) {

        switch(comparison_operator) {
            case dandb::sql::ComparisonOperator::Equal:
                return comparison == 0;
            case dandb::sql::ComparisonOperator::NotEqual:
                return comparison != 0;
            case dandb::sql::ComparisonOperator::Less:
                return comparison < 0;
            case dandb::sql::ComparisonOperator::LessEqual:
                return comparison <= 0;
            case dandb::sql::ComparisonOperator::Greater:
                return comparison > 0;
            case dandb::sql::ComparisonOperator::GreaterEqual:
                return comparison >= 0;
            case dandb::sql::ComparisonOperator::IsNull:
            case dandb::sql::ComparisonOperator::IsNotNull:
                return false;
        }

        return false;

    }

    dandb::core::Result<bool> row_matches_predicate(
        const dandb::record::Row& row,
        const dandb::sql::BoundPredicate& predicate,
        const std::optional<dandb::record::Value>& predicate_value
    ) {

        const auto& row_value = row.value(predicate.column.ordinal);

        if(predicate.comparison_operator == dandb::sql::ComparisonOperator::IsNull) {
            return row_value.is_null();
        }

        if(predicate.comparison_operator == dandb::sql::ComparisonOperator::IsNotNull) {
            return !row_value.is_null();
        }

        if(row_value.is_null()) {
            return false;
        }

        if(!predicate_value.has_value()) {
            return dandb::core::Status::InternalError("SELECT comparison predicate has no converted literal value");
        }

        auto comparison_result = compare_values(row_value, *predicate_value);
        if(!comparison_result.ok()) {
            return comparison_result.status();
        }

        return comparison_matches(comparison_result.value(), predicate.comparison_operator);

    }

    std::optional<std::vector<std::byte>> next_key(std::vector<std::byte> key) {

        for(std::size_t i = key.size(); i > 0; i--) {
            const std::size_t current_index = i-1;
            const auto value = std::to_integer<std::uint8_t>(key[current_index]);

            if(value == 0xff) {
                key[current_index] = std::byte{ 0x00 };
                continue;
            }

            key[current_index] = static_cast<std::byte>(value+1);
            return key;
        }

        return std::nullopt;

    }

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
                } else if constexpr(std::is_same_v<BoundStatementType, sql::BoundDropTableStatement>) {
                    return execute_drop_table_statement(bound_statement);
                } else if constexpr(std::is_same_v<BoundStatementType, sql::BoundInsertStatement>) {
                    return execute_insert_statement(bound_statement);
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
                column_definition.constraints.primary_key || column_definition.constraints.unique
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

    ExecutionResult Database::execute_drop_table_statement(const sql::BoundDropTableStatement& statement) {

        const auto status = catalog_.drop_table(statement.table_name);
        if(!status.ok()) {
            return ExecutionResult{status};
        }

        return ExecutionResult{status, "Table '"+statement.table_name+"' dropped"};

    }

    ExecutionResult Database::execute_insert_statement(const sql::BoundInsertStatement& statement) {

        const auto* table_descriptor = catalog_.find_table(statement.table_id);
        if(table_descriptor == nullptr) {
            return ExecutionResult{core::Status::InternalError("Cannot execute INSERT: bound table is missing from catalog")};
        }

        const auto* schema = catalog_.schema_for_table(statement.table_id);
        if(schema == nullptr) {
            return ExecutionResult{core::Status::InternalError("Cannot execute INSERT: bound table has no schema")};
        }

        if(statement.values.size() != schema->column_count()) {
            return ExecutionResult{core::Status::InvalidArgument("Cannot execute INSERT: value count does not match table schema")};
        }

        std::vector<record::Value> values;
        values.reserve(statement.values.size());

        for(std::size_t ordinal = 0; ordinal < statement.values.size(); ordinal++) {

            const auto& column = schema->column(ordinal);
            auto value_result = statement.values[ordinal].convert_to(column.logical_type(), column.nullable());
            if(!value_result.ok()) {
                return ExecutionResult{value_result.status()};
            }

            values.push_back(std::move(value_result.value()));

        }

        auto row_result = record::RowHelpers::build_row(*schema, std::move(values));
        if(!row_result.ok()) {
            return ExecutionResult{row_result.status()};
        }

        auto row_bytes_result = record::RowCodec::encode(*schema, row_result.value());
        if(!row_bytes_result.ok()) {
            return ExecutionResult{row_bytes_result.status()};
        }

        auto primary_key_bytes_result = record::RowHelpers::primary_key_bytes(*schema, row_result.value());
        if(!primary_key_bytes_result.ok()) {
            return ExecutionResult{primary_key_bytes_result.status()};
        }

        auto tree_result = btree::BTree::open_existing(
            *pager_,
            table_descriptor->root_page_id(),
            static_cast<std::uint16_t>(schema->primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(schema->row_size())
        );
        if(!tree_result.ok()) {
            return ExecutionResult{tree_result.status()};
        }

        btree::BTree tree = std::move(tree_result.value());

        const bool owns_transaction = !pager_->in_transaction();
        if(owns_transaction) {
            const auto begin_status = pager_->begin_transaction();
            if(!begin_status.ok()) {
                return ExecutionResult{begin_status};
            }
        }

        const auto insert_status = tree.insert(primary_key_bytes_result.value(), row_bytes_result.value());
        if(!insert_status.ok()) {
            return ExecutionResult{handle_mutation_failure(insert_status, owns_transaction)};

        }

        if(!owns_transaction) {
            return ExecutionResult{core::Status::Ok(), std::nullopt, std::nullopt, 1};
        }

        const auto commit_status = pager_->commit_transaction();
        if(!commit_status.ok()) {
            return ExecutionResult{commit_status};
        }

        const auto catalog_status = catalog_.on_transaction_committed();
        if(!catalog_status.ok()) {
            return ExecutionResult{catalog_status};
        }

        return ExecutionResult{commit_status, std::nullopt, std::nullopt, 1};

    }

    core::Status Database::handle_mutation_failure(core::Status failure_status, bool owns_transaction) {

        if(!owns_transaction) {
            return failure_status;
        }

        const auto rollback_status = pager_->rollback_transaction();
        if(!rollback_status.ok()) {
            return rollback_status;
        }

        const auto catalog_status = catalog_.on_transaction_rolled_back();
        if(!catalog_status.ok()) {
            return catalog_status;
        }

        return failure_status;

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
