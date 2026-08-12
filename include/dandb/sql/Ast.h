#pragma once

#include <dandb/record/LiteralValue.h>
#include <dandb/record/LogicalType.h>
#include <dandb/sql/Token.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dandb::sql {

    struct Identifier {
        std::string text;
        SourceLocation location;
    };

    struct ColumnType {
        record::LogicalType logical_type;
        SourceLocation location;
    };

    struct ColumnDefinition {
        Identifier name;
        ColumnType type;
        bool primary_key;
        bool unique;
        bool not_null;
        SourceLocation location;
    };

    struct LiteralExpression {
        record::LiteralValue value;
        SourceLocation location;
    };

    enum class ComparisonOperator {
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        IsNull,
        IsNotNull
    };

    struct Predicate {
        Identifier column_name;
        ComparisonOperator comparison_operator;
        std::optional<LiteralExpression> literal;
        SourceLocation location;
    };

    struct Assignment {
        Identifier column_name;
        LiteralExpression value;
        SourceLocation location;
    };

    struct SelectAll {
        SourceLocation location;
    };

    struct SelectColumns {
        std::vector<Identifier> columns;
        SourceLocation location;
    };

    using SelectProjection = std::variant<SelectAll, SelectColumns>;

    struct CreateTableStatement {
        Identifier table_name;
        std::vector<ColumnDefinition> columns;
        SourceLocation location;
    };

    struct DropTableStatement {
        Identifier table_name;
        SourceLocation location;
    };

    struct CreateIndexStatement {
        Identifier index_name;
        Identifier table_name;
        Identifier column_name;
        bool unique;
        SourceLocation location;
    };

    struct DropIndexStatement {
        Identifier index_name;
        SourceLocation location;
    };

    struct InsertStatement {
        Identifier table_name;
        std::vector<LiteralExpression> values;
        SourceLocation location;
    };

    struct SelectStatement {
        SelectProjection projection;
        Identifier table_name;
        std::optional<Predicate> predicate;
        SourceLocation location;
    };

    struct UpdateStatement {
        Identifier table_name;
        Assignment assignment;
        std::optional<Predicate> predicate;
        SourceLocation location;
    };

    struct DeleteStatement {
        Identifier table_name;
        std::optional<Predicate> predicate;
        SourceLocation location;
    };

    struct BeginStatement {
        SourceLocation location;
    };

    struct CommitStatement {
        SourceLocation location;
    };

    struct RollbackStatement {
        SourceLocation location;
    };

    struct CheckpointStatement {
        SourceLocation location;
    };

    using Statement = std::variant<
        CreateTableStatement,
        DropTableStatement,
        CreateIndexStatement,
        DropIndexStatement,
        InsertStatement,
        SelectStatement,
        UpdateStatement,
        DeleteStatement,
        BeginStatement,
        CommitStatement,
        RollbackStatement,
        CheckpointStatement
    >;

}
