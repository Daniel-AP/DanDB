#include <dandb/sql/Binder.h>

#include <dandb/record/LiteralValue.h>
#include <dandb/catalog/Catalog.h>
#include <dandb/catalog/SystemTables.h>

#include <string>
#include <type_traits>
#include <utility>

namespace {

    std::string make_binder_error_message(dandb::sql::SourceLocation location, std::string message) {
        return "SQL error at line "+std::to_string(location.line)+
            ", column "+std::to_string(location.column)+": "+std::move(message);
    }

}

namespace dandb::sql {

    Binder::Binder(const catalog::Catalog& catalog) :
        catalog_(catalog)
    {}

    core::Result<BoundStatement> Binder::bind(const Statement& statement) const {
        return std::visit(
            [this](const auto& current_statement) -> core::Result<BoundStatement> {

                using CurrentStatement = std::decay_t<decltype(current_statement)>;

                if constexpr(std::is_same_v<CurrentStatement, SelectStatement>) {
                    auto result = bind_select_statement(current_statement);
                    if(!result.ok()) return result.status();
                    return BoundStatement{std::move(result.value())};
                } else if constexpr(std::is_same_v<CurrentStatement, InsertStatement>) {
                    auto result = bind_insert_statement(current_statement);
                    if(!result.ok()) return result.status();
                    return BoundStatement{std::move(result.value())};
                } else if constexpr(std::is_same_v<CurrentStatement, UpdateStatement>) {
                    auto result = bind_update_statement(current_statement);
                    if(!result.ok()) return result.status();
                    return BoundStatement{std::move(result.value())};
                } else if constexpr(std::is_same_v<CurrentStatement, DeleteStatement>) {
                    auto result = bind_delete_statement(current_statement);
                    if(!result.ok()) return result.status();
                    return BoundStatement{std::move(result.value())};
                } else if constexpr(std::is_same_v<CurrentStatement, CreateIndexStatement>) {
                    auto result = bind_create_index_statement(current_statement);
                    if(!result.ok()) return result.status();
                    return BoundStatement{std::move(result.value())};
                } else if constexpr(std::is_same_v<CurrentStatement, DropTableStatement>) {
                    auto result = bind_drop_table_statement(current_statement);
                    if(!result.ok()) return result.status();
                    return BoundStatement{std::move(result.value())};
                } else {
                    return BoundStatement{current_statement};
                }

            },
            statement
        );
    }

    core::Result<BoundSelectStatement> Binder::bind_select_statement(const SelectStatement& statement) const {

        auto table_result = bind_table(statement.table_name);
        if(!table_result.ok()) return table_result.status();

        auto projection_result = bind_projection(table_result.value(), statement.projection);
        if(!projection_result.ok()) return projection_result.status();

        BoundSelectStatement bound_statement{
            table_result.value(),
            std::move(projection_result.value()),
            std::nullopt
        };

        if(statement.predicate.has_value()) {
            auto predicate_result = bind_predicate(bound_statement.table_id, *statement.predicate);
            if(!predicate_result.ok()) return predicate_result.status();
            bound_statement.predicate = std::move(predicate_result.value());
        }

        return bound_statement;

    }

    core::Result<BoundInsertStatement> Binder::bind_insert_statement(const InsertStatement& statement) const {

        auto table_result = bind_table(statement.table_name);
        if(!table_result.ok()) return table_result.status();

        const auto non_system_table_status = validate_non_system_table(table_result.value());
        if(!non_system_table_status.ok()) return non_system_table_status;

        std::vector<record::LiteralValue> values;

        for(const auto& expr: statement.values) {
            values.push_back(expr.value);
        }

        const auto* schema = catalog_.schema_for_table(table_result.value());
        if(schema == nullptr) {
            return core::Status::InternalError("Resolved table has no schema");
        }

        const std::size_t schema_column_count = schema->column_count();

        if(values.size() != schema_column_count) {
            return core::Status::InvalidArgument(make_binder_error_message(
                statement.location,
                "INSERT has "+std::to_string(values.size())+" values but table '"+statement.table_name.text+
                    "' has "+std::to_string(schema_column_count)+" columns"
            ));
        }

        return BoundInsertStatement{
            table_result.value(),
            std::move(values)
        };

    }

    core::Result<BoundUpdateStatement> Binder::bind_update_statement(const UpdateStatement& statement) const {

        auto table_result = bind_table(statement.table_name);
        if(!table_result.ok()) return table_result.status();

        const auto non_system_table_status = validate_non_system_table(table_result.value());
        if(!non_system_table_status.ok()) return non_system_table_status;

        auto assignment_result = bind_assignment(table_result.value(), statement.assignment);
        if(!assignment_result.ok()) return assignment_result.status();

        BoundUpdateStatement bound_statement{
            table_result.value(),
            std::move(assignment_result.value()),
            std::nullopt
        };

        if(statement.predicate.has_value()) {
            auto predicate_result = bind_predicate(table_result.value(), *statement.predicate);
            if(!predicate_result.ok()) return predicate_result.status();
            bound_statement.predicate = std::move(predicate_result.value());
        }

        return bound_statement;

    }

