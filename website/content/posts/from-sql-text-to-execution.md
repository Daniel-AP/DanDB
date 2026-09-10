---
title: "From SQL Text to Execution"
weight: 3
---

A SQL statement may look complete to the person writing it, but DanDB cannot act on the text directly. It first has to recognize the words and symbols, understand their structure, connect the names to the open database, and choose the code that will perform the operation.

Consider this query against the `users` table created in the first post:

```sql
SELECT name FROM users WHERE active = TRUE;
```

Before DanDB reads a row, this query passes through the lexer, parser, and binder. Each stage produces a more useful representation and answers a different question about the input.

## The Pipeline

The command-line interface passes the accumulated text to `Database::execute`. DanDB tokenizes the complete input, parses it into one or more statements, then binds and executes each statement in order.

The separation is important because valid SQL has more than one meaning of "valid." The lexer can decide whether the characters form recognizable tokens. The parser can decide whether those tokens follow DanDB's grammar. Neither can decide whether a table called `users` exists. That requires the binder and the catalog for the database that is currently open.

The REPL also uses this boundary to handle multiline input. If the lexer or parser reports `IncompleteInput`, it keeps collecting text. Commands such as `.help` and `.exit` are handled by the REPL itself and never enter the SQL pipeline.

## Lexing

The lexer reads the source from left to right and produces a sequence of `Token` values. Each token contains a category, the original text, and the line and column where it began.

For the example query, a simplified token list looks like this:

```text
Select            "SELECT"    1:1
Identifier        "name"      1:8
From              "FROM"      1:13
Identifier        "users"     1:18
Where             "WHERE"     1:24
Identifier        "active"    1:30
Equal             "="         1:37
BooleanLiteral    "TRUE"      1:39
Semicolon         ";"         1:43
EndOfInput        ""          1:44
```

At this point, `name`, `users`, and `active` are only identifiers. The lexer does not know that one names a table and the others name columns. It also recognizes `TRUE` as a Boolean literal, but it does not know whether `active` accepts Boolean values.

Reserved words are matched without regard to letter case, so `select`, `SELECT`, and `SeLeCt` produce the same token kind. Identifier text is preserved instead. This distinction lets SQL keywords remain case-insensitive while table, column, and index names keep the exact spelling used by the database.

Numbers, strings, Boolean values, null values, punctuation, and comparison operators also receive their own token kinds. Whitespace updates the source position but does not become a token. If the lexer encounters an unsupported character, it reports the location immediately instead of passing ambiguous input to the parser.

DanDB scans the input directly in C++ instead of using regular expressions. The token rules are small, and reading one character at a time gives the lexer direct control over source positions, multi-character operators, and unfinished string literals.

An unfinished string literal is treated differently. It produces `IncompleteInput` because another line could still complete the statement. This is how the lexer participates in the REPL without knowing anything about prompts or terminal behavior.

## Parsing

The parser receives tokens and turns them into statement structures. DanDB uses a handwritten recursive-descent parser with a function for each supported statement or grammar part. `parse_select_statement`, for example, reads the projection, table name, and optional predicate in the order required by the grammar.

This choice fits the current grammar. DanDB supports a focused set of statements with little ambiguity and no general expression language, so each grammar rule maps cleanly to a small C++ function.

Another option would be to describe the grammar outside the C++ source and generate the parser. A Python script could perform that generation, but Python would only be development tooling. The generated C++ would still parse queries while DanDB is running.

A parser generator such as ANTLR or Bison is the more established version of that approach. It can be valuable for a much larger grammar with complex expressions, precedence rules, or ambiguity. For DanDB's current grammar, that benefit does not justify another tool, a generation step, and generated output in the build.

The handwritten parser also gives DanDB direct control over errors. Each parsing function knows what it is trying to read, so it can report that a projection, literal, closing parenthesis, or statement terminator is missing at the exact source location. More importantly, `expect_kind` can distinguish reaching the end of unfinished input from encountering a token that makes the statement invalid. The first case returns `IncompleteInput` and lets the REPL continue reading. The second returns a final parse error.

A generated parser can also provide precise errors, but doing so usually requires configuring its recovery rules and adapting its default diagnostics to the behavior the application needs. With DanDB's small grammar, keeping those decisions in ordinary C++ makes them visible and straightforward to test. This is not an advantage of handwritten parsers in every project. The cost is that every grammar branch, diagnostic, and future change must be implemented and maintained manually.

The example becomes a structure conceptually equivalent to this:

```text
SelectStatement
  projection: SelectColumns ["name"]
  table_name: "users"
  predicate
    column_name: "active"
    comparison_operator: Equal
    literal: Boolean true
```

