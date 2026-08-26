#include <dandb/execution/Database.h>

#include <dandb/btree/BTree.h>
#include <dandb/record/KeyCodec.h>
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
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <variant>

namespace {

    constexpr std::size_t DEFAULT_BUFFER_POOL_CAPACITY = 10;

    struct FullTableScanPath {};

    struct PrimaryKeyRangePath {};

    struct SecondaryIndexRangePath {
        const dandb::catalog::IndexDescriptor& index_descriptor;
    };

    using AccessPath = std::variant<
        FullTableScanPath,
        PrimaryKeyRangePath,
        SecondaryIndexRangePath
    >;

    AccessPath plan_access_path(
        const dandb::record::Schema& schema,
        std::span<const dandb::catalog::IndexDescriptor> index_descriptors,
        const std::optional<dandb::sql::BoundPredicate>& predicate
    ) {

        if(!predicate.has_value()) {
            return FullTableScanPath{};
        }

        const bool is_null_predicate = (
            predicate->comparison_operator == dandb::sql::ComparisonOperator::IsNull ||
            predicate->comparison_operator == dandb::sql::ComparisonOperator::IsNotNull
        );

        if(is_null_predicate) {
            return FullTableScanPath{};
        }

        if(predicate->column.ordinal == schema.primary_key_ordinal()) {
            return PrimaryKeyRangePath{};
        }

        for(const auto& index_descriptor: index_descriptors) {
            if(index_descriptor.primary()) continue;

            if(index_descriptor.indexed_column_id() == predicate->column.column_id) {
                return SecondaryIndexRangePath{ index_descriptor };
            }
        }

        return FullTableScanPath{};

    }

    dandb::core::Status consume_table_cursors(std::vector<dandb::btree::BTreeCursor>& cursors, auto&& process_table_row) {

        for(auto& cursor: cursors) {
            while(true) {
                auto entry_result = cursor.next();
                if(!entry_result.ok()) {
                    return entry_result.status();
                }

                if(!entry_result.value().has_value()) {
                    break;
                }

                const auto& entry = *entry_result.value();

                const auto process_status = process_table_row(
                    std::span<const std::byte>{entry.key},
                    std::span<const std::byte>{entry.value}
                );
                if(!process_status.ok()) {
                    return process_status;
                }
            }
        }

        return dandb::core::Status::Ok();

    }

    dandb::core::Status consume_secondary_index_cursors(
        std::vector<dandb::btree::BTreeCursor>& cursors,
        const dandb::btree::BTree& table_tree,
        auto&& process_table_row
    ) {

        for(auto& cursor: cursors) {
            while(true) {
                auto entry_result = cursor.next();
                if(!entry_result.ok()) {
                    return entry_result.status();
                }

                if(!entry_result.value().has_value()) {
                    break;
                }

                const auto& index_entry = *entry_result.value();

                auto table_row_result = table_tree.find(index_entry.value);
                if(!table_row_result.ok()) {
                    return table_row_result.status();
                }

                const auto process_status = process_table_row(
                    std::span<const std::byte>{index_entry.value},
                    std::span<const std::byte>{table_row_result.value()}
                );
                if(!process_status.ok()) {
                    return process_status;
                }
            }
        }

        return dandb::core::Status::Ok();

    }

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

    std::optional<std::vector<std::byte>> next_key(std::span<const std::byte> key) {

        std::vector<std::byte> successor(key.begin(), key.end());

        for(std::size_t i = successor.size(); i > 0; i--) {
            const std::size_t current_index = i-1;
            const auto value = std::to_integer<std::uint8_t>(successor[current_index]);

            if(value == 0xff) {
                successor[current_index] = std::byte{ 0x00 };
                continue;
            }

            successor[current_index] = static_cast<std::byte>(value+1);
            return successor;
        }

        return std::nullopt;

    }

