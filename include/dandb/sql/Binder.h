#pragma once

#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/TableId.h>
#include <dandb/record/LiteralValue.h>
#include <dandb/sql/Ast.h>

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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

}
