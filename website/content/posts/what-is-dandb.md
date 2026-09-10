---
title: "What Is DanDB?"
weight: 1
---

DanDB is a small relational database engine that I built from scratch in C++20. It accepts a subset of SQL and stores the data in its own persistent, page-based format. I made it to understand how databases work internally by building one myself.

Reading about database internals helped, but implementing them made the ideas much easier to understand.

## What It Can Do

DanDB covers the basic workflow of a relational database. It can create tables and indexes, insert, query, update, and delete rows, enforce primary-key, unique, and non-null constraints, and group changes in explicit transactions. Committed data remains available after reopening the database, and checkpoints keep the main database file up to date.

Its scope is intentionally focused. DanDB supports a limited part of SQL, runs on Windows.

## In Practice

DanDB runs locally as a command-line program. Pass it a path and it will either open the database at that location or create a new one:

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

## Build and Test

The repository includes CMake presets for a C++20 build with g++ and Ninja. From the project root, the complete debug workflow is:

```powershell
cmake --preset gxx-debug
cmake --build --preset gxx-debug
ctest --preset gxx-debug
```

The first command prepares the build, the second compiles DanDB and its test executables, and the third runs the automated test suite.