    dandb::core::Result<std::vector<dandb::btree::BTreeCursor>> open_table_cursors(
        dandb::btree::BTree& tree,
        dandb::sql::ComparisonOperator comparison_operator,
        std::span<const std::byte> key
    ) {

        std::vector<dandb::btree::BTreeCursor> cursors;

        const auto next_key_result = next_key(key);

        switch(comparison_operator) {
            case dandb::sql::ComparisonOperator::Equal: {
                auto cursor_result = next_key_result.has_value()
                    ? tree.scan_range(
                        key,
                        std::span<const std::byte>{next_key_result->data(), next_key_result->size()}
                    )
                    : tree.scan_range(key, std::nullopt);

                if(!cursor_result.ok()) {
                    return cursor_result.status();
                }

                cursors.push_back(std::move(cursor_result.value()));
                return cursors;
            }

            case dandb::sql::ComparisonOperator::NotEqual: {
                auto before_cursor_result = tree.scan_range(std::nullopt, key);
                if(!before_cursor_result.ok()) {
                    return before_cursor_result.status();
                }

                cursors.push_back(std::move(before_cursor_result.value()));

                if(next_key_result.has_value()) {
                    auto after_cursor_result = tree.scan_range(
                        std::span<const std::byte>{next_key_result->data(), next_key_result->size()},
                        std::nullopt
                    );
                    if(!after_cursor_result.ok()) {
                        return after_cursor_result.status();
                    }

                    cursors.push_back(std::move(after_cursor_result.value()));
                }

                return cursors;
            }

            case dandb::sql::ComparisonOperator::Less: {
                auto cursor_result = tree.scan_range(std::nullopt, key);
                if(!cursor_result.ok()) {
                    return cursor_result.status();
                }

                cursors.push_back(std::move(cursor_result.value()));
                return cursors;
            }

            case dandb::sql::ComparisonOperator::LessEqual: {
                auto cursor_result = next_key_result.has_value()
                    ? tree.scan_range(
                        std::nullopt,
                        std::span<const std::byte>{next_key_result->data(), next_key_result->size()}
                    )
                    : tree.scan();

                if(!cursor_result.ok()) {
                    return cursor_result.status();
                }

                cursors.push_back(std::move(cursor_result.value()));
                return cursors;
            }

            case dandb::sql::ComparisonOperator::Greater: {
                if(!next_key_result.has_value()) {
                    return cursors;
                }

                auto cursor_result = tree.scan_range(
                    std::span<const std::byte>{next_key_result->data(), next_key_result->size()},
                    std::nullopt
                );
                if(!cursor_result.ok()) {
                    return cursor_result.status();
                }

                cursors.push_back(std::move(cursor_result.value()));
                return cursors;
            }

            case dandb::sql::ComparisonOperator::GreaterEqual: {
                auto cursor_result = tree.scan_range(key, std::nullopt);
                if(!cursor_result.ok()) {
                    return cursor_result.status();
                }

                cursors.push_back(std::move(cursor_result.value()));
                return cursors;
            }

            case dandb::sql::ComparisonOperator::IsNull:
            case dandb::sql::ComparisonOperator::IsNotNull:
                return dandb::core::Status::InternalError("Cannot open primary-key cursors for a null predicate");
        }

        return dandb::core::Status::InternalError("Cannot open primary-key cursors for an unknown comparison operator");

    }