    core::Result<BoundDeleteStatement> Binder::bind_delete_statement(const DeleteStatement& statement) const {

        auto table_result = bind_table(statement.table_name);
        if(!table_result.ok()) return table_result.status();

        const auto non_system_table_status = validate_non_system_table(table_result.value());
        if(!non_system_table_status.ok()) return non_system_table_status;

        BoundDeleteStatement bound_statement{
            table_result.value(),
            std::nullopt
        };

        if(statement.predicate.has_value()) {
            auto predicate_result = bind_predicate(bound_statement.table_id, *statement.predicate);
            if(!predicate_result.ok()) return predicate_result.status();
            bound_statement.predicate = std::move(predicate_result.value());
        }

        return bound_statement;
        
    }

    core::Result<BoundCreateIndexStatement> Binder::bind_create_index_statement(const CreateIndexStatement& statement) const {

        auto table_result = bind_table(statement.table_name);
        if(!table_result.ok()) return table_result.status();

        const auto non_system_table_status = validate_non_system_table(table_result.value());
        if(!non_system_table_status.ok()) return non_system_table_status;

        auto column_result = bind_column(table_result.value(), statement.column_name);
        if(!column_result.ok()) return column_result.status();

        return BoundCreateIndexStatement{
            statement.index_name.text,
            table_result.value(),
            std::move(column_result.value()),
            statement.unique
        };

    }

    core::Result<BoundDropTableStatement> Binder::bind_drop_table_statement(const DropTableStatement& statement) const {

        auto table_result = bind_table(statement.table_name);
        if(!table_result.ok()) return table_result.status();

        const auto non_system_table_status = validate_non_system_table(table_result.value());
        if(!non_system_table_status.ok()) return non_system_table_status;

        return BoundDropTableStatement{
            table_result.value(),
            statement.table_name.text
        };

    }

    core::Result<catalog::TableId> Binder::bind_table(const Identifier& table_name) const {

        const auto* table = catalog_.find_table(table_name.text);
        if(table == nullptr) {
            return core::Status::NotFound(make_binder_error_message(table_name.location, "Table '"+table_name.text+"' does not exist"));
        }

        return table->table_id();

    }

    core::Result<BoundColumn> Binder::bind_column(catalog::TableId table_id, const Identifier& column_name) const {

        const auto* column = catalog_.find_column(table_id, column_name.text);
        if(column == nullptr) {
            return core::Status::NotFound(make_binder_error_message(column_name.location, "Column '"+column_name.text+"' does not exist"));
        }

        return BoundColumn{
            column->column_id(),
            column->ordinal()
        };

    }

    core::Result<std::vector<BoundColumn>> Binder::bind_projection(catalog::TableId table_id, const SelectProjection& projection) const {

        std::vector<BoundColumn> bound_columns;

        if(std::holds_alternative<SelectAll>(projection)) {

            const auto* schema = catalog_.schema_for_table(table_id);
            if(schema == nullptr) {
                return core::Status::InternalError("Resolved table has no schema");
            }

            const auto& select_all = std::get<SelectAll>(projection);
            bound_columns.reserve(schema->column_count());

            for(const auto& column: schema->columns()) {
                auto column_result = bind_column(table_id, Identifier{column.name(), select_all.location});
                if(!column_result.ok()) return column_result.status();
                bound_columns.push_back(std::move(column_result.value()));
            }

            return bound_columns;

        }

        const auto& select_columns = std::get<SelectColumns>(projection);
        bound_columns.reserve(select_columns.columns.size());

        for(const auto& column_name: select_columns.columns) {
            auto column_result = bind_column(table_id, column_name);
            if(!column_result.ok()) return column_result.status();
            bound_columns.push_back(std::move(column_result.value()));
        }

        return bound_columns;

    }

    core::Result<BoundPredicate> Binder::bind_predicate(catalog::TableId table_id, const Predicate& predicate) const {

        auto column_result = bind_column(table_id, predicate.column_name);
        if(!column_result.ok()) return column_result.status();

        std::optional<record::LiteralValue> literal;
        if(predicate.literal.has_value()) {
            literal = predicate.literal->value;
        }

        return BoundPredicate{
            std::move(column_result.value()),
            predicate.comparison_operator,
            std::move(literal)
        };

    }

    core::Result<BoundAssignment> Binder::bind_assignment(catalog::TableId table_id, const Assignment& assignment) const {

        auto column_result = bind_column(table_id, assignment.column_name);
        if(!column_result.ok()) return column_result.status();

        const auto* schema = catalog_.schema_for_table(table_id);
        if(schema == nullptr) {
            return core::Status::InternalError("Resolved table has no schema");
        }

        if(column_result.value().ordinal == schema->primary_key_ordinal()) {
            return core::Status::InvalidArgument(make_binder_error_message(assignment.column_name.location, "Cannot update primary key column '"+assignment.column_name.text+"'"));
        }

        return BoundAssignment{
            std::move(column_result.value()),
            assignment.value.value
        };

    }

    core::Status Binder::validate_non_system_table(catalog::TableId table_id) const {

        bool is_system_table = (
            table_id == catalog::DANDB_TABLES_ID ||
            table_id == catalog::DANDB_COLUMNS_ID ||
            table_id == catalog::DANDB_INDEXES_ID ||
            table_id == catalog::DANDB_INDEX_COLUMNS_ID
        );

        if(is_system_table) {
            return core::Status::InvalidArgument("Cannot write to a system table");
        }

        return core::Status::Ok();

    }

}
