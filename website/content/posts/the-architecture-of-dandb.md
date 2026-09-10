---
title: "The Architecture of DanDB"
weight: 2
---

DanDB takes SQL text and turns it into data stored on disk. The first parts figure out what the SQL means. The next parts read or change rows and indexes. The last parts manage pages, transactions, and files.

## Overview

![Top-down map of the DanDB architecture](../../diagrams/dandb-architecture.svg)

*A SQL statement passes through four main parts. Each part does one kind of work and hands the result to the next.*

The diagram reads top-down, although some parts are shared. The catalog helps both binding and execution. The pager brings together pages from memory, the write-ahead log (WAL), and the main database file.

### SQL Processing

The command-line interface sends SQL text to `execution::Database`, which manages the work for an open database. The lexer splits the text into tokens. The parser then uses those tokens to describe what DanDB should do.

A parsed statement still contains names such as `users`, `id`, or `email`. Before DanDB can use them, it needs to know which table and columns those names refer to. The binder gets that information from the catalog and adds the identifiers, column positions, and types that execution needs.

This keeps SQL syntax separate from the contents of a specific database. The parser can recognize a valid `SELECT` (for example). The binder checks whether the table and columns exist and finds their schema information.

### Execution

`Database` receives the bound statement and brings together the catalog, rows, B+ trees, and transaction state needed to run it. Reads and changes use the same data structures, but they use them in different ways.

For a read, DanDB uses the query condition and catalog information to choose a full table scan, a primary-key lookup, or a secondary-index lookup. A secondary index stores the primary key of the matching row. DanDB uses that key to find the complete row in the table tree.

For an `INSERT`, `UPDATE`, or `DELETE`, DanDB checks the new values against the schema and updates the table tree. It also updates every affected secondary index in the same transaction. Creating or dropping a table or index uses the same transaction system, so catalog information and stored data change together.

### Storage

Execution works with schemas, values, and rows. Storage needs bytes that can be written to pages. The record layer connects the two by checking values, encoding rows, and creating keys that B+ trees can compare in the correct order.

DanDB uses B+ trees in three places. A table tree connects each primary key to its row. A secondary-index tree connects an indexed value to the row's primary key. The catalog has its own trees for system tables (tables, columns and indexes).

All of these trees use the same path to storage. A B+ tree knows its root page and the size of its keys and values, but it never reads a file directly. It asks the pager to load, create, and change pages. `BTreeCursor` handles ordered scans, and a changed page is marked as dirty so the pager knows it must be saved.

### Persistence

The pager sits between the B+ trees and the files on disk. It works with the buffer pool, transactions, the WAL manager, the disk manager, the database header, and pages recovered from the write-ahead log.

When a B+ tree asks for a page, the pager first checks whether it is already in memory (buffer pool). If it is not, the pager looks for a committed copy recovered from the WAL. If there is no such copy, it reads the page from the main database file. The page stays pinned in memory while another part of DanDB is using it.

All changes belong to a transaction until it commits or rolls back. On commit, DanDB writes the changed pages to the WAL and makes them available to later reads. It does not copy them to the main database file right away. A checkpoint does that later, then resets the WAL.

The same design is used when DanDB opens an existing database. It checks the main file and its WAL, finds the latest pages from committed transactions, and makes those pages available through the pager. The catalog loads after this step, so catalog information and user data come from the same recovered state.

## Read Path

Consider a query such as:

```sql
SELECT name FROM users WHERE id = 1;
```

Here is what happens:

1. The CLI sends the SQL text to `Database`.
2. The lexer and parser turn it into a statement.
3. The binder asks the catalog what `users`, `name`, and `id` refer to.
4. DanDB sees that `id` is the primary key and opens the table's B+ tree.
5. The tree asks the pager for the pages needed to find the row.
6. The pager gets those pages from memory, the recovered WAL state, or the main database file.
7. DanDB decodes the row and takes the requested `name` value.
8. The value returns to the CLI for display.

Each part answers a different question. SQL processing finds out what the query means. The catalog describes the table. The B+ tree finds the stored bytes. The record layer turns those bytes back into values.

## Write Path

An insertion starts the same way, then adds the work needed to save the change:

```sql
INSERT INTO users VALUES (2, 'Grace', TRUE);
```

Here is what happens:

1. SQL processing identifies the target table and its column types.
2. DanDB converts the values, checks the row, and encodes its primary key and data.
3. The table's B+ tree stores the primary key and row.
4. Each secondary index stores its key together with the row's primary key.
5. The pager tracks every page changed or created by the transaction.
6. Commit writes those pages and a commit record to the WAL, then syncs the file.
7. The pager makes the committed pages the current database state.
8. A later checkpoint copies those pages to the main database file.

If a change fails before commit, DanDB rolls back the transaction instead of keeping only some table or index changes. An explicit transaction uses the same parts, but lets several statements commit or roll back together.
