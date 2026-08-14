#include <dandb/sql/Binder.h>

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

}
