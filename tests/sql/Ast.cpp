#include <catch_amalgamated.hpp>

#include <dandb/sql/Ast.h>

#include <vector>

using dandb::record::LogicalType;
using dandb::sql::ColumnDefinition;
using dandb::sql::ColumnConstraints;
using dandb::sql::ColumnType;
using dandb::sql::CreateTableStatement;
using dandb::sql::Identifier;
using dandb::sql::SourceLocation;

namespace {

    template<typename T>
    constexpr bool supports_not_null = requires(T column_definition) {
        column_definition.constraints.not_null = true;
    };

}

TEST_CASE("ColumnDefinition records whether NOT NULL was specified", "[sql][ast]") {
    REQUIRE(supports_not_null<ColumnDefinition>);
}

TEST_CASE("AST constructs a CREATE TABLE statement", "[sql][ast]") {
    const SourceLocation create_location{1, 1};
    const SourceLocation users_location{1, 14};
    const SourceLocation id_location{2, 3};
    const SourceLocation int64_location{2, 6};
    const SourceLocation name_location{3, 3};
    const SourceLocation string_location{3, 8};

    CreateTableStatement statement{
        Identifier{"users", users_location},
        std::vector<ColumnDefinition>{
            ColumnDefinition{
                Identifier{"id", id_location},
                ColumnType{LogicalType::int64(), int64_location},
                ColumnConstraints{true, false, false},
                id_location
            },
            ColumnDefinition{
                Identifier{"name", name_location},
                ColumnType{LogicalType::string(64).value(), string_location},
                ColumnConstraints{false, false, true},
                name_location
            }
        },
        create_location
    };

    REQUIRE(statement.table_name.text == "users");
    REQUIRE(statement.columns.size() == 2);
    REQUIRE(statement.columns[0].constraints.primary_key);
    REQUIRE(statement.columns[1].constraints.not_null);
    REQUIRE(statement.columns[1].type.logical_type.capacity() == 64);
}
