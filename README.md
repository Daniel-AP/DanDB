# DanDB

DanDB is a small relational database engine that I built from scratch in C++20. It accepts a subset of SQL and stores the data in its own persistent, page-based format. I made it to understand how databases work internally by building one myself.

Reading about database internals helped, but implementing them made the ideas much easier to understand.

## What It Can Do

DanDB covers the basic workflow of a relational database. It can create tables and indexes, insert, query, update, and delete rows, enforce primary-key, unique, and non-null constraints, and group changes in explicit transactions. Committed data remains available after reopening the database, and checkpoints keep the main database file up to date.

Its scope is intentionally focused. DanDB supports a limited part of SQL and currently runs on Windows.

## Requirements

- Windows
- CMake 3.20 or newer
- Ninja
- g++ with C++20 support

## Build and Test

The repository includes CMake presets for debug and release builds. From the project root, the complete debug workflow is:

```powershell
cmake --preset gxx-debug
cmake --build --preset gxx-debug
ctest --preset gxx-debug
```

## In Practice

After building the project, pass `dandb_cli` a path. It will either open the database at that location or create a new one:

```powershell
.\out\build\gxx-debug\dandb_cli.exe .\example.dandb
```

Once connected, you can enter SQL statements directly:

```sql
CREATE TABLE users (id INT64 PRIMARY KEY, name STRING(64), active BOOL NOT NULL);
INSERT INTO users VALUES (1, 'Ada', TRUE);
SELECT * FROM users;
```

Statements end with a semicolon. The `.help` command shows the available operations, and `.exit` closes the program. Opening `example.dandb` again brings back the committed rows stored in it.

## Documentation

The design journal continues from the system overview into each major layer:

1. [The Architecture of DanDB](website/content/posts/the-architecture-of-dandb.md)
2. [From SQL Text to Execution](website/content/posts/from-sql-text-to-execution.md)
3. [The Catalog and Data Model](website/content/posts/the-catalog-and-data-model.md)
4. [Tables and Indexes as B+ Trees](website/content/posts/tables-and-indexes-as-b-trees.md)
5. [The Pager and Buffer Pool](website/content/posts/the-pager-and-buffer-pool.md)
6. [Transactions and the Write-Ahead Log](website/content/posts/transactions-and-the-write-ahead-log.md)
7. [Recovery and Checkpoints](website/content/posts/recovery-and-checkpoints.md)

The repository also contains concise references for the [supported SQL grammar](docs/sql-grammar.txt), [database and WAL file formats](docs/file-format.txt), and [system tables](docs/system-tables.txt).
