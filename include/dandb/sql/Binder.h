#pragma once

#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/TableId.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/record/LiteralValue.h>
#include <dandb/sql/Ast.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dandb::catalog {
    class Catalog;
}

namespace dandb::sql {

    struct BoundColumn {
        catalog::ColumnId column_id;
        std::size_t ordinal;
    };

    struct BoundPredicate {
        BoundColumn column;
        ComparisonOperator comparison_operator;
        std::optional<record::LiteralValue> literal;
    };

    struct BoundAssignment {
        BoundColumn column;
        record::LiteralValue value;
    };

    struct BoundSelectStatement {
        catalog::TableId table_id;
        std::vector<BoundColumn> projection;
        std::optional<BoundPredicate> predicate;
    };

    struct BoundInsertStatement {
        catalog::TableId table_id;
        std::vector<record::LiteralValue> values;
    };

    struct BoundUpdateStatement {
        catalog::TableId table_id;
        BoundAssignment assignment;
        std::optional<BoundPredicate> predicate;
    };

    struct BoundDeleteStatement {
        catalog::TableId table_id;
        std::optional<BoundPredicate> predicate;
    };

    struct BoundCreateIndexStatement {
        std::string index_name;
        catalog::TableId table_id;
        BoundColumn indexed_column;
        bool unique;
    };

    struct BoundDropTableStatement {
        catalog::TableId table_id;
        std::string table_name;
    };

    using BoundStatement = std::variant<
        CreateTableStatement,
        BoundDropTableStatement,
        BoundCreateIndexStatement,
        DropIndexStatement,
        BoundInsertStatement,
        BoundSelectStatement,
        BoundUpdateStatement,
        BoundDeleteStatement,
        BeginStatement,
        CommitStatement,
        RollbackStatement,
        CheckpointStatement
    >;

    class Binder {
        public:
            explicit Binder(const catalog::Catalog& catalog);

            core::Result<BoundStatement> bind(const Statement& statement) const;

        private:
            const catalog::Catalog& catalog_;

            core::Result<BoundSelectStatement> bind_select_statement(const SelectStatement& statement) const;
            core::Result<BoundInsertStatement> bind_insert_statement(const InsertStatement& statement) const;
            core::Result<BoundUpdateStatement> bind_update_statement(const UpdateStatement& statement) const;
            core::Result<BoundDeleteStatement> bind_delete_statement(const DeleteStatement& statement) const;
            core::Result<BoundCreateIndexStatement> bind_create_index_statement(const CreateIndexStatement& statement) const;
            core::Result<BoundDropTableStatement> bind_drop_table_statement(const DropTableStatement& statement) const;

            core::Result<catalog::TableId> bind_table(const Identifier& table_name) const;
            core::Result<BoundColumn> bind_column(catalog::TableId table_id, const Identifier& column_name) const;
            core::Result<std::vector<BoundColumn>> bind_projection(catalog::TableId table_id, const SelectProjection& projection) const;
            core::Result<BoundPredicate> bind_predicate(catalog::TableId table_id, const Predicate& predicate) const;
            core::Result<BoundAssignment> bind_assignment(catalog::TableId table_id, const Assignment& assignment) const;

            core::Status validate_non_system_table(catalog::TableId table_id, const Identifier& table_name, std::string_view action) const;
    };

}