    dandb::core::Result<std::vector<dandb::btree::BTreeCursor>> open_secondary_index_cursors(
        dandb::btree::BTree& tree,
        dandb::sql::ComparisonOperator comparison_operator,
        std::span<const std::byte> indexed_key_prefix
    ) {

        if(indexed_key_prefix.size() > tree.key_size()) {
            return dandb::core::Status::InvalidArgument("Cannot open secondary-index cursors: indexed key prefix is larger than the B+ tree key");
        }

        std::vector<std::byte> lower_bound(indexed_key_prefix.begin(), indexed_key_prefix.end());
        lower_bound.resize(tree.key_size(), std::byte{ 0x00 });

        std::optional<std::vector<std::byte>> next_lower_bound = next_key(indexed_key_prefix);
        if(next_lower_bound.has_value()) {
            next_lower_bound->resize(tree.key_size(), std::byte{ 0x00 });
        }

        std::vector<dandb::btree::BTreeCursor> cursors;

        switch(comparison_operator) {
            case dandb::sql::ComparisonOperator::Equal: {
                auto cursor_result = next_lower_bound.has_value()
                    ? tree.scan_range(
                        lower_bound,
                        std::span<const std::byte>{next_lower_bound->data(), next_lower_bound->size()}
                    )
                    : tree.scan_range(lower_bound, std::nullopt);

                if(!cursor_result.ok()) {
                    return cursor_result.status();
                }

                cursors.push_back(std::move(cursor_result.value()));
                return cursors;
            }

            case dandb::sql::ComparisonOperator::NotEqual: {
                auto before_cursor_result = tree.scan_range(std::nullopt, lower_bound);
                if(!before_cursor_result.ok()) {
                    return before_cursor_result.status();
                }

                cursors.push_back(std::move(before_cursor_result.value()));

                if(next_lower_bound.has_value()) {
                    auto after_cursor_result = tree.scan_range(
                        std::span<const std::byte>{next_lower_bound->data(), next_lower_bound->size()},
                        std::nullopt
                    );
                    if(!after_cursor_result.ok()) {
                        return after_cursor_result.status();
                    }

                    cursors.push_back(std::move(after_cursor_result.value()));
                }

                return cursors;
            }

            case dandb::sql::ComparisonOperator::Less: {
                auto cursor_result = tree.scan_range(std::nullopt, lower_bound);
                if(!cursor_result.ok()) {
                    return cursor_result.status();
                }

                cursors.push_back(std::move(cursor_result.value()));
                return cursors;
            }

            case dandb::sql::ComparisonOperator::LessEqual: {
                auto cursor_result = next_lower_bound.has_value()
                    ? tree.scan_range(
                        std::nullopt,
                        std::span<const std::byte>{next_lower_bound->data(), next_lower_bound->size()}
                    )
                    : tree.scan();

                if(!cursor_result.ok()) {
                    return cursor_result.status();
                }

                cursors.push_back(std::move(cursor_result.value()));
                return cursors;
            }

            case dandb::sql::ComparisonOperator::Greater: {
                if(!next_lower_bound.has_value()) {
                    return cursors;
                }

                auto cursor_result = tree.scan_range(
                    std::span<const std::byte>{next_lower_bound->data(), next_lower_bound->size()},
                    std::nullopt
                );
                if(!cursor_result.ok()) {
                    return cursor_result.status();
                }

                cursors.push_back(std::move(cursor_result.value()));
                return cursors;
            }

            case dandb::sql::ComparisonOperator::GreaterEqual: {
                auto cursor_result = tree.scan_range(lower_bound, std::nullopt);
                if(!cursor_result.ok()) {
                    return cursor_result.status();
                }

                cursors.push_back(std::move(cursor_result.value()));
                return cursors;
            }

            case dandb::sql::ComparisonOperator::IsNull:
            case dandb::sql::ComparisonOperator::IsNotNull:
                return dandb::core::Status::InternalError("Cannot open secondary-index cursors for a null predicate");
        }

        return dandb::core::Status::InternalError("Cannot open secondary-index cursors for an unknown comparison operator");

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
            if(pager_->in_transaction()) pager_->mark_transaction_failed();
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

    core::Status Database::close() {
        return pager_->close();
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
                } else if constexpr(std::is_same_v<BoundStatementType, sql::BoundCreateIndexStatement>) {
                    return execute_create_index_statement(bound_statement);
                } else if constexpr(std::is_same_v<BoundStatementType, sql::BoundDropTableStatement>) {
                    return execute_drop_table_statement(bound_statement);
                } else if constexpr(std::is_same_v<BoundStatementType, sql::BoundSelectStatement>) {
                    return execute_select_statement(bound_statement);
                } else if constexpr(std::is_same_v<BoundStatementType, sql::BoundInsertStatement>) {
                    return execute_insert_statement(bound_statement);
                } else if constexpr(std::is_same_v<BoundStatementType, sql::BoundUpdateStatement>) {
                    return execute_update_statement(bound_statement);
                } else if constexpr(std::is_same_v<BoundStatementType, sql::BoundDeleteStatement>) {
                    return execute_delete_statement(bound_statement);
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

    ExecutionResult Database::execute_create_index_statement(const sql::BoundCreateIndexStatement& statement) {

        const bool owns_transaction = !pager_->in_transaction();
        if(owns_transaction) {

            const auto begin_status = pager_->begin_transaction();
            if(!begin_status.ok()) {
                return ExecutionResult{begin_status};
            }

        }

        const auto create_index_status = catalog_.create_index(
            statement.table_id,
            statement.index_name,
            statement.indexed_column.column_id,
            statement.unique
        );
        if(!create_index_status.ok()) {
            return ExecutionResult{handle_mutation_failure(create_index_status, owns_transaction)};
        }

        const auto* index_descriptor = catalog_.find_index(statement.index_name);
        if(index_descriptor == nullptr) {

            return ExecutionResult{handle_mutation_failure(
                core::Status::InternalError("Cannot execute CREATE INDEX: new index is missing from catalog"),
                owns_transaction
            )};

        }

        const auto* table_descriptor = catalog_.find_table(statement.table_id);
        if(table_descriptor == nullptr) {

            return ExecutionResult{handle_mutation_failure(
                core::Status::InternalError("Cannot execute CREATE INDEX: table is missing from catalog"),
                owns_transaction
            )};

        }

        const auto* schema = catalog_.schema_for_table(statement.table_id);
        if(schema == nullptr) {

            return ExecutionResult{handle_mutation_failure(
                core::Status::InternalError("Cannot execute CREATE INDEX: table schema is missing from catalog"),
                owns_transaction
            )};

        }

        auto table_tree_result = open_table_tree(*table_descriptor);
        if(!table_tree_result.ok()) {
            return ExecutionResult{handle_mutation_failure(table_tree_result.status(), owns_transaction)};
        }

        auto index_tree_result = open_index_tree(*index_descriptor);
        if(!index_tree_result.ok()) {
            return ExecutionResult{handle_mutation_failure(index_tree_result.status(), owns_transaction)};
        }

        auto cursor_result = table_tree_result.value().scan();
        if(!cursor_result.ok()) {
            return ExecutionResult{handle_mutation_failure(cursor_result.status(), owns_transaction)};
        }

        auto index_tree = std::move(index_tree_result.value());
        auto cursor = std::move(cursor_result.value());

        while(true) {

            auto entry_result = cursor.next();
            if(!entry_result.ok()) {
                return ExecutionResult{handle_mutation_failure(entry_result.status(), owns_transaction)};
            }

            if(!entry_result.value().has_value()) {
                break;
            }

            const auto& entry = *entry_result.value();

            auto row_result = record::RowCodec::decode(*schema, entry.value);
            if(!row_result.ok()) {
                return ExecutionResult{handle_mutation_failure(row_result.status(), owns_transaction)};
            }

            auto indexed_key_result = record::RowHelpers::indexed_key_bytes(
                *schema,
                row_result.value(),
                statement.indexed_column.ordinal
            );
            if(!indexed_key_result.ok()) {
                return ExecutionResult{handle_mutation_failure(indexed_key_result.status(), owns_transaction)};
            }

            auto index_key = std::move(indexed_key_result.value());
            if(!index_descriptor->unique()) {
                index_key.insert(index_key.end(), entry.key.begin(), entry.key.end());
            }

            const auto insert_status = index_tree.insert(index_key, entry.key);
            if(!insert_status.ok()) {
                return ExecutionResult{handle_mutation_failure(insert_status, owns_transaction)};
            }

        }

        if(!owns_transaction) {
            return ExecutionResult{core::Status::Ok(), "Index '"+statement.index_name+"' created"};
        }

        const auto commit_status = pager_->commit_transaction();
        if(!commit_status.ok()) {
            return ExecutionResult{commit_status};
        }

        const auto catalog_status = catalog_.on_transaction_committed();
        if(!catalog_status.ok()) {
            return ExecutionResult{catalog_status};
        }

        return ExecutionResult{commit_status, "Index '"+statement.index_name+"' created"};

    }

    ExecutionResult Database::execute_drop_table_statement(const sql::BoundDropTableStatement& statement) {

        const auto status = catalog_.drop_table(statement.table_name);
        if(!status.ok()) {
            return ExecutionResult{status};
        }

        return ExecutionResult{status, "Table '"+statement.table_name+"' dropped"};

    }

    ExecutionResult Database::execute_select_statement(const sql::BoundSelectStatement& statement) {

        // Resolve metadata, prepare result

        const auto* table_descriptor = catalog_.find_table(statement.table_id);
        if(table_descriptor == nullptr) {
            return ExecutionResult{core::Status::InternalError("Cannot execute SELECT: bound table is missing from catalog")};
        }

        const auto* schema = catalog_.schema_for_table(statement.table_id);
        if(schema == nullptr) {
            return ExecutionResult{core::Status::InternalError("Cannot execute SELECT: bound table has no schema")};
        }

        ExecutionResult::RowSet row_set;
        row_set.column_names.reserve(statement.projection.size());

        for(const auto& column: statement.projection) {
            row_set.column_names.push_back(schema->column(column.ordinal).name());
        }

        // Convert literal to predicate type

        std::optional<record::Value> predicate_value;

        if(statement.predicate.has_value()) {
            const auto& predicate = *statement.predicate;

            const bool is_null_predicate = (
                predicate.comparison_operator == sql::ComparisonOperator::IsNull ||
                predicate.comparison_operator == sql::ComparisonOperator::IsNotNull
            );

            if(!is_null_predicate) {
                if(!predicate.literal.has_value()) {
                    return ExecutionResult{core::Status::InternalError("Cannot execute SELECT: comparison predicate has no literal")};
                }

                if(predicate.literal->is_null()) {
                    return ExecutionResult{core::Status::Ok(), std::nullopt, std::move(row_set)};
                }

                const auto& predicate_column = schema->column(predicate.column.ordinal);

                auto predicate_value_result = predicate.literal->convert_to(
                    predicate_column.logical_type(),
                    predicate_column.nullable()
                );
                if(!predicate_value_result.ok()) {
                    return ExecutionResult{predicate_value_result.status()};
                }

                predicate_value = std::move(predicate_value_result.value());
            }
        }

        auto process_row_bytes = [&statement, schema, &predicate_value, &row_set](
            std::span<const std::byte>,
            std::span<const std::byte> row_bytes
        ) -> core::Status {

            auto row_result = record::RowCodec::decode(*schema, row_bytes);
            if(!row_result.ok()) {
                return row_result.status();
            }

            const auto& row = row_result.value();

            if(statement.predicate.has_value()) {
                auto matches_result = row_matches_predicate(row, *statement.predicate, predicate_value);
                if(!matches_result.ok()) {
                    return matches_result.status();
                }

                if(!matches_result.value()) {
                    return core::Status::Ok();
                }
            }

            std::vector<record::Value> projected_values;
            projected_values.reserve(statement.projection.size());

            for(const auto& column: statement.projection) {
                projected_values.push_back(row.value(column.ordinal));
            }

            row_set.rows.emplace_back(std::move(projected_values));
            return core::Status::Ok();

        };

        auto table_tree_result = open_table_tree(*table_descriptor);
        if(!table_tree_result.ok()) return ExecutionResult{table_tree_result.status()};

        auto table_tree = std::move(table_tree_result.value());

        const auto access_path = plan_access_path(
            *schema,
            catalog_.indexes_for_table(statement.table_id),
            statement.predicate
        );

        const auto access_path_status = std::visit(
            [&](const auto& path) -> core::Status {

                using AccessPathType = std::decay_t<decltype(path)>;

                if constexpr(std::is_same_v<AccessPathType, FullTableScanPath>) {

                    auto cursor_result = table_tree.scan();
                    if(!cursor_result.ok()) {
                        return cursor_result.status();
                    }

                    std::vector<btree::BTreeCursor> cursors;
                    cursors.push_back(std::move(cursor_result.value()));

                    return consume_table_cursors(cursors, process_row_bytes);

                } else if constexpr(std::is_same_v<AccessPathType, PrimaryKeyRangePath>) {

                    if(!predicate_value.has_value()) {
                        return core::Status::InternalError("Cannot execute SELECT: primary-key access path has no converted literal value");
                    }

                    auto key_result = record::KeyCodec::encode(*predicate_value);
                    if(!key_result.ok()) {
                        return key_result.status();
                    }

                    auto cursors_result = open_table_cursors(
                        table_tree,
                        statement.predicate->comparison_operator,
                        key_result.value()
                    );
                    if(!cursors_result.ok()) {
                        return cursors_result.status();
                    }

                    auto cursors = std::move(cursors_result.value());
                    
                    return consume_table_cursors(cursors, process_row_bytes);

                } else {

                    if(!predicate_value.has_value()) {
                        return core::Status::InternalError("Cannot execute SELECT: secondary-index access path has no converted literal value");
                    }

                    auto index_tree_result = open_index_tree(path.index_descriptor);
                    if(!index_tree_result.ok()) {
                        return index_tree_result.status();
                    }

                    auto key_result = record::KeyCodec::encode(*predicate_value);
                    if(!key_result.ok()) {
                        return key_result.status();
                    }

                    auto cursors_result = open_secondary_index_cursors(
                        index_tree_result.value(),
                        statement.predicate->comparison_operator,
                        key_result.value()
                    );
                    if(!cursors_result.ok()) {
                        return cursors_result.status();
                    }

                    auto cursors = std::move(cursors_result.value());

                    return consume_secondary_index_cursors(cursors, table_tree, process_row_bytes);

                }
                
            },
            access_path
        );
        if(!access_path_status.ok()) {
            return ExecutionResult{access_path_status};
        }

        return ExecutionResult{core::Status::Ok(), std::nullopt, std::move(row_set)};

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

        auto tree_result = open_table_tree(*table_descriptor);
        if(!tree_result.ok()) return ExecutionResult{tree_result.status()};

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

        const auto index_descriptors = catalog_.indexes_for_table(statement.table_id);

        for(const auto& index_descriptor: index_descriptors) {

            if(index_descriptor.primary()) continue;

            const auto* indexed_column = catalog_.find_column(statement.table_id, index_descriptor.indexed_column_id());
            if(indexed_column == nullptr) {
                return ExecutionResult{handle_mutation_failure(
                    core::Status::InternalError("Cannot execute INSERT: indexed column is missing from catalog"),
                    owns_transaction
                )};
            }

            auto index_tree_result = open_index_tree(index_descriptor);
            if(!index_tree_result.ok()) {
                return ExecutionResult{handle_mutation_failure(index_tree_result.status(), owns_transaction)};
            }

            auto index_key_result = record::RowHelpers::indexed_key_bytes(
                *schema,
                row_result.value(),
                indexed_column->ordinal()
            );
            if(!index_key_result.ok()) {
                return ExecutionResult{handle_mutation_failure(index_key_result.status(), owns_transaction)};
            }

            auto index_key = std::move(index_key_result.value());

            if(!index_descriptor.unique()) {
                index_key.insert(
                    index_key.end(),
                    primary_key_bytes_result.value().begin(),
                    primary_key_bytes_result.value().end()
                );
            }

            const auto index_insert_status = index_tree_result.value().insert(
                index_key,
                primary_key_bytes_result.value()
            );
            if(!index_insert_status.ok()) {
                return ExecutionResult{handle_mutation_failure(index_insert_status, owns_transaction)};
            }

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

    ExecutionResult Database::execute_update_statement(const sql::BoundUpdateStatement& statement) {

        // Validate metadata and convert values

        const auto* table_descriptor = catalog_.find_table(statement.table_id);
        if(table_descriptor == nullptr) {
            return ExecutionResult{core::Status::InternalError("Cannot execute UPDATE: bound table is missing from catalog")};
        }

        const auto* schema = catalog_.schema_for_table(statement.table_id);
        if(schema == nullptr) {
            return ExecutionResult{core::Status::InternalError("Cannot execute UPDATE: bound table has no schema")};
        }

        const auto& assignment_column = schema->column(statement.assignment.column.ordinal);
        auto assignment_value_result = statement.assignment.value.convert_to(assignment_column.logical_type(), assignment_column.nullable());
        if(!assignment_value_result.ok()) return ExecutionResult{assignment_value_result.status()};

        const std::vector<std::size_t> assignment_ordinals{ statement.assignment.column.ordinal };
        const std::vector<record::Value> assignment_values{ std::move(assignment_value_result.value()) };

        std::optional<record::Value> predicate_value;

        if(statement.predicate.has_value()) {

            const auto& predicate = *statement.predicate;
            const bool is_null_predicate = (
                predicate.comparison_operator == sql::ComparisonOperator::IsNull ||
                predicate.comparison_operator == sql::ComparisonOperator::IsNotNull
            );

            if(!is_null_predicate) {

                if(!predicate.literal.has_value()) {
                    return ExecutionResult{core::Status::InternalError("Cannot execute UPDATE: comparison predicate has no literal")};
                }

                if(predicate.literal->is_null()) {
                    return ExecutionResult{core::Status::Ok(), std::nullopt, std::nullopt, std::size_t{ 0 }};
                }

                const auto& predicate_column = schema->column(predicate.column.ordinal);
                auto predicate_value_result = predicate.literal->convert_to(predicate_column.logical_type(), predicate_column.nullable());
                if(!predicate_value_result.ok()) return ExecutionResult{predicate_value_result.status()};

                predicate_value = std::move(predicate_value_result.value());

            }

        }

        // Open the table and plan candidate access

        auto table_tree_result = open_table_tree(*table_descriptor);
        if(!table_tree_result.ok()) return ExecutionResult{table_tree_result.status()};

        auto table_tree = std::move(table_tree_result.value());
        const auto index_descriptors = catalog_.indexes_for_table(statement.table_id);
        const auto access_path = plan_access_path(*schema, index_descriptors, statement.predicate);

        // Begin the statement transaction

        const bool owns_transaction = !pager_->in_transaction();

        if(owns_transaction) {
            const auto begin_status = pager_->begin_transaction();
            if(!begin_status.ok()) return ExecutionResult{begin_status};
        }

        // Open indexes affected by the assignment

        struct AffectedIndex {
            btree::BTree tree;
            std::size_t indexed_column_ordinal;
            bool unique;
        };

        std::vector<AffectedIndex> affected_indexes;
        affected_indexes.reserve(index_descriptors.size());

        for(const auto& index_descriptor: index_descriptors) {

            if(index_descriptor.primary()) continue;

            const auto* indexed_column = catalog_.find_column(statement.table_id, index_descriptor.indexed_column_id());

            if(indexed_column == nullptr) {
                return ExecutionResult{handle_mutation_failure(core::Status::InternalError("Cannot execute UPDATE: indexed column is missing from catalog"), owns_transaction)};
            }

            if(indexed_column->ordinal() != statement.assignment.column.ordinal) continue;

            auto index_tree_result = open_index_tree(index_descriptor);
            if(!index_tree_result.ok()) return ExecutionResult{handle_mutation_failure(index_tree_result.status(), owns_transaction)};

            affected_indexes.push_back(AffectedIndex{
                std::move(index_tree_result.value()),
                indexed_column->ordinal(),
                index_descriptor.unique()
            });

        }

        std::size_t rows_affected = 0;

        // Process one candidate row

        auto process_update_candidate = [&](std::span<const std::byte> primary_key, std::span<const std::byte> row_bytes) -> core::Status {

            auto row_result = record::RowCodec::decode(*schema, row_bytes);
            if(!row_result.ok()) return row_result.status();

            const auto& row = row_result.value();

            if(statement.predicate.has_value()) {

                auto matches_result = row_matches_predicate(row, *statement.predicate, predicate_value);
                if(!matches_result.ok()) return matches_result.status();
                if(!matches_result.value()) return core::Status::Ok();

            }

            auto updated_row_result = record::RowHelpers::replace_non_primary_key_values(*schema, row, assignment_ordinals, assignment_values);
            if(!updated_row_result.ok()) return updated_row_result.status();

            auto updated_row_bytes_result = record::RowCodec::encode(*schema, updated_row_result.value());
            if(!updated_row_bytes_result.ok()) return updated_row_bytes_result.status();

            struct PendingIndexUpdate {

                AffectedIndex* index;
                std::vector<std::byte> old_key;
                std::vector<std::byte> new_key;

            };

            std::vector<PendingIndexUpdate> pending_index_updates;
            pending_index_updates.reserve(affected_indexes.size());

            for(auto& affected_index: affected_indexes) {

                auto old_index_key_result = record::RowHelpers::indexed_key_bytes(*schema, row, affected_index.indexed_column_ordinal);
                if(!old_index_key_result.ok()) return old_index_key_result.status();

                auto new_index_key_result = record::RowHelpers::indexed_key_bytes(*schema, updated_row_result.value(), affected_index.indexed_column_ordinal);
                if(!new_index_key_result.ok()) return new_index_key_result.status();

                auto old_index_key = std::move(old_index_key_result.value());
                auto new_index_key = std::move(new_index_key_result.value());

                if(!affected_index.unique) {
                    old_index_key.insert(old_index_key.end(), primary_key.begin(), primary_key.end());
                    new_index_key.insert(new_index_key.end(), primary_key.begin(), primary_key.end());
                }

                if(old_index_key == new_index_key) continue;

                pending_index_updates.push_back(PendingIndexUpdate{
                    &affected_index,
                    std::move(old_index_key),
                    std::move(new_index_key)
                });

            }

            for(auto& pending_index_update: pending_index_updates) {
                const auto erase_status = pending_index_update.index->tree.erase(pending_index_update.old_key);
                if(!erase_status.ok()) return erase_status;
            }

            const auto update_status = table_tree.update_value(primary_key, updated_row_bytes_result.value());
            if(!update_status.ok()) return update_status;

            for(auto& pending_index_update: pending_index_updates) {
                const auto insert_status = pending_index_update.index->tree.insert(pending_index_update.new_key, primary_key);
                if(!insert_status.ok()) return insert_status;
            }

            rows_affected++;
            return core::Status::Ok();

        };

        // Execute the selected access path

        const auto update_rows_status = std::visit(
            [&](const auto& path) -> core::Status {

                using AccessPathType = std::decay_t<decltype(path)>;

                if constexpr(std::is_same_v<AccessPathType, FullTableScanPath>) {

                    auto cursor_result = table_tree.scan();
                    if(!cursor_result.ok()) return cursor_result.status();

                    std::vector<btree::BTreeCursor> cursors;
                    cursors.push_back(std::move(cursor_result.value()));

                    return consume_table_cursors(cursors, process_update_candidate);

                } else if constexpr(std::is_same_v<AccessPathType, PrimaryKeyRangePath>) {

                    const bool has_primary_key_predicate = (
                        statement.predicate.has_value() &&
                        predicate_value.has_value()
                    );

                    if(!has_primary_key_predicate) {
                        return core::Status::InternalError("Cannot execute UPDATE: primary-key access path has no converted literal value");
                    }

                    auto key_result = record::KeyCodec::encode(*predicate_value);
                    if(!key_result.ok()) return key_result.status();

                    auto cursors_result = open_table_cursors(table_tree, statement.predicate->comparison_operator, key_result.value());
                    if(!cursors_result.ok()) return cursors_result.status();

                    auto cursors = std::move(cursors_result.value());
                    return consume_table_cursors(cursors, process_update_candidate);

                } else {

                    const bool has_secondary_index_predicate = (
                        statement.predicate.has_value() &&
                        predicate_value.has_value()
                    );

                    if(!has_secondary_index_predicate) {
                        return core::Status::InternalError("Cannot execute UPDATE: secondary-index access path has no converted literal value");
                    }

                    struct PendingUpdate {
                        std::vector<std::byte> primary_key;
                        std::vector<std::byte> row_bytes;
                    };

                    std::vector<PendingUpdate> pending_updates;

                    {

                        auto index_tree_result = open_index_tree(path.index_descriptor);
                        if(!index_tree_result.ok()) return index_tree_result.status();

                        auto key_result = record::KeyCodec::encode(*predicate_value);
                        if(!key_result.ok()) return key_result.status();

                        auto cursors_result = open_secondary_index_cursors(index_tree_result.value(), statement.predicate->comparison_operator, key_result.value());
                        if(!cursors_result.ok()) return cursors_result.status();

                        auto cursors = std::move(cursors_result.value());

                        const auto pending_updates_status = consume_secondary_index_cursors(
                            cursors,
                            table_tree,
                            [&](std::span<const std::byte> primary_key, std::span<const std::byte> row_bytes) -> core::Status {

                                pending_updates.push_back(PendingUpdate{
                                    std::vector<std::byte>{ primary_key.begin(), primary_key.end() },
                                    std::vector<std::byte>{ row_bytes.begin(), row_bytes.end() }
                                });

                                return core::Status::Ok();

                            }
                        );
                        if(!pending_updates_status.ok()) return pending_updates_status;

                    }

                    for(const auto& pending_update: pending_updates) {

                        const auto update_status = process_update_candidate(pending_update.primary_key, pending_update.row_bytes);
                        if(!update_status.ok()) return update_status;

                    }

                    return core::Status::Ok();

                }

            },
            access_path
        );

        if(!update_rows_status.ok()) {
            return ExecutionResult{handle_mutation_failure(update_rows_status, owns_transaction)};
        }

        // Finish the transaction

        if(!owns_transaction) {
            return ExecutionResult{core::Status::Ok(), std::nullopt, std::nullopt, rows_affected};
        }

        const auto commit_status = pager_->commit_transaction();
        if(!commit_status.ok()) return ExecutionResult{commit_status};

        const auto catalog_status = catalog_.on_transaction_committed();
        if(!catalog_status.ok()) return ExecutionResult{catalog_status};

        return ExecutionResult{commit_status, std::nullopt, std::nullopt, rows_affected};

    }

    ExecutionResult Database::execute_delete_statement(const sql::BoundDeleteStatement& statement) {

        const auto* table_descriptor = catalog_.find_table(statement.table_id);
        if(table_descriptor == nullptr) {
            return ExecutionResult{core::Status::InternalError("Cannot execute DELETE: bound table is missing from catalog")};
        }

        const auto* schema = catalog_.schema_for_table(statement.table_id);
        if(schema == nullptr) {
            return ExecutionResult{core::Status::InternalError("Cannot execute DELETE: bound table has no schema")};
        }

        std::optional<record::Value> predicate_value;

        if(statement.predicate.has_value()) {

            const auto& predicate = *statement.predicate;
            const bool is_null_predicate = (
                predicate.comparison_operator == sql::ComparisonOperator::IsNull ||
                predicate.comparison_operator == sql::ComparisonOperator::IsNotNull
            );

            if(!is_null_predicate) {
                if(!predicate.literal.has_value()) {
                    return ExecutionResult{core::Status::InternalError("Cannot execute DELETE: comparison predicate has no literal")};
                }

                if(predicate.literal->is_null()) {
                    return ExecutionResult{core::Status::Ok(), std::nullopt, std::nullopt, std::size_t{ 0 }};
                }

                const auto& predicate_column = schema->column(predicate.column.ordinal);
                auto predicate_value_result = predicate.literal->convert_to(predicate_column.logical_type(), predicate_column.nullable());
                if(!predicate_value_result.ok()) {
                    return ExecutionResult{predicate_value_result.status()};
                }

                predicate_value = std::move(predicate_value_result.value());
            }

        }

        auto tree_result = open_table_tree(*table_descriptor);
        if(!tree_result.ok()) return ExecutionResult{tree_result.status()};

        auto tree = std::move(tree_result.value());
        std::vector<btree::BTreeCursor> cursors;

        const bool uses_primary_key = (
            statement.predicate.has_value() &&
            predicate_value.has_value() &&
            statement.predicate->column.ordinal == schema->primary_key_ordinal()
        );

        if(!uses_primary_key) {
            auto cursor_result = tree.scan();
            if(!cursor_result.ok()) {
                return ExecutionResult{cursor_result.status()};
            }

            cursors.push_back(std::move(cursor_result.value()));
        } else {
            auto key_result = record::KeyCodec::encode(*predicate_value);
            if(!key_result.ok()) {
                return ExecutionResult{key_result.status()};
            }

            auto cursors_result = open_table_cursors(tree, statement.predicate->comparison_operator, key_result.value());
            if(!cursors_result.ok()) {
                return ExecutionResult{cursors_result.status()};
            }

            cursors = std::move(cursors_result.value());
        }

        const bool owns_transaction = !pager_->in_transaction();
        if(owns_transaction) {
            const auto begin_status = pager_->begin_transaction();
            if(!begin_status.ok()) {
                return ExecutionResult{begin_status};
            }
        }

        struct AffectedIndex {
            btree::BTree tree;
            std::size_t indexed_column_ordinal;
            bool unique;
        };

        struct PendingDeletion {
            std::vector<std::byte> primary_key;
            record::Row row;
        };

        std::vector<AffectedIndex> affected_indexes;
        const auto index_descriptors = catalog_.indexes_for_table(statement.table_id);
        affected_indexes.reserve(index_descriptors.size());

        for(const auto& index_descriptor: index_descriptors) {

            if(index_descriptor.primary()) continue;

            const auto* indexed_column = catalog_.find_column(
                statement.table_id,
                index_descriptor.indexed_column_id()
            );
            if(indexed_column == nullptr) {
                return ExecutionResult{handle_mutation_failure(
                    core::Status::InternalError("Cannot execute DELETE: indexed column is missing from catalog"),
                    owns_transaction
                )};
            }

            auto index_tree_result = open_index_tree(index_descriptor);
            if(!index_tree_result.ok()) {
                return ExecutionResult{handle_mutation_failure(index_tree_result.status(), owns_transaction)};
            }

            affected_indexes.push_back(AffectedIndex{
                std::move(index_tree_result.value()),
                indexed_column->ordinal(),
                index_descriptor.unique()
            });

        }

        std::vector<PendingDeletion> pending_deletions;

        for(auto& cursor: cursors) {
            while(true) {
                auto entry_result = cursor.next();
                if(!entry_result.ok()) {
                    return ExecutionResult{handle_mutation_failure(entry_result.status(), owns_transaction)};
                }

                if(!entry_result.value().has_value()) {
                    break;
                }

                const auto& entry = *entry_result.value();
                auto row_result = record::RowCodec::decode(*schema, entry.value);
                if(!row_result.ok()) {
                    return ExecutionResult{handle_mutation_failure(row_result.status(), owns_transaction)};
                }

                const auto& row = row_result.value();

                if(statement.predicate.has_value()) {
                    auto matches_result = row_matches_predicate(row, *statement.predicate, predicate_value);
                    if(!matches_result.ok()) {
                        return ExecutionResult{handle_mutation_failure(matches_result.status(), owns_transaction)};
                    }

                    if(!matches_result.value()) {
                        continue;
                    }
                }

                pending_deletions.push_back(PendingDeletion{
                    entry.key,
                    std::move(row_result.value())
                });
            }
        }

        std::size_t rows_affected = 0;

        for(const auto& pending_deletion: pending_deletions) {

            for(auto& maintained_index: affected_indexes) {

                auto index_key_result = record::RowHelpers::indexed_key_bytes(
                    *schema,
                    pending_deletion.row,
                    maintained_index.indexed_column_ordinal
                );
                if(!index_key_result.ok()) {
                    return ExecutionResult{handle_mutation_failure(index_key_result.status(), owns_transaction)};
                }

                auto index_key = std::move(index_key_result.value());

                if(!maintained_index.unique) {
                    index_key.insert(
                        index_key.end(),
                        pending_deletion.primary_key.begin(),
                        pending_deletion.primary_key.end()
                    );
                }

                const auto index_erase_status = maintained_index.tree.erase(index_key);
                if(!index_erase_status.ok()) {
                    return ExecutionResult{handle_mutation_failure(index_erase_status, owns_transaction)};
                }

            }

            const auto erase_status = tree.erase(pending_deletion.primary_key);
            if(!erase_status.ok()) {
                return ExecutionResult{handle_mutation_failure(erase_status, owns_transaction)};
            }

            rows_affected++;
        }

        if(!owns_transaction) {
            return ExecutionResult{core::Status::Ok(), std::nullopt, std::nullopt, rows_affected};
        }

        const auto commit_status = pager_->commit_transaction();
        if(!commit_status.ok()) {
            return ExecutionResult{commit_status};
        }

        const auto catalog_status = catalog_.on_transaction_committed();
        if(!catalog_status.ok()) {
            return ExecutionResult{catalog_status};
        }

        return ExecutionResult{commit_status, std::nullopt, std::nullopt, rows_affected};

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

    core::Result<btree::BTree> Database::open_table_tree(const catalog::TableDescriptor& table_descriptor) const {

        const auto* schema = catalog_.schema_for_table(table_descriptor.table_id());
        if(schema == nullptr) {
            return core::Status::InternalError("Cannot open table tree: table schema is missing from catalog");
        }

        return btree::BTree::open_existing(
            *pager_,
            table_descriptor.root_page_id(),
            static_cast<std::uint16_t>(schema->primary_key_column().logical_type().fixed_size()),
            static_cast<std::uint16_t>(schema->row_size())
        );

    }

    core::Result<btree::BTree> Database::open_index_tree(const catalog::IndexDescriptor& index_descriptor) const {

        if(index_descriptor.primary()) {
            return core::Status::InvalidArgument("Cannot open index tree: primary index must be opened as a table tree");
        }

        const auto* schema = catalog_.schema_for_table(index_descriptor.table_id());
        if(schema == nullptr) {
            return core::Status::InternalError("Cannot open index tree: table schema is missing from catalog");
        }

        const auto* indexed_column = catalog_.find_column(index_descriptor.table_id(), index_descriptor.indexed_column_id());
        if(indexed_column == nullptr) {
            return core::Status::InternalError("Cannot open index tree: indexed column is missing from catalog");
        }

        const std::size_t indexed_key_size = indexed_column->logical_type().fixed_size();
        const std::size_t primary_key_size = schema->primary_key_column().logical_type().fixed_size();
        const std::size_t key_size = index_descriptor.unique()
            ? indexed_key_size
            : indexed_key_size+primary_key_size;

        return btree::BTree::open_existing(
            *pager_,
            index_descriptor.root_page_id(),
            static_cast<std::uint16_t>(key_size),
            static_cast<std::uint16_t>(primary_key_size)
        );

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

}