This is an abstract syntax tree, usually shortened to AST. It records the structure of the statement without keeping irrelevant details such as whitespace. Identifiers and expressions retain source locations so a later error can still point back to the relevant part of the original SQL.

The parser also converts literal text into DanDB values when the conversion does not depend on a column. Integer and double lexemes are parsed with range checking. Boolean, string, and null tokens become their corresponding literal expressions. Column-specific validation still cannot happen here because the parser has no catalog or schema.

Every supported statement has its own structure, and `Statement` is a `std::variant` over that closed set. This makes the valid statement types explicit in the C++ type system. Code that visits a `Statement` must account for each alternative, without requiring a shared runtime inheritance hierarchy.

The trade-off is also explicit. Adding a new statement requires a new structure and updates to the variant and its visitors. For DanDB's focused grammar, that cost keeps the possible paths visible instead of hiding them behind a more general representation.

`Parser::parse` can return several statements from the same input. It requires a terminator after each one and preserves their order. A missing terminator at the end is reported as incomplete input, while a token that cannot continue the current statement is a final parse error.

## Binding

Parsing proves that the query has a supported shape. Binding connects that shape to the database that is open now.

`Binder` receives the parsed `Statement` and a read-only reference to `Catalog`. For the example query, it looks up `users`, then resolves the projected `name` column and the predicate's `active` column. The resulting `BoundSelectStatement` no longer depends on repeated name searches:

```cpp
struct BoundColumn {
    catalog::ColumnId column_id;
    std::size_t ordinal;
};

struct BoundSelectStatement {
    catalog::TableId table_id;
    std::vector<BoundColumn> projection;
    std::optional<BoundPredicate> predicate;
};
```

The IDs identify catalog objects. The ordinals identify each column's position inside a row and preserve the requested projection order. Keeping both lets later code retrieve metadata by identity while accessing row values by position.

Binding also performs checks that require catalog context. A referenced table or column must exist. `SELECT *` expands into bound columns in schema order. An `INSERT` must provide the same number of values as the table has columns. An `UPDATE` cannot target the primary-key column. Operations that would modify protected system tables are rejected here as well.

These checks are related to database structure, but they do not include every value rule. A bound statement stores table and column identity, not a copy of every logical type. During execution, DanDB reads the schema and converts literals for their target columns. This keeps parsing independent of the database and avoids turning the bound representation into another copy of the catalog.

Some statements do not need existing table or column references. Those can pass through the binder in their parsed form. The binder enriches a statement only when resolving database objects makes the next step more precise.

## Execution

`Database::execute_statement` receives one parsed statement, checks the current transaction state, binds the statement, and dispatches the resulting `BoundStatement` with `std::visit`. Each alternative goes to its matching execution function.

For the example `SELECT`, execution retrieves the table descriptor and schema, converts the predicate literal using the `active` column's logical type, and decides which access path can answer the predicate. It then hands the request to the relevant table or index path. Row decoding and B+ tree access begin beyond this point and belong to the later data-model and indexing posts.

Other statement types follow the same entry pattern even though their work differs. The important guarantee at this boundary is that statement-specific execution receives the resolved objects and positions it needs. It does not have to reinterpret the original SQL string.

The result travels back through `ExecutionResult`. It always contains a status and may also contain a success message, a row set, or an affected-row count. The REPL formats that structured result for the terminal. Formatting is kept outside the SQL and execution stages, so `Database` does not depend on a particular presentation.

When one input contains several statements, DanDB executes them in order and records one result for each completed statement. It stops at the first failure rather than continuing with operations whose assumptions may no longer hold.

## Error Boundaries

Each stage reports errors about the information it owns. This keeps a missing table from looking like malformed syntax and prevents an invalid character from reaching execution.

- The lexer reports unexpected characters and unfinished string literals.
- The parser reports unsupported or malformed statement structure and numeric literals outside the accepted range.
- The binder reports missing tables or columns and operations that conflict with the resolved database structure.
- Execution reports value conversion, constraint, transaction, storage, and I/O failures.

Line and column information begins in the lexer and remains attached to parsed identifiers and expressions long enough for parser and binder errors to reference the original input. The status category then tells the caller whether the input is incomplete or has failed.

That distinction matters inside an explicit transaction. Incomplete input leaves the transaction usable because the statement has not finished yet. A real lexer, parser, binder, or execution failure marks the transaction as failed, and DanDB requires `ROLLBACK` before accepting more work. The transaction and recovery posts will cover what happens to changed pages. Here, the important point is that the SQL pipeline communicates the failure state without trying to manage durability itself.
