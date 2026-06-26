# DanDB Plan

This file is the ordered implementation plan for rebuilding DanDB from this repository while using the old codebase only as a legacy reference.

The old project now lives next to this repository as `../DanDB-legacy`. This repository is the `DanDB` project. Do not mechanically move the old source tree into the new repository. Copy only small, understood fragments when a task explicitly says the old implementation is still useful.

The plan is intentionally detailed. Each task is written so that a future chat can start from that task, read the listed files, assume all previous tasks were completed by someone else, and finish the task without needing the whole design conversation again.

## Product Shape

DanDB is an educational SQLite-inspired database engine with a CLI/REPL as the user-facing feature. There is no separate public C++ API feature in the scope. Internal C++ classes are still required because the code must be clean, testable, and readable, but the user should experience the database through SQL statements in the CLI.

The database should prioritize correctness, readability, durability, and learning value over advanced SQL coverage or maximum performance. Performance should be reasonable for the selected architecture, but DanDB is not trying to compete with production databases.

## Locked Scope

- Language and build: C++20, CMake, Catch2, Windows-first.
- No GitHub Actions in this plan.
- User-facing feature: CLI/REPL that opens one database file and runs SQL statements.
- SQL comments are not supported in v1.
- Documentation site is postponed. During implementation, capture lightweight notes for a future casual build journal or project site.
- Database filename: user opens a main file such as `mydb.dandb`.
- WAL filename: append `.wal` to the opened database path, for example `mydb.dandb.wal`.
- Page size: 4096 bytes.
- Page 0: database header page.
- Page allocation v1: append-only page IDs. No free-list and no page reuse yet.
- Durability model: WAL-only. No rollback journal.
- Checkpointing: manual only through SQL, for example `CHECKPOINT;`.
- Concurrency model: one process and one writer. Use an exclusive lock so two processes do not open the same database for writing.
- Transaction model: manual transactions from the start plus autocommit statements.
- Transaction SQL: `BEGIN;`, `COMMIT;`, `ROLLBACK;`, `CHECKPOINT;`.
- Error rule: if any statement inside a manual transaction fails, the transaction becomes failed and unusable. Only `ROLLBACK;` is accepted after that.
- Recovery rule: on open, recover committed WAL frames. Ignore incomplete or checksum-invalid trailing WAL bytes only if they are after the last complete committed transaction. Fail loudly on committed corruption.
- Testability: include test-only fault injection hooks for write, sync, and recovery tests.
- Normal table storage: clustered B+ tree keyed by primary key.
- Every table must have exactly one declared single-column primary key. There is no hidden rowid in v1.
- Primary keys are indexed by the table B+ tree itself.
- Each table may have at most one user-created secondary index.
- Each table may have any number of valid single-column unique constraints. These create internal unique indexes and do not count as the one optional user-created secondary index.
- Composite primary keys, composite indexes, joins, aggregation, ALTER TABLE, and advanced SQL are out of scope.

## SQL Scope

Supported types:

```text
INT8
INT16
INT32
INT64
DOUBLE
STRING(N)
BOOL
NULL values
```

Important type rules:

- `BOOL` is stored internally as one byte.
- `INT8`, `INT16`, `INT32`, and `INT64` must reject overflow.
- `STRING(N)` stores at most `N` bytes. Overflow is an error.
- `DOUBLE` is allowed in table rows but should not be allowed as a primary key or index key in v1.
- `NULL` can be stored in nullable non-key columns.
- Primary key columns are always not-null.
- In v1, unique indexed columns should be treated as not-null unless a later task deliberately implements full SQL NULL uniqueness behavior. This keeps uniqueness enforcement understandable.
- Type conversion should be strict. Do not silently coerce unsafe values.

Supported DDL:

```sql
CREATE TABLE table_name (
  id INT64 PRIMARY KEY,
  name STRING(64),
  active BOOL,
  email STRING(120) UNIQUE
);

DROP TABLE table_name;

CREATE INDEX index_name ON table_name(column_name);
CREATE UNIQUE INDEX index_name ON table_name(column_name);
DROP INDEX index_name;
```

Supported DML:

```sql
INSERT INTO table_name VALUES (1, 'Ada', true, 'ada@example.com');

SELECT * FROM table_name;
SELECT id, name FROM table_name WHERE id = 1;

UPDATE table_name SET name = 'Grace' WHERE id = 1;

DELETE FROM table_name WHERE id = 1;
```

Simplifications:

- `INSERT` always provides a full row in schema order. No named column list and no default expressions in v1.
- `UPDATE` cannot change the primary key column.
- `WHERE` supports one simple predicate in v1.
- Supported predicate operators: `=`, `!=`, `<`, `<=`, `>`, `>=`, `IS NULL`, `IS NOT NULL`.
- No joins.
- No aggregation.
- No grouping.
- No ordering.
- No limit.
- No EXPLAIN in v1.
- Keywords are case-insensitive.
- Identifiers are simple unquoted names: letters, digits, and underscore, not starting with a digit.

## Recommended Source Layout For The New Repo

```text
DanDB/
  CMakeLists.txt
  CMakePresets.json
  README.md
  PLAN.md
  include/
    dandb/
      core/
      platform/
      storage/
      wal/
      buffer/
      transaction/
      record/
      btree/
      catalog/
      sql/
      execution/
      cli/
  src/
    core/
    platform/
    storage/
    wal/
    buffer/
    transaction/
    record/
    btree/
    catalog/
    sql/
    execution/
    cli/
  tests/
    core/
    platform/
    storage/
    wal/
    buffer/
    transaction/
    record/
    btree/
    catalog/
    sql/
    execution/
    cli/
    testutil/
  third_party/
    catch2/
```

This layout is more modular than the legacy project because the new design has clearer boundaries: platform I/O, raw page storage, WAL, buffer pool, pager, record encoding, B+ trees, catalog, SQL, execution, and CLI.

## Legacy Code Reference Map

Read legacy files only when a task points to them. Treat them as examples, not as source of truth.

```text
../DanDB-legacy/include/dandb/core/Status.h
../DanDB-legacy/include/dandb/core/Result.h
../DanDB-legacy/include/dandb/core/Constants.h
../DanDB-legacy/src/storage/DiskManager.cpp
../DanDB-legacy/include/dandb/storage/DiskManager.h
../DanDB-legacy/src/buffer/BufferPoolManager.cpp
../DanDB-legacy/include/dandb/buffer/BufferPoolManager.h
../DanDB-legacy/src/buffer/LRUReplacer.cpp
../DanDB-legacy/include/dandb/buffer/LRUReplacer.h
../DanDB-legacy/include/dandb/buffer/Page.h
../DanDB-legacy/src/record/Value.cpp
../DanDB-legacy/include/dandb/record/Value.h
../DanDB-legacy/src/record/Schema.cpp
../DanDB-legacy/include/dandb/record/Schema.h
../DanDB-legacy/src/record/Layout.cpp
../DanDB-legacy/include/dandb/record/Layout.h
../DanDB-legacy/src/record/Codec.cpp
../DanDB-legacy/include/dandb/record/Codec.h
../DanDB-legacy/src/record/Row.cpp
../DanDB-legacy/include/dandb/record/Row.h
../DanDB-legacy/src/catalog/TableManager.cpp
../DanDB-legacy/include/dandb/catalog/TableManager.h
../DanDB-legacy/tests/
```

Do not carry these legacy decisions into the new architecture:

- Do not keep `tables.meta`.
- Do not make `PagedTable` the foundation of normal tables.
- Do not let `BufferPoolManager` call `DiskManager` directly for durability decisions.
- Do not use `std::fstream` as the final durable file abstraction.
- Do not implement the old free-list in v1.
- Do not build a heap table first and then migrate it. Normal tables are B+ trees from the beginning.

## Documentation Direction

Do not spend implementation time building a documentation website yet.

Each task has a `Doc note` item. Capture one or two sentences after finishing the task if you want. At the end, use those notes to create a casual learning site or build journal. Better final options than a large formal docs portal are:

- A simple Markdown build journal inside `docs/`.
- A small static site using Astro or Starlight if you want it to feel like a personal project site.
- A small VitePress site if you want a fast Markdown-centered docs site.
- A README plus blog posts if you want the lowest-maintenance option.

Docusaurus remains possible, but it is not the default recommendation for this project because it may feel too large and formal for the tone you want.

## Task Format

Each task uses this structure:

- Read first: files or concepts to inspect before changing code.
- Goal: the single thing the task should accomplish.
- Build: concrete files, classes, methods, and behavior to create.
- Requirements: rules that must be true when the task is done.
- Tests to request: tests that should exist by the end of the task.
- Common mistakes: risks to watch for.
- Done when: observable completion condition.
- Doc note: future blog or explanation note to capture.

Do tasks in order. Later tasks assume earlier tasks are complete.

## Day 01 - New Repository Foundation

### D01-T01 - Create the DanDB repository skeleton

Read first:

- `../DanDB-legacy/CMakeLists.txt`
- `../DanDB-legacy/CMakePresets.json`
- `../DanDB-legacy/AGENTS.md`

Goal:

Create the empty new repository structure for DanDB without copying legacy implementation code.

Build:

- Create the new `DanDB` folder after the legacy repo has been renamed.
- Add the source layout shown in this plan.
- Add empty `.gitignore`, `README.md`, `PLAN.md`, and top-level `CMakeLists.txt`.
- Add placeholder directories for `include/dandb/...`, `src/...`, and `tests/...`.
- Do not add any database logic yet.

Requirements:

- The folder structure must make architecture boundaries visible.
- The repository must not contain the old `PagedTable`, old `TableManager`, old `DiskManager`, or old generated build outputs.
- Keep names simple and predictable. Avoid clever folder names.

Tests to request:

- No runtime tests yet.
- A simple check that CMake can configure once the next task adds targets.

Common mistakes:

- Copying the entire old `src/` or `include/` tree.
- Creating a layout that hides important boundaries under one large `engine` folder.
- Adding documentation site tooling too early.

Done when:

- The new repo exists.
- The folders are present.
- No legacy implementation code has been copied yet.

Doc note:

- Capture why the rewrite starts from a clean repo instead of mutating the legacy codebase.

### D01-T02 - Add a minimal CMake and Catch2 test target

Read first:

- `../DanDB-legacy/CMakeLists.txt`
- `../DanDB-legacy/third_party/catch2/`

Goal:

Make the new repository compile and run a trivial test before any database logic exists.

Build:

- Add a root `CMakeLists.txt` using C++20.
- Add a static library target named `dandb_core` or `dandb`.
- Add a CLI executable target named `dandb_cli`.
- Add a Catch2 test executable named `dandb_tests`.
- Vendor or copy the existing Catch2 folder from the legacy repo only if it is small and already working.
- Add one trivial test file under `tests/core/SmokeTest.cpp`.

Requirements:

- CMake should configure with the selected preset.
- The CLI may only print a placeholder message for now.
- Tests should run and pass.
- Do not add storage classes yet.

Tests to request:

- One smoke test that proves the test binary runs.

Common mistakes:

- Waiting too long to create the test target.
- Mixing CLI code into the core library.
- Making the build depend on generated files.

Done when:

- `cmake --preset ...` configures.
- The test executable builds.
- The smoke test passes.

Doc note:

- Capture the reason for setting up tests before storage code.

### D01-T03 - Add the core error model

Read first:

- `../DanDB-legacy/include/dandb/core/Status.h`
- `../DanDB-legacy/include/dandb/core/Result.h`

Goal:

Create a small, readable error model used by every layer.

Build:

- Add `include/dandb/core/ErrorCode.h`.
- Add `include/dandb/core/Status.h`.
- Add `include/dandb/core/Result.h`.
- Add matching `src/core` files only if implementation cannot stay header-only.
- Use simple types:
  - `enum class ErrorCode`
  - `class Status`
  - `template <typename T> class Result`
- Include codes for at least:
  - `Ok`
  - `InvalidArgument`
  - `IoError`
  - `Corruption`
  - `NotFound`
  - `AlreadyExists`
  - `ConstraintViolation`
  - `TransactionError`
  - `ParseError`
  - `InternalError`

Requirements:

- `Status::ok()` must be easy to check.
- `Result<T>` must either contain a value or a non-ok status.
- Error messages should be human-readable.
- Do not use exceptions for normal database errors.
- Do not overbuild with stack traces or nested causes yet.

Tests to request:

- `Status` ok/error construction.
- `Result<T>` value access when ok.
- `Result<T>` status access when error.

Common mistakes:

- Returning raw booleans from low-level code.
- Throwing exceptions in storage code for expected failures.
- Making error objects too complex too early.

Done when:

- Core status tests pass.
- New code can represent both success and failure clearly.

Doc note:

- Capture the difference between expected database errors and programmer bugs.

### D01-T04 - Define basic constants and strong IDs

Read first:

- `../DanDB-legacy/include/dandb/core/Constants.h`
- `../DanDB-legacy/include/dandb/record/RID.h`

Goal:

Define small shared types that prevent accidental mixing of integers.

Build:

- Add `include/dandb/core/Constants.h`.
- Add `include/dandb/storage/PageId.h`.
- Add `include/dandb/catalog/ObjectId.h`.
- Define:
  - `kPageSize = 4096`
  - `using page_id_t = std::uint64_t` or a small wrapper type
  - `using table_id_t = std::uint64_t`
  - `using index_id_t = std::uint64_t`
  - invalid ID constants
- Decide whether IDs are wrappers or typedefs. Prefer small wrapper structs only if they remain readable.

Requirements:

- Page IDs are 64-bit.
- Page 0 is reserved for the database header.
- First allocatable page is page 1.
- Constants must not depend on storage implementation classes.

Tests to request:

- Basic compile-time checks for page size and invalid IDs.

Common mistakes:

- Using signed integers for page IDs.
- Letting every class define its own page size.
- Hiding constants in `.cpp` files where other layers need them.

Done when:

- Constants are available to storage, WAL, buffer, catalog, and tests.

Doc note:

- Capture why page 0 is special.

### D01-T05 - Add byte encoding helpers

Read first:

- `../DanDB-legacy/src/record/Codec.cpp`
- `../DanDB-legacy/include/dandb/record/Codec.h`

Goal:

Create safe helpers for reading and writing little-endian integers from byte buffers.

Build:

- Add `include/dandb/core/Endian.h`.
- Implement functions such as:
  - `write_u16_le`
  - `write_u32_le`
  - `write_u64_le`
  - `read_u16_le`
  - `read_u32_le`
  - `read_u64_le`
- Use `std::span<std::byte>` or clear pointer plus size helpers.
- Return `Status` or `Result<T>` on bounds errors where appropriate.

Requirements:

- No undefined behavior from unaligned reads.
- No `reinterpret_cast` for serialized integers.
- Helpers must be boring and obvious.
- All on-disk numeric fields use these helpers.

Tests to request:

- Round-trip each integer size.
- Known byte layout tests.
- Bounds failure tests.

Common mistakes:

- Depending on host endianness.
- Reading past the end of a page.
- Serializing structs directly to disk.

Done when:

- Helpers are tested and ready for page, WAL, and record formats.

Doc note:

- Capture why databases avoid writing raw C++ structs to disk.

### D01-T06 - Add checksum utility

Read first:

- No legacy code required.

Goal:

Create a simple checksum function used by WAL frames and headers.

Build:

- Add `include/dandb/core/Checksum.h`.
- Add `src/core/Checksum.cpp`.
- Implement a deterministic checksum such as FNV-1a 64-bit.
- Expose a function that accepts bytes and an optional seed.

Requirements:

- The checksum must be deterministic across platforms.
- It is not a cryptographic hash.
- It must be easy to test and easy to replace later.

Tests to request:

- Empty input checksum.
- Known input checksum.
- Checksum changes when one byte changes.

Common mistakes:

- Treating checksum as security.
- Using platform-dependent hashing.
- Mixing checksum code into WAL manager too early.

Done when:

- The checksum utility has independent tests.

Doc note:

- Capture what checksums can and cannot protect against.

### D01-T07 - Add test utilities for temporary files

Read first:

- Legacy tests under `../DanDB-legacy/tests/`

Goal:

Make file-based tests easy and isolated.

Build:

- Add `tests/testutil/TempDir.h`.
- Add `tests/testutil/TempDir.cpp`.
- Implement helpers to create a unique temporary directory for each test.
- Implement helpers to build paths such as `test.dandb` and `test.dandb.wal`.
- Ensure cleanup is best-effort and does not delete outside the temp directory.

Requirements:

- Tests must not write into the repository root.
- Every file-based test should use this utility.
- Path handling should work on Windows.

Tests to request:

- Temp directory exists.
- Unique directories across two instances.
- Cleanup removes files created inside the temp directory.

Common mistakes:

- Hardcoding one shared test database path.
- Making cleanup dangerous.
- Assuming Unix path separators.

Done when:

- Later storage tests can safely create real files.

Doc note:

- Capture why database tests need isolated files.

### D01-T08 - Establish naming and include conventions

Read first:

- The new repo files created so far.

Goal:

Write down and apply simple code style rules before the project grows.

Build:

- Add a short section to `README.md` or `docs/dev-notes.md`.
- Document namespace `dandb`.
- Document file naming:
  - Public headers under `include/dandb/...`
  - Implementations under `src/...`
  - Tests mirroring module names
- Document class naming and method naming.
- Document that on-disk structures are encoded manually, not by dumping C++ structs.

Requirements:

- Keep the style guide short.
- This is not a formal documentation site.
- The guide should help future chats keep code consistent.

Tests to request:

- No tests needed.

Common mistakes:

- Creating a huge style guide nobody follows.
- Mixing multiple naming styles from different examples.

Done when:

- A future task can read the style note and follow the same conventions.

Doc note:

- This task itself is the note.

### D01-T09 - Day 01 review checkpoint

Read first:

- All files created on Day 01.

Goal:

Confirm the repository foundation is small, understandable, and ready for storage work.

Build:

- Run the full test suite.
- Review the include tree and source tree.
- Remove accidental placeholder files that do not help.
- Add a short `docs/day01-notes.md` only if you want to preserve learning notes.

Requirements:

- No storage logic exists yet.
- Build and test setup works.
- Core helpers are tested.

Tests to request:

- Full current test suite.

Common mistakes:

- Starting file I/O before the foundation is stable.
- Keeping unused scaffolding because it feels harmless.

Done when:

- Day 02 can start with a clean base.

Doc note:

- Capture the first architecture diagram in plain text.

## Day 02 - Platform File I/O

### D02-T01 - Design the platform file boundary

Read first:

- `../DanDB-legacy/src/storage/DiskManager.cpp`
- `../DanDB-legacy/include/dandb/storage/DiskManager.h`

Goal:

Separate durable file operations from database logic.

Build:

- Add `include/dandb/platform/FileHandle.h`.
- Add `src/platform/FileHandle.cpp`.
- Add a Windows implementation first.
- Define operations:
  - `open_existing`
  - `create_new`
  - `open_or_create`
  - `read_at`
  - `write_at`
  - `sync`
  - `size`
  - `truncate`
  - `close`
- Return `Status` or `Result<T>` from every operation.

Requirements:

- Higher layers must not use `std::fstream` directly.
- Reads and writes are positioned operations.
- Partial reads and writes must be handled explicitly.
- The class owns the OS handle and closes it safely.

Tests to request:

- Create and open a file.
- Write bytes at offset 0 and read them back.
- Write bytes at a later offset and verify file size.
- Read beyond EOF returns a clear error or short-read status by design.

Common mistakes:

- Letting `DiskManager` own platform details.
- Assuming one OS call writes all bytes.
- Forgetting to close handles on error paths.

Done when:

- Tests can perform exact positioned file I/O.

Doc note:

- Capture why databases usually need positioned I/O instead of stream-style I/O.

### D02-T02 - Implement exact read and exact write semantics

Read first:

- `include/dandb/platform/FileHandle.h`

Goal:

Make file I/O behavior predictable for page and WAL code.

Build:

- Add `read_exact_at(offset, buffer)` behavior.
- Add `write_all_at(offset, buffer)` behavior.
- Decide how EOF is reported:
  - reading a full page beyond EOF should be a controlled error for raw file I/O;
  - higher layers can decide when missing pages are corruption.
- Ensure repeated OS calls continue until the buffer is complete or an error occurs.

Requirements:

- No caller should need to handle partial successful writes.
- The method names should make exact behavior obvious.
- Error messages should include operation context.

Tests to request:

- Exact read succeeds for full data.
- Exact read fails for short file.
- Exact write writes the entire buffer.

Common mistakes:

- Returning success after a short read.
- Hiding EOF as zero-filled data in the platform layer.

Done when:

- `FileHandle` has deterministic exact I/O methods.

Doc note:

- Capture the difference between short I/O and corruption.

### D02-T03 - Add durable sync behavior

Read first:

- Windows documentation for file flushing if needed.
- The current `FileHandle` implementation.

Goal:

Expose a real file sync operation that WAL commit can rely on.

Build:

- Implement `FileHandle::sync()`.
- On Windows, use the correct file flush primitive for the handle.
- Return a clear `IoError` if flushing fails.
- Do not fake sync with close or stream flush.

Requirements:

- WAL commit later calls this after writing commit records.
- Checkpoint later calls this after copying pages to the main database.
- Tests should be able to call sync even if they cannot simulate power loss yet.

Tests to request:

- Sync succeeds on an open writable file.
- Sync fails or returns a clear error after the handle is closed, if that state is testable.

Common mistakes:

- Confusing C++ stream flush with OS durable sync.
- Ignoring sync errors.

Done when:

- File sync is available and tested enough for WAL work.

Doc note:

- Capture why commit is not durable until data reaches stable storage.

### D02-T04 - Add database file locking

Read first:

- `FileHandle` code from previous tasks.

Goal:

Prevent two DanDB processes from opening the same database for writing.

Build:

- Add `include/dandb/platform/FileLock.h` if keeping lock separate is clearer.
- Implement an exclusive lock for the main database file.
- The lock should be acquired during database open and held until close.
- If another process holds the lock, return a clear error.

Requirements:

- This is a coarse lock. It does not implement concurrent readers.
- The lock belongs near platform code, not SQL or catalog code.
- Tests may use two handles in one process if Windows lock semantics allow it.

Tests to request:

- First lock succeeds.
- Second exclusive lock on same file fails.
- Lock is released when handle closes.

Common mistakes:

- Adding complex reader/writer locking.
- Making locks optional.
- Reporting lock failure as generic corruption.

Done when:

- A database can protect itself from accidental double-open.

Doc note:

- Capture why simple exclusive locking is enough for v1.

### D02-T05 - Add path helpers for main DB and WAL files

Read first:

- Test temp path helper from Day 01.

Goal:

Make file naming consistent.

Build:

- Add `include/dandb/platform/DatabasePath.h`.
- Implement helpers:
  - `main_path()`
  - `wal_path()`
  - `display_name()`
- The WAL path is always `main_path + ".wal"`.
- Do not require the main file to have `.dandb`, but recommend it in messages and README.

Requirements:

- If user opens `demo.dandb`, WAL is `demo.dandb.wal`.
- If user opens `demo`, WAL is `demo.wal`.
- Do not ask the user for a WAL path separately.

Tests to request:

- `demo.dandb` maps to `demo.dandb.wal`.
- Paths with directories work.

Common mistakes:

- Replacing the main extension with `.wal` instead of appending.
- Spreading WAL path construction across the codebase.

Done when:

- Every future component can ask one helper for WAL path.

Doc note:

- Capture the reason for deriving WAL path automatically.

### D02-T06 - Add file fault injection hooks

Read first:

- Current `FileHandle`.

Goal:

Prepare for crash and durability tests without making production code ugly.

Build:

- Add a test-only fault injection interface around file operations.
- Allow tests to fail specific operations:
  - before write
  - after partial write if supported
  - before sync
  - after sync
  - before truncate
- Keep production default as no faults.
- Prefer dependency injection over global mutable state if it stays readable.

Requirements:

- Fault injection must not affect normal builds unless tests enable it.
- It must be possible to test WAL recovery from interrupted writes later.
- Keep hooks narrow and named after file operations.

Tests to request:

- Injected write failure returns `IoError`.
- Injected sync failure returns `IoError`.
- No-fault mode behaves normally.

Common mistakes:

- Building a large mocking framework.
- Making every class aware of fault injection.
- Forgetting to reset injected faults between tests.

Done when:

- Future WAL tests can force controlled I/O failures.

Doc note:

- Capture why crash safety needs failure injection, not just happy-path tests.

### D02-T07 - Day 02 review checkpoint

Read first:

- All platform module files.

Goal:

Confirm file I/O is strong enough before pages and WAL depend on it.

Build:

- Run all tests.
- Check that only platform code talks to OS file APIs.
- Check that error messages include enough context.
- Remove accidental direct file usage elsewhere.

Requirements:

- No storage manager exists yet.
- File operations are tested in isolation.
- Locking and WAL path naming are decided.

Tests to request:

- Full test suite.

Common mistakes:

- Starting `DiskManager` while file operations still have fuzzy semantics.

Done when:

- Day 03 can treat `FileHandle` as reliable infrastructure.

Doc note:

- Capture a small note explaining the platform boundary.

## Day 03 - Main Database File And Page 0

### D03-T01 - Define the database header format

Read first:

- `include/dandb/core/Endian.h`
- `include/dandb/core/Constants.h`

Goal:

Design page 0 as a stable, validated database header.

Build:

- Add `include/dandb/storage/DatabaseHeader.h`.
- Add `src/storage/DatabaseHeader.cpp`.
- Define a fixed header inside page 0:
  - magic bytes, for example `DDB1`
  - format version
  - page size
  - database ID or salt
  - page count
  - catalog root page ID
  - `dandb_tables` root page ID
  - `dandb_columns` root page ID
  - `dandb_indexes` root page ID
  - `dandb_index_columns` root page ID
  - reserved zero-filled bytes for future fields
  - checksum for the header region
- Do not serialize a C++ struct directly.

Requirements:

- Header must fit well inside page 0.
- Unknown future fields must be zero in v1.
- Opening a DB with wrong page size must fail.
- Opening a DB with bad magic must fail.
- Opening a DB with unsupported version must fail.

Tests to request:

- Encode and decode a valid header.
- Reject wrong magic.
- Reject wrong page size.
- Reject bad checksum.
- Reject nonzero reserved bytes if that rule is chosen.

Common mistakes:

- Putting mutable transaction state into the main header.
- Forgetting checksum coverage rules.
- Making page 0 depend on catalog classes.

Done when:

- Header encoding and validation are independent and tested.

Doc note:

- Capture why databases use a magic number and version in the file header.

### D03-T02 - Implement raw page representation

Read first:

- `../DanDB-legacy/include/dandb/buffer/Page.h`

Goal:

Create a simple in-memory page object used by storage, buffer, and B+ tree code.

Build:

- Add `include/dandb/storage/Page.h`.
- Define a `Page` class or struct containing:
  - page ID
  - fixed 4096-byte data buffer
  - helper accessors for bytes
- Keep pin counts and dirty flags out of this raw `Page` if the buffer frame owns them.

Requirements:

- Page data is always exactly 4096 bytes.
- Page object should be easy to zero-initialize.
- Page ID can be invalid for empty buffer frames.

Tests to request:

- Page size is exactly 4096.
- New page data is zeroed if that is the chosen behavior.

Common mistakes:

- Mixing buffer frame state into raw page data too early.
- Allocating page data dynamically for every operation.

Done when:

- Other modules can pass page-sized buffers safely.

Doc note:

- Capture the distinction between a disk page and a buffer frame.

### D03-T03 - Implement DiskManager open/create

Read first:

- `../DanDB-legacy/src/storage/DiskManager.cpp`
- `include/dandb/platform/FileHandle.h`
- `include/dandb/storage/DatabaseHeader.h`

Goal:

Implement raw main database file access with header validation.

Build:

- Add `include/dandb/storage/DiskManager.h`.
- Add `src/storage/DiskManager.cpp`.
- Constructor should not do complex work. Prefer static open/create functions returning `Result<DiskManager>`.
- Implement:
  - `create(path, initial_header)`
  - `open_existing(path)`
  - `read_header()`
  - `write_header()`
  - `read_page(page_id)`
  - `write_page(page_id, bytes)`
  - `sync()`
  - `file_size()`
- `DiskManager` uses `FileHandle`, not `std::fstream`.

Requirements:

- Creating a DB writes page 0.
- Opening validates page 0.
- Reading page 0 through `read_page` is either forbidden or carefully defined. Prefer explicit header methods.
- DiskManager does not know about WAL transactions.
- DiskManager does not allocate pages. Pager owns allocation policy.

Tests to request:

- Create new database and reopen it.
- Open fails for empty file.
- Open fails for bad header.
- Write and read a non-header page.

Common mistakes:

- Letting DiskManager decide transaction durability.
- Letting DiskManager update page count without Pager.
- Zero-filling missing pages silently.

Done when:

- The main database file can be created, opened, validated, read, and written at page granularity.

Doc note:

- Capture why DiskManager is deliberately lower-level than Pager.

### D03-T04 - Add database creation rules

Read first:

- `DatabaseHeader`
- `DiskManager`

Goal:

Define exactly what a new empty DanDB file contains.

Build:

- Add a helper that creates an initial header.
- Initialize page count so page 0 exists.
- Reserve catalog root page IDs as invalid until catalog bootstrap creates them.
- Generate a database ID or salt.
- Ensure all unused bytes in page 0 are zero.

Requirements:

- New database files are deterministic except for database ID or salt.
- Empty files should not be mistaken for valid databases.
- The created file should have at least one full page.

Tests to request:

- New DB file size is 4096 bytes after create.
- Header page count is 1.
- Catalog root IDs are invalid before bootstrap.

Common mistakes:

- Creating a zero-byte file and expecting later code to fix it.
- Using page ID 0 for normal data.

Done when:

- A newly created file has a valid page 0 and no table pages yet.

Doc note:

- Capture what exists in an empty database before catalog bootstrap.

### D03-T05 - Add header update discipline

Read first:

- `DatabaseHeader`
- `DiskManager`

Goal:

Make mutable header fields updateable without making them unsafe.

Build:

- Add methods to update:
  - page count
  - catalog/system table root page IDs
- Recompute header checksum after updates.
- Keep header writes explicit. Do not write page 0 accidentally from unrelated code.

Requirements:

- Header updates must be part of Pager-managed transactions later.
- For now, tests may write header directly through DiskManager.
- Header validation after update must pass.

Tests to request:

- Change page count, write, reopen, validate.
- Change catalog root, write, reopen, validate.

Common mistakes:

- Letting multiple modules own header state.
- Updating in-memory header but forgetting to write page 0.

Done when:

- Header mutation is centralized and tested.

Doc note:

- Capture why page count belongs in the header but allocation belongs in Pager.

### D03-T06 - Add raw file corruption tests

Read first:

- Existing DiskManager tests.

Goal:

Start treating corruption as a first-class behavior.

Build:

- Add tests that manually corrupt:
  - magic
  - version
  - page size
  - checksum
  - file size not multiple of page size
- Ensure each returns `ErrorCode::Corruption` or a clearly chosen status.

Requirements:

- Corruption errors should be distinct from ordinary not-found errors.
- Messages should help identify what failed.
- Do not attempt auto-repair yet.

Tests to request:

- All corruption cases above.

Common mistakes:

- Panicking or crashing on corrupt files.
- Returning generic `IoError` for valid reads of invalid data.

Done when:

- Opening corrupted database files fails predictably.

Doc note:

- Capture the difference between I/O failure and format corruption.

### D03-T07 - Day 03 review checkpoint

Read first:

- Storage files created on Day 03.

Goal:

Verify the main file layer is stable before adding WAL.

Build:

- Run all tests.
- Check that `DiskManager` has no WAL knowledge.
- Check that header encoding uses endian helpers.
- Check that no direct C++ struct serialization is used.

Requirements:

- Main database files can be opened and rejected correctly.
- Page 0 rules are clear.

Tests to request:

- Full test suite.

Common mistakes:

- Hiding TODOs around corruption handling.

Done when:

- Day 04 can add WAL without changing DiskManager responsibilities.

Doc note:

- Capture the current main-file format in a small table.

## Day 04 - WAL File Format And WAL Manager

### D04-T01 - Define the WAL header format

Read first:

- `DatabaseHeader`
- `Checksum`
- SQLite WAL overview if you want conceptual background.

Goal:

Design a simple WAL file that can be scanned after a crash.

Build:

- Add `include/dandb/wal/WalHeader.h`.
- Add `src/wal/WalHeader.cpp`.
- Define fields:
  - magic bytes, for example `DWAL`
  - WAL format version
  - page size
  - database ID or salt copied from main DB
  - checksum seed if desired
  - reserved zero bytes
  - header checksum
- WAL header occupies a small fixed region, not a full database page unless that makes implementation simpler.

Requirements:

- WAL must belong to exactly one main database file.
- Wrong database ID is corruption or mismatch.
- Wrong page size is corruption.
- Header checksum must be validated.

Tests to request:

- Encode/decode valid WAL header.
- Reject wrong magic.
- Reject wrong page size.
- Reject wrong database ID.
- Reject bad checksum.

Common mistakes:

- Letting WAL exist without linking it to the main DB.
- Making WAL header depend on Pager state too much.

Done when:

- WAL header format is clear and tested.

Doc note:

- Capture why the WAL file must identify its database.

### D04-T02 - Define WAL frame and commit record formats

Read first:

- `WalHeader`
- `Page`

Goal:

Represent changed pages and transaction boundaries in the WAL.

Build:

- Add `include/dandb/wal/WalRecord.h`.
- Define two record kinds:
  - page frame
  - commit record
- Page frame contains:
  - record type
  - transaction ID
  - page ID
  - page image size
  - full 4096-byte page image
  - checksum
- Commit record contains:
  - record type
  - transaction ID
  - frame count or last frame number
  - checksum
- Keep all records self-validating.

Requirements:

- A transaction is durable only if its commit record is present and valid.
- Full-page images are simpler than physical diffs and are required in v1.
- Record checksums must cover enough bytes to detect torn or corrupt records.

Tests to request:

- Encode/decode page frame.
- Encode/decode commit record.
- Reject frame with wrong checksum.
- Reject unknown record type.

Common mistakes:

- Considering page frames committed before the commit record.
- Trying to optimize WAL size too early.

Done when:

- WAL records can be serialized and validated independently.

Doc note:

- Capture why full-page WAL is easier than logging row-level operations.

### D04-T03 - Implement WalManager open/create

Read first:

- `FileHandle`
- `DatabasePath`
- `WalHeader`
- `WalRecord`

Goal:

Create and open the WAL sidecar file.

Build:

- Add `include/dandb/wal/WalManager.h`.
- Add `src/wal/WalManager.cpp`.
- Implement:
  - `open_or_create(wal_path, expected_db_id, page_size)`
  - `append_page_frame(txn_id, page_id, page_bytes)`
  - `append_commit(txn_id, frame_count)`
  - `sync()`
  - `truncate_or_reset()`
  - `size()`
- Opening an empty WAL should create a header.
- Opening a non-empty WAL validates the header.

Requirements:

- WalManager does not know how to apply frames to the main DB yet.
- WalManager appends records only.
- WalManager never overwrites middle records.

Tests to request:

- Open creates WAL file.
- Reopen validates header.
- Append page frame and commit.
- WAL file grows.

Common mistakes:

- Writing WAL frames through DiskManager.
- Truncating WAL during normal commit.

Done when:

- WAL can be created and appended to, but recovery is not implemented yet.

Doc note:

- Capture the append-only nature of WAL.

### D04-T04 - Implement WAL scanner

Read first:

- `WalRecord`
- `WalManager`

Goal:

Read the WAL from start to end and identify committed transactions.

Build:

- Add `include/dandb/wal/WalScanner.h`.
- Add `src/wal/WalScanner.cpp`.
- Scanner output should include:
  - ordered valid records
  - committed transaction IDs
  - latest committed frame for each page ID
  - offset where valid WAL ended
  - whether trailing bytes were ignored
- Treat incomplete trailing record as ignored tail only if it follows the last valid committed transaction.

Requirements:

- Uncommitted frames after the last commit are ignored.
- Invalid bytes inside a committed transaction are corruption.
- Unknown records before the end of committed WAL are corruption.

Tests to request:

- Empty WAL with header scans as no committed frames.
- One committed transaction scans correctly.
- Uncommitted frames are ignored.
- Incomplete trailing record after commit is ignored.
- Corrupt committed frame fails.

Common mistakes:

- Applying the last frame even without commit.
- Treating all checksum failures as harmless tail damage.

Done when:

- Recovery code can ask scanner for committed page images.

Doc note:

- Capture the rule that commit record defines durability.

### D04-T05 - Add WAL sync discipline

Read first:

- `WalManager`
- `FileHandle::sync`

Goal:

Ensure commits use WAL sync in the correct order.

Build:

- Add a method such as `commit_transaction(txn_id, frames)` if it keeps ordering clear.
- Commit order:
  - append page frames
  - append commit record
  - sync WAL file
- Return failure if sync fails.
- Do not write changed pages to the main DB during commit.

Requirements:

- A committed transaction means its WAL commit record reached durable storage.
- Main file checkpointing is separate.
- Errors during commit leave the transaction outcome unknown to the caller, but recovery rules still decide what is durable.

Tests to request:

- Commit calls sync.
- Injected sync failure returns error.
- WAL with frames but failed sync can still be scanned according to bytes actually present in the test.

Common mistakes:

- Syncing before writing the commit record.
- Writing main DB pages during commit.
- Ignoring sync failure.

Done when:

- WAL commit ordering is explicit and tested with fault injection.

Doc note:

- Capture the write-ahead rule in one paragraph.

### D04-T06 - Add WAL reset behavior

Read first:

- `WalManager`
- `WalScanner`

Goal:

Prepare for manual checkpoint by supporting safe WAL reset after pages are copied to the main DB.

Build:

- Implement `reset()`:
  - truncate WAL file
  - write WAL header
  - sync if required by chosen durability discipline
- Do not call reset from commit.
- Do not reset if checkpoint fails.

Requirements:

- Reset produces a valid empty WAL.
- Existing WalManager can continue appending after reset.
- Header database ID remains the same.

Tests to request:

- Append records, reset, scan as empty.
- Append again after reset.

Common mistakes:

- Deleting the WAL file and forgetting to recreate header.
- Resetting before main database pages are durable.

Done when:

- WAL reset is available for the future checkpoint task.

Doc note:

- Capture why checkpoint and WAL reset are separate from commit.

### D04-T07 - Day 04 review checkpoint

Read first:

- All WAL files.

Goal:

Confirm the WAL layer can write, validate, scan, and reset records without knowing database semantics.

Build:

- Run all tests.
- Review every place that writes to WAL.
- Confirm checksums are tested.
- Confirm tail-handling rules are documented in tests.

Requirements:

- WAL manager does not apply frames to the main DB.
- WAL scanner does not know about SQL or catalog.

Tests to request:

- Full test suite.

Common mistakes:

- Letting WAL layer become a transaction manager.

Done when:

- Day 05 can build Pager on top of DiskManager and WalManager.

Doc note:

- Capture a small example WAL sequence with two page frames and one commit.

## Day 05 - Buffer Pool And Pager Baseline

### D05-T01 - Implement buffer frame state

Read first:

- `../DanDB-legacy/include/dandb/buffer/Page.h`
- `../DanDB-legacy/include/dandb/buffer/BufferPoolManager.h`

Goal:

Represent cached pages with metadata needed by Pager.

Build:

- Add `include/dandb/buffer/BufferFrame.h`.
- A frame should contain:
  - `Page`
  - page ID
  - pin count
  - dirty flag
  - last modifying transaction ID if useful
  - eviction metadata
- Keep frame data simple and visible.

Requirements:

- Dirty means page has changes not checkpointed to the main DB.
- Pin count prevents eviction while a caller uses the page.
- Frame does not decide how to write itself.

Tests to request:

- Pin and unpin state changes.
- Dirty state changes.

Common mistakes:

- Making frames call DiskManager.
- Confusing dirty-in-buffer with committed-in-WAL.

Done when:

- A buffer frame can safely track cached page state.

Doc note:

- Capture what dirty means in a WAL database.

### D05-T02 - Implement LRU replacement

Read first:

- `../DanDB-legacy/src/buffer/LRUReplacer.cpp`
- `../DanDB-legacy/include/dandb/buffer/LRUReplacer.h`

Goal:

Choose victim frames when the buffer pool is full.

Build:

- Add `include/dandb/buffer/LRUReplacer.h`.
- Add `src/buffer/LRUReplacer.cpp`.
- Implement:
  - mark frame evictable
  - mark frame non-evictable
  - record access
  - choose victim
- Keep the algorithm simple.

Requirements:

- Pinned frames are not evictable.
- Recently used frames should be evicted later.
- The replacer must not know about pages or files.

Tests to request:

- Victim selection respects recency.
- Non-evictable frames are skipped.
- Empty replacer reports no victim.

Common mistakes:

- Evicting pinned frames.
- Over-optimizing replacement policy.

Done when:

- Buffer pool can ask for a safe victim frame.

Doc note:

- Capture why buffer replacement is independent from storage durability.

### D05-T03 - Implement BufferPoolManager memory cache

Read first:

- `../DanDB-legacy/src/buffer/BufferPoolManager.cpp`
- New `BufferFrame`
- New `LRUReplacer`

Goal:

Cache pages in memory without owning disk or WAL policy.

Build:

- Add `include/dandb/buffer/BufferPoolManager.h`.
- Add `src/buffer/BufferPoolManager.cpp`.
- Implement:
  - `find(page_id)`
  - `insert_clean(page)`
  - `new_empty_frame(page_id)`
  - `pin(page_id)`
  - `unpin(page_id, dirty)`
  - `evict_victim()`
- Buffer pool should not read from DiskManager by itself.
- Buffer pool should not write to DiskManager by itself.

Requirements:

- Pager decides when to load and flush pages.
- Buffer pool owns memory frames only.
- Eviction of dirty pages is allowed only if Pager has made the page recoverable through WAL, or if Pager writes it as part of checkpoint. In early implementation, it is acceptable to reject eviction of dirty pages until Pager handles it.

Tests to request:

- Cache hit returns existing frame.
- Cache miss can insert page.
- Pinned frame cannot be evicted.
- Dirty frame is tracked correctly.

Common mistakes:

- Copying the legacy design where buffer manager talks directly to DiskManager.
- Losing dirty state on eviction.

Done when:

- BufferPoolManager is a pure memory cache.

Doc note:

- Capture why Pager, not BufferPoolManager, owns durability decisions.

### D05-T04 - Design Pager ownership

Read first:

- `DiskManager`
- `WalManager`
- `BufferPoolManager`

Goal:

Create the central class that coordinates pages, WAL, recovery, and transactions.

Build:

- Add `include/dandb/storage/Pager.h`.
- Add `src/storage/Pager.cpp`.
- Pager should own or coordinate:
  - `DiskManager`
  - `WalManager`
  - `BufferPoolManager`
  - current database header state
  - transaction state
- Define methods:
  - `open(path, options)`
  - `create(path, options)`
  - `get_page(page_id)`
  - `new_page()`
  - `mark_dirty(page_id)`
  - `begin()`
  - `commit()`
  - `rollback()`
  - `checkpoint()`
  - `close()`

Requirements:

- Pager is the only layer that understands both main DB and WAL.
- B+ tree, catalog, and SQL use Pager, not DiskManager or WalManager.
- Pager owns page allocation.

Tests to request:

- Pager can create a DB and close it.
- Pager can open existing DB.

Common mistakes:

- Letting Catalog call WalManager.
- Letting B+ tree allocate disk pages directly.
- Making Pager parse SQL.

Done when:

- Pager class exists with clear responsibilities, even if many methods are stubs.

Doc note:

- Capture a diagram showing Pager in the middle of the storage stack.

### D05-T05 - Implement Pager recovery on open

Read first:

- `WalScanner`
- `DiskManager`
- `DatabaseHeader`

Goal:

When opening a database, make committed WAL frames visible.

Build:

- On `Pager::open`, scan the WAL.
- Build an in-memory map:
  - page ID -> latest committed page image from WAL
- When `get_page(page_id)` is called, return the WAL image if it exists; otherwise read from the main DB.
- Do not checkpoint automatically.

Requirements:

- Committed changes survive close/open even before checkpoint.
- Uncommitted WAL frames are invisible.
- Main DB file may be older than the latest committed state.

Tests to request:

- Manually create WAL with committed page frame, open Pager, read updated page.
- Uncommitted frame is ignored.
- Corrupt committed frame fails open.

Common mistakes:

- Applying WAL frames to the main DB during open without an explicit checkpoint.
- Forgetting that reads must see committed WAL pages.

Done when:

- Pager reads the logical latest page image after recovery.

Doc note:

- Capture the difference between recovery visibility and checkpointing.

### D05-T06 - Implement Pager page allocation

Read first:

- `DatabaseHeader`
- `DiskManager`
- `Pager`

Goal:

Allocate new page IDs using append-only policy.

Build:

- Implement `Pager::new_page()`.
- Use header page count as the next page ID.
- Increment page count in the in-memory header.
- Return a zero-filled page pinned in the buffer pool.
- Mark the header page and new page dirty in the current transaction.

Requirements:

- First allocated page is page 1.
- No free-list.
- No page reuse.
- Page count changes must be transactional through WAL.

Tests to request:

- New DB allocates page 1 then page 2.
- Reopen after commit preserves page count.
- Allocation inside rolled-back transaction does not become visible.

Common mistakes:

- Writing page count directly to main header outside transaction.
- Reusing deleted pages.
- Forgetting page 0 update when page count changes.

Done when:

- Pager can allocate pages in a way WAL can protect.

Doc note:

- Capture why append-only allocation is easier for v1.

### D05-T07 - Implement transaction state skeleton

Read first:

- `Pager`
- `WalManager`

Goal:

Represent manual and autocommit transactions.

Build:

- Add `include/dandb/transaction/TransactionState.h`.
- Add `src/transaction/TransactionState.cpp` if needed.
- Track:
  - no active transaction
  - active transaction
  - failed active transaction
  - transaction ID
  - dirty page IDs
  - original page images needed for rollback
- Implement Pager methods:
  - `begin()`
  - `commit()` stub or partial
  - `rollback()` stub or partial
  - `in_transaction()`
  - `mark_transaction_failed()`

Requirements:

- Calling `BEGIN` twice is an error.
- Calling `COMMIT` without transaction is an error.
- Calling `ROLLBACK` without transaction is an error.
- Failed transaction accepts only rollback.

Tests to request:

- Begin transitions to active.
- Double begin fails.
- Failed transaction rejects commit.
- Rollback clears failed state.

Common mistakes:

- Treating failed transaction as still usable.
- Hiding transaction state inside SQL layer.

Done when:

- Pager has explicit transaction state.

Doc note:

- Capture why a failed transaction is simpler to make rollback-only.

### D05-T08 - Implement commit of dirty pages to WAL

Read first:

- `Pager`
- `TransactionState`
- `WalManager`

Goal:

Make committed page changes durable in WAL.

Build:

- On commit:
  - collect dirty pages from transaction state
  - append full-page frames to WAL
  - append commit record
  - sync WAL
  - update recovery map so reads see committed images
  - clear transaction state
- For autocommit, wrap a single write operation in an implicit transaction.

Requirements:

- Commit does not write dirty pages to main DB.
- Dirty pages remain in buffer and are recoverable from WAL.
- Commit failure marks transaction outcome according to your chosen error rule. For v1, return error and require reopen or rollback if state is safe.

Tests to request:

- Write page, commit, close, reopen, read committed data.
- Write page without commit, close, reopen, data absent.
- Inject sync failure, commit returns error.

Common mistakes:

- Clearing dirty state before WAL sync succeeds.
- Forgetting to update recovered-page map after commit.
- Writing main DB pages on commit.

Done when:

- WAL-backed commit works for raw pages.

Doc note:

- Capture the exact commit order.

### D05-T09 - Implement rollback of dirty pages

Read first:

- `TransactionState`
- `BufferPoolManager`
- `Pager`

Goal:

Undo uncommitted page changes in memory.

Build:

- Before the first modification to a page in a transaction, store its original image.
- On rollback:
  - restore original images in the buffer pool
  - discard newly allocated pages from visibility if possible
  - restore in-memory header page count
  - clear dirty set
  - clear transaction state
- Rollback does not write to WAL.

Requirements:

- Rollback only affects uncommitted changes.
- Committed WAL frames remain visible.
- Page allocation rollback must not leave header page count advanced.

Tests to request:

- Modify existing page, rollback, original data visible.
- Allocate page, rollback, page count restored.
- Failed transaction rollback clears state.

Common mistakes:

- Trying to undo by writing inverse WAL records.
- Forgetting header rollback.
- Keeping a new page reachable after rollback.

Done when:

- Raw page changes can be rolled back before commit.

Doc note:

- Capture why rollback uses in-memory before-images in this simple design.

### D05-T10 - Implement manual checkpoint

Read first:

- `WalScanner`
- `DiskManager`
- `Pager`

Goal:

Copy committed WAL pages to the main database file only when requested.

Build:

- Implement `Pager::checkpoint()`.
- Checkpoint order:
  - reject if a transaction is active
  - scan current committed WAL state
  - write latest committed page image for each page ID to main DB
  - write updated header page if needed
  - sync main DB
  - reset WAL
  - clear recovered-page map
- Expose this later as SQL `CHECKPOINT;`.

Requirements:

- Manual only. Do not checkpoint automatically on open or commit.
- Checkpoint failure must not reset WAL.
- After checkpoint, reopening DB without WAL frames still sees committed data.

Tests to request:

- Commit page change, checkpoint, WAL resets, main DB has page.
- Inject main DB sync failure, WAL is not reset.
- Checkpoint while transaction active fails.

Common mistakes:

- Resetting WAL before main DB sync.
- Allowing checkpoint during active transaction.

Done when:

- Raw page WAL lifecycle is complete: commit, recover, checkpoint.

Doc note:

- Capture the checkpoint order and why the order matters.

### D05-T11 - Day 05 review checkpoint

Read first:

- Pager, WAL, storage, buffer tests.

Goal:

Confirm the core durability architecture works before record and table logic depend on it.

Build:

- Run all tests.
- Review ownership:
  - DiskManager owns main-file page I/O.
  - WalManager owns WAL append/scan/reset.
  - BufferPoolManager owns memory frames.
  - Pager coordinates all durable behavior.
- Add missing comments only where code is hard to follow.

Requirements:

- Raw page transactions are durable.
- Recovery works.
- Manual checkpoint works.

Tests to request:

- Full test suite.

Common mistakes:

- Moving to records while raw page transactions are unreliable.

Done when:

- Day 06 can build row encoding on top of a reliable Pager.

Doc note:

- Capture the first full storage-stack explanation.

## Day 06 - Types, Schema, Rows, And Keys

### D06-T01 - Define logical types

Read first:

- `../DanDB-legacy/include/dandb/record/LogicalType.h`
- `../DanDB-legacy/include/dandb/record/Value.h`

Goal:

Represent the supported SQL types in C++.

Build:

- Add `include/dandb/record/LogicalType.h`.
- Define type kind enum:
  - `Int8`
  - `Int16`
  - `Int32`
  - `Int64`
  - `Double`
  - `String`
  - `Bool`
- Store string length for `STRING(N)`.
- Add helpers:
  - parse type from SQL spelling later
  - fixed storage size
  - whether type can be indexed
  - display name

Requirements:

- `STRING(N)` must validate `N > 0`.
- `DOUBLE` is allowed for rows but not allowed as an index key in v1.
- `BOOL` fixed size is one byte.

Tests to request:

- Size of each fixed type.
- `STRING(64)` size is 64.
- Invalid string length is rejected.
- Indexability rules.

Common mistakes:

- Treating type spelling as the same as internal type enum.
- Forgetting that `STRING(N)` has a parameter.

Done when:

- The record layer has a complete type model.

Doc note:

- Capture why fixed-size rows make the first version easier.

### D06-T02 - Define Value and literal conversion

Read first:

- `../DanDB-legacy/src/record/Value.cpp`
- `../DanDB-legacy/include/dandb/record/Value.h`

Goal:

Represent runtime SQL values before they are encoded into rows.

Build:

- Add `include/dandb/record/Value.h`.
- Add `src/record/Value.cpp`.
- Store variants for:
  - null
  - int64 logical integer input
  - double
  - string
  - bool
- Add conversion method:
  - `Result<EncodedField> convert_to(LogicalType)`
  - or equivalent validation helper.

Requirements:

- Overflow into smaller integer types is an error.
- String too long is an error.
- Bool accepts only bool literals, not arbitrary integers, unless parser later deliberately supports that.
- Null is accepted only for nullable columns.

Tests to request:

- INT8 overflow fails.
- INT16 overflow fails.
- String overflow fails.
- Bool conversion works.
- Null conversion respects nullable flag.

Common mistakes:

- Silently truncating strings.
- Silently wrapping integer overflow.
- Treating null as an empty string or zero.

Done when:

- Literal values can be validated against column types.

Doc note:

- Capture why strict typing is easier to debug than silent coercion.

### D06-T03 - Define Column and Schema

Read first:

- `../DanDB-legacy/src/record/Schema.cpp`
- `../DanDB-legacy/include/dandb/record/Schema.h`

Goal:

Represent table schemas, constraints, and fixed row layout.

Build:

- Add `include/dandb/record/Column.h`.
- Add `include/dandb/record/Schema.h`.
- Add `src/record/Schema.cpp`.
- Column fields:
  - name
  - logical type
  - nullable
  - primary key flag
  - unique flag
  - ordinal
  - fixed offset
- Schema fields:
  - table name if useful
  - ordered columns
  - primary key column ordinal
  - row size
  - null bitmap size

Requirements:

- Exactly one primary key column.
- Primary key column cannot be nullable.
- Unique columns in v1 should be not-null unless full null uniqueness semantics are implemented later.
- Duplicate column names are rejected.
- Row size is fixed and deterministic.

Tests to request:

- Valid schema computes offsets.
- Missing primary key rejected.
- Two primary keys rejected.
- Duplicate column rejected.
- Nullable primary key rejected.

Common mistakes:

- Letting schema validation happen only in SQL parser.
- Computing offsets repeatedly instead of storing them.

Done when:

- A schema fully describes how a fixed row is encoded.

Doc note:

- Capture what metadata belongs in Schema versus Catalog.

### D06-T04 - Implement fixed row encoding

Read first:

- `../DanDB-legacy/src/record/Layout.cpp`
- `../DanDB-legacy/src/record/Codec.cpp`
- `../DanDB-legacy/include/dandb/record/Row.h`

Goal:

Encode and decode complete fixed-size rows.

Build:

- Add `include/dandb/record/Row.h`.
- Add `include/dandb/record/RowCodec.h`.
- Add `src/record/Row.cpp`.
- Add `src/record/RowCodec.cpp`.
- Row layout:
  - null bitmap first
  - fixed-width field bytes after bitmap
  - strings stored as fixed N-byte regions with actual length if needed
- Decide string encoding:
  - simplest: fixed N bytes padded with zero plus length stored elsewhere if needed
  - better: 2-byte or 4-byte length prefix inside the fixed string region, followed by bytes

Requirements:

- Row encoded size equals schema row size.
- Null fields have null bitmap bit set.
- Non-null fields have type bytes encoded little-endian.
- Decoding must reconstruct values exactly.
- String overflow is rejected before encoding.

Tests to request:

- Encode/decode all supported types.
- Encode/decode null.
- String length boundaries.
- Row size remains fixed.

Common mistakes:

- Letting decoded strings include padding bytes.
- Forgetting null bitmap when comparing rows.
- Using C string termination as storage format.

Done when:

- Complete rows can be round-tripped through bytes.

Doc note:

- Capture the fixed row layout with a small byte diagram.

### D06-T05 - Implement key encoding

Read first:

- `LogicalType`
- `RowCodec`

Goal:

Encode index keys so B+ tree comparisons are correct.

Build:

- Add `include/dandb/record/KeyCodec.h`.
- Add `src/record/KeyCodec.cpp`.
- Support keys for:
  - INT8
  - INT16
  - INT32
  - INT64
  - STRING(N)
  - BOOL
- Reject `DOUBLE` keys in v1.
- Encode keys into fixed-size byte arrays or comparable key objects.

Requirements:

- Numeric ordering must match logical ordering.
- String ordering should be bytewise lexicographic for v1.
- Null keys are not allowed for primary or unique indexed keys in v1.
- Secondary non-unique indexes also should reject null in v1 unless a later task explicitly supports null index entries.

Tests to request:

- INT ordering compares correctly.
- Negative integers compare correctly.
- Strings compare lexicographically.
- Bool false sorts before true.
- DOUBLE key creation rejected.

Common mistakes:

- Comparing little-endian signed integer bytes directly.
- Ignoring negative integer ordering.
- Letting null keys enter B+ tree code.

Done when:

- B+ tree can compare keys without knowing SQL types deeply.

Doc note:

- Capture why key encoding is separate from row encoding.

### D06-T06 - Implement row projection helpers

Read first:

- `Row`
- `Schema`

Goal:

Make SELECT output and index maintenance easier.

Build:

- Add helpers:
  - get value by column ordinal
  - get value by column name
  - build row from ordered values
  - replace non-primary-key fields for UPDATE
  - extract primary key
  - extract indexed column key

Requirements:

- Primary key update attempts must be detectable.
- Missing or extra INSERT values are errors.
- Column lookup should return clear errors.

Tests to request:

- Extract primary key.
- Extract named column.
- Build row with correct values.
- Reject wrong value count.

Common mistakes:

- Making SQL executor manually compute offsets everywhere.
- Allowing update to change primary key by accident.

Done when:

- Higher layers can work with rows through clear helpers.

Doc note:

- Capture why row helpers reduce executor complexity.

### D06-T07 - Add record validation tests

Read first:

- All record module tests.

Goal:

Make the record layer trustworthy before B+ tree uses it.

Build:

- Add focused tests for:
  - schema validation
  - type conversion
  - row encoding
  - row decoding
  - key encoding and comparison

Requirements:

- Tests should not depend on Pager or B+ tree.
- Tests should cover edge values for each integer type.
- Tests should cover string boundary length exactly N and N + 1.

Tests to request:

- All listed record tests.

Common mistakes:

- Only testing happy paths.
- Testing row encoding through SQL before record layer is stable.

Done when:

- Record tests give confidence that B+ tree entries will be valid.

Doc note:

- Capture the supported type table for future README/blog use.

### D06-T08 - Day 06 review checkpoint

Read first:

- Record layer files.

Goal:

Confirm rows and keys are simple, fixed-size, and ready for page storage.

Build:

- Run all tests.
- Review row layout for readability.
- Review key comparator for signed integer correctness.
- Add explanatory comments only around tricky encoding rules.

Requirements:

- No SQL parser code depends on unfinished record behavior.
- No B+ tree code exists yet.

Tests to request:

- Full test suite.

Common mistakes:

- Moving to B+ tree before key comparison is correct.

Done when:

- Day 07 can implement B+ tree pages.

Doc note:

- Capture one example row and its fixed-size encoded layout.

## Day 07 - B+ Tree Page Format And Search

### D07-T01 - Define B+ tree page header

Read first:

- `Page`
- `Endian`
- `KeyCodec`

Goal:

Define how B+ tree nodes are stored inside 4096-byte pages.

Build:

- Add `include/dandb/btree/BTreePage.h`.
- Add `src/btree/BTreePage.cpp`.
- Common page header fields:
  - page kind: internal or leaf
  - root flag if useful
  - key count
  - parent page ID
  - next leaf page ID for leaves
  - previous leaf page ID if useful
  - reserved bytes
- Leaf entries:
  - key bytes
  - row bytes for table B+ tree, or primary key bytes for secondary index tree
- Internal entries:
  - separator key bytes
  - child page IDs

Requirements:

- Do not use C++ structs as on-disk layout.
- Page capacity must be computed from key size and value size.
- Page validation should catch impossible key counts.

Tests to request:

- Initialize leaf page.
- Initialize internal page.
- Validate empty page headers.
- Compute capacity for a sample schema.

Common mistakes:

- Hardcoding one key size globally.
- Forgetting leaf sibling pointer.
- Mixing table rows and secondary index values without a clear entry layout.

Done when:

- B+ tree pages have a clear binary layout.

Doc note:

- Capture how leaf and internal pages differ.

### D07-T02 - Implement B+ tree page entry operations

Read first:

- `BTreePage`
- `KeyCodec`

Goal:

Manipulate entries inside one B+ tree page.

Build:

- Implement methods:
  - get key at slot
  - get value at slot
  - find insertion position
  - insert entry into non-full page
  - erase entry from page
  - split entries into another page helper if useful
- Keep operations page-local. They should not allocate pages.

Requirements:

- Entries remain sorted by key.
- Duplicate handling is controlled by the caller.
- Bounds checks return errors or assertions consistently.

Tests to request:

- Insert keys in sorted order.
- Insert keys in reverse order.
- Find existing key.
- Find missing key position.
- Erase key.

Common mistakes:

- Letting page-local code call Pager.
- Off-by-one errors in internal node child pointers.

Done when:

- A single leaf or internal page can maintain sorted entries.

Doc note:

- Capture why page-local operations are separate from tree-level operations.

### D07-T03 - Implement BTree class skeleton

Read first:

- `Pager`
- `BTreePage`
- `Schema`
- `KeyCodec`

Goal:

Create the tree-level object that owns root page ID and uses Pager for pages.

Build:

- Add `include/dandb/btree/BTree.h`.
- Add `src/btree/BTree.cpp`.
- Constructor inputs:
  - Pager reference
  - root page ID
  - key definition
  - value layout definition
  - uniqueness mode
- Define operations:
  - `create_empty_tree`
  - `find`
  - `insert`
  - `erase`
  - `update`
  - `scan`
  - `validate`

Requirements:

- BTree does not know SQL syntax.
- BTree does not know catalog table names.
- BTree uses Pager for every page access and allocation.

Tests to request:

- Create empty tree and find missing key.
- Root page initialized as leaf.

Common mistakes:

- Making BTree own database file access.
- Baking table row layout into generic BTree too deeply.

Done when:

- Tree-level operations have a clear class home.

Doc note:

- Capture what BTree knows and what it deliberately does not know.

### D07-T04 - Implement B+ tree search

Read first:

- `BTree`
- `BTreePage`

Goal:

Find a key by descending from root to leaf.

Build:

- Implement `BTree::find(key)`.
- Internal node search chooses the correct child.
- Leaf search returns found value or not found.
- Add debug path tracing if useful for tests.

Requirements:

- Works for empty root leaf.
- Works for one-level tree.
- Works for multi-level tree after later insert tests create one.
- Does not modify pages.

Tests to request:

- Find missing key in empty tree.
- Manually build a small root/leaf setup and find keys.
- Boundary keys choose correct child.

Common mistakes:

- Choosing wrong child for equal separator keys.
- Reading uninitialized child pointers.

Done when:

- Search logic is correct before insertion complexity arrives.

Doc note:

- Capture how separator keys guide search.

### D07-T05 - Implement insert into non-full leaf

Read first:

- `BTreePage`
- `BTree::find`

Goal:

Support simple inserts before handling splits.

Build:

- Implement `BTree::insert` for the case where the target leaf has space.
- Enforce uniqueness if BTree is configured unique.
- For non-unique secondary indexes, store composite logical key `(indexed_key, primary_key)` or equivalent so duplicates can coexist.
- Mark modified leaf dirty through Pager.

Requirements:

- Table primary B+ tree rejects duplicate primary keys.
- Unique index tree rejects duplicate indexed keys.
- Ordinary secondary index allows duplicate indexed values but distinguishes by primary key.

Tests to request:

- Insert one row and find it.
- Insert several rows in different order and find all.
- Duplicate key rejected in unique tree.
- Duplicate indexed value allowed in non-unique tree.

Common mistakes:

- Forgetting to mark page dirty.
- Checking uniqueness only after modifying the page.
- Using only indexed key for non-unique secondary index entries.

Done when:

- Single-page B+ tree inserts work.

Doc note:

- Capture why non-unique indexes need primary key as part of the entry identity.

### D07-T06 - Implement leaf split

Read first:

- `BTreePage`
- `Pager::new_page`

Goal:

Handle inserts when a leaf page is full.

Build:

- Split a full leaf into old and new leaf.
- Move roughly half entries to the new leaf.
- Maintain leaf sibling pointers.
- Return separator key to parent insert logic.
- If the root leaf splits, create a new internal root.

Requirements:

- All entries remain sorted.
- Leaf scan order remains correct.
- Split must be transactional through Pager dirty pages.

Tests to request:

- Insert enough rows to split root leaf.
- Find all rows after split.
- Leaf linked-list scan returns sorted rows.

Common mistakes:

- Losing one entry during split.
- Promoting the wrong separator key.
- Forgetting to update root page ID when root splits.

Done when:

- B+ tree can grow from one leaf to root plus leaves.

Doc note:

- Capture the first root split with a before/after diagram.

### D07-T07 - Implement internal insert and internal split

Read first:

- Leaf split implementation.
- Internal page layout.

Goal:

Allow the B+ tree to grow beyond two levels.

Build:

- Insert separator and child pointer into parent internal node.
- If parent is full, split internal node.
- Propagate separator upward.
- If root internal node splits, create a new root.
- Update parent page IDs if your format uses them.

Requirements:

- Tree height can increase.
- Search still reaches correct leaves.
- Parent pointers, if present, remain correct.
- Root page ID updates must be persisted through catalog/header owner later.

Tests to request:

- Insert enough rows for multiple leaf splits.
- Insert enough rows for internal split.
- Find all rows.
- Validate tree shape.

Common mistakes:

- Internal node child count is key count plus one.
- Losing the middle child during split.
- Forgetting to update parent IDs.

Done when:

- B+ tree insertion supports multi-level trees.

Doc note:

- Capture the internal-node child pointer rule.

### D07-T08 - Implement B+ tree full scan

Read first:

- Leaf sibling pointer code.

Goal:

Return all entries in key order.

Build:

- Add `BTreeCursor` or scan iterator.
- Descend to leftmost leaf.
- Move across leaf sibling links.
- Return entries in sorted order.

Requirements:

- Scan works on empty tree.
- Scan works on one leaf.
- Scan works after many splits.
- Cursor should be simple and not outlive unsafe page references.

Tests to request:

- Empty scan.
- Single-page scan.
- Multi-page sorted scan.

Common mistakes:

- Keeping pinned pages forever during scan.
- Broken sibling links after split.

Done when:

- SELECT without index can later scan table rows.

Doc note:

- Capture why B+ tree leaves are linked.

### D07-T09 - Day 07 review checkpoint

Read first:

- B+ tree page and insertion tests.

Goal:

Confirm B+ tree search and insertion are correct before deletion and table integration.

Build:

- Run all tests.
- Add a B+ tree validator stub if not already present.
- Review split tests carefully.

Requirements:

- Insert/find/scan work for table-like entries and index-like entries.
- No SQL or catalog code depends on B+ tree yet.

Tests to request:

- Full test suite.

Common mistakes:

- Moving to SQL before B+ tree structure is reliable.

Done when:

- Day 08 can add deletion, validation, and range scans.

Doc note:

- Capture current B+ tree invariants.

## Day 08 - B+ Tree Delete, Range Scan, And Validation

### D08-T01 - Implement B+ tree validator

Read first:

- `BTree`
- `BTreePage`

Goal:

Create a debug/test tool that checks tree invariants.

Build:

- Add `include/dandb/btree/BTreeValidator.h`.
- Add `src/btree/BTreeValidator.cpp`.
- Validate:
  - page kind is valid
  - key counts are within bounds
  - keys sorted inside pages
  - child key ranges match separators
  - all leaves at same depth
  - leaf sibling links are consistent
  - root special cases are allowed

Requirements:

- Validator is used heavily in tests.
- Validator returns detailed errors, not just false.
- Production code can compile it in because it is useful and simple.

Tests to request:

- Valid tree passes.
- Manually corrupted sorted order fails.
- Broken sibling link fails.

Common mistakes:

- Writing validator after bugs become hard to diagnose.
- Making validator mutate pages.

Done when:

- Tests can call `validate()` after complex operations.

Doc note:

- Capture why validators are powerful for storage engines.

### D08-T02 - Implement erase from leaf without rebalancing

Read first:

- `BTreePage` erase operation.

Goal:

Support simple deletion from a leaf page.

Build:

- Implement `BTree::erase(key)` for target leaf.
- Remove entry if found.
- Mark leaf dirty.
- Return not found if missing.
- Do not rebalance yet.

Requirements:

- Root leaf may become empty.
- Non-root underflow may exist temporarily until next task handles it.
- Validator may need a mode that allows underfull pages until rebalancing is implemented.

Tests to request:

- Insert then erase key.
- Erase missing key returns not found.
- Root leaf empty after deleting all rows.

Common mistakes:

- Treating delete missing as success when higher layers need to know.
- Forgetting secondary index delete later needs exact entry identity.

Done when:

- Basic erase works in leaves.

Doc note:

- Capture why deletion is more complex than insertion.

### D08-T03 - Implement leaf redistribution and merge

Read first:

- `BTree::erase`
- Leaf split logic.

Goal:

Keep leaf pages valid after deletion.

Build:

- After erase, detect underflow.
- Borrow from left or right sibling if possible.
- Otherwise merge with a sibling.
- Update parent separator keys.
- Maintain leaf sibling pointers.
- Mark all changed pages dirty.

Requirements:

- Root leaf is allowed to be empty.
- Non-root leaves should satisfy minimum occupancy after rebalance.
- Parent separator keys must still route searches correctly.

Tests to request:

- Delete causing borrow from sibling.
- Delete causing merge.
- Scan after deletes remains sorted.
- Find all remaining keys.

Common mistakes:

- Forgetting to update parent separator after borrowing.
- Breaking sibling links during merge.
- Deleting the wrong separator from parent.

Done when:

- Leaf deletion preserves tree correctness.

Doc note:

- Capture borrow versus merge in plain language.

### D08-T04 - Implement internal node rebalancing

Read first:

- Internal split code.
- Leaf rebalance code.

Goal:

Handle parent underflow after child merges.

Build:

- Rebalance internal nodes after a child separator is removed.
- Borrow from sibling internal node if possible.
- Merge internal nodes if needed.
- Shrink tree height when root has one child.

Requirements:

- Root can shrink.
- All leaves remain at same depth.
- Validator passes after large delete sequences.

Tests to request:

- Delete enough keys to shrink root.
- Delete across multiple levels.
- Random insert/delete sequences with validation after each operation.

Common mistakes:

- Mishandling child count during internal merge.
- Keeping an unnecessary empty root.
- Not updating parent pointers if used.

Done when:

- B+ tree delete is structurally complete.

Doc note:

- Capture why root has special rules.

### D08-T05 - Implement range scan

Read first:

- `BTreeCursor`
- Key comparator.

Goal:

Support simple indexed WHERE predicates.

Build:

- Add scan methods:
  - full scan
  - lower-bound scan
  - equality lookup
  - range scan with inclusive/exclusive boundaries
- Use leaf sibling links for forward traversal.

Requirements:

- `id = 5` should use exact lookup.
- `id >= 5` should start at lower bound.
- `id < 10` can start at leftmost and stop at upper bound.
- Range scan should not load unrelated pages once upper bound is passed.

Tests to request:

- Equality range.
- Inclusive lower bound.
- Exclusive upper bound.
- Empty range.

Common mistakes:

- Scanning from leftmost for every indexed predicate.
- Off-by-one with inclusive and exclusive bounds.

Done when:

- Planner/executor can later use B+ tree for simple WHERE.

Doc note:

- Capture how range scans make indexes useful.

### D08-T06 - Add B+ tree transaction tests through Pager

Read first:

- Pager transaction tests.
- B+ tree insert/delete tests.

Goal:

Verify B+ tree changes obey WAL transactions.

Build:

- Add integration tests that use real Pager and BTree.
- Test insert commit and reopen.
- Test insert rollback.
- Test delete commit and reopen.
- Test delete rollback.
- Test checkpoint after B+ tree changes.

Requirements:

- No direct DiskManager access in B+ tree tests except test setup utilities.
- Tests should validate tree after reopen.

Tests to request:

- All listed integration tests.

Common mistakes:

- Only testing B+ tree in memory.
- Forgetting that root page ID changes must be durable.

Done when:

- B+ tree operations survive commit, recovery, rollback, and checkpoint.

Doc note:

- Capture why page-level WAL can protect B+ tree operations.

### D08-T07 - Day 08 review checkpoint

Read first:

- All B+ tree files and tests.

Goal:

Confirm B+ tree is strong enough to become table storage.

Build:

- Run all tests.
- Review validator coverage.
- Review delete edge cases.
- Review range scan API.

Requirements:

- Insert, find, scan, range scan, delete, and validate work.
- B+ tree uses Pager only for page access.

Tests to request:

- Full test suite.

Common mistakes:

- Proceeding to catalog while B+ tree delete is flaky.

Done when:

- Day 09 can create system tables and user tables on B+ trees.

Doc note:

- Capture a compact B+ tree architecture summary.

## Day 09 - Catalog And System Tables

### D09-T01 - Define catalog descriptors

Read first:

- `Schema`
- `BTree`
- Legacy `TableManager` only for comparison:
  - `../DanDB-legacy/src/catalog/TableManager.cpp`
  - `../DanDB-legacy/include/dandb/catalog/TableManager.h`

Goal:

Represent tables, columns, and indexes in memory without using `tables.meta`.

Build:

- Add `include/dandb/catalog/TableDescriptor.h`.
- Add `include/dandb/catalog/ColumnDescriptor.h`.
- Add `include/dandb/catalog/IndexDescriptor.h`.
- Fields for table:
  - table ID
  - name
  - root page ID
  - primary key column ID
- Fields for column:
  - column ID
  - table ID
  - name
  - type
  - ordinal
  - nullable
  - primary key flag
  - unique flag
- Fields for index:
  - index ID
  - table ID
  - name
  - root page ID
  - unique flag
  - primary flag
  - internal flag
  - indexed column ID

Requirements:

- Descriptors are plain metadata.
- They do not parse SQL.
- They do not read files by themselves.

Tests to request:

- Descriptor construction and validation helpers.

Common mistakes:

- Recreating `tables.meta` as a side file.
- Mixing catalog storage with SQL parsing.

Done when:

- Catalog metadata has clear C++ representations.

Doc note:

- Capture why catalog metadata is stored in tables.

### D09-T02 - Define system table schemas

Read first:

- `Schema`
- `TableDescriptor`
- `IndexDescriptor`

Goal:

Define DanDB's internal catalog as normal fixed-row tables.

Build:

- Add `include/dandb/catalog/SystemTables.h`.
- Define schemas:
  - `dandb_tables`
  - `dandb_columns`
  - `dandb_indexes`
  - `dandb_index_columns`
- `dandb_index_columns` maps indexes to indexed columns:
  - `index_id`
  - `column_id`
  - `ordinal`
- In v1 every index has exactly one row in `dandb_index_columns` with `ordinal = 0`.

Requirements:

- System tables use the same B+ tree table storage as user tables.
- Users can SELECT from system tables later.
- Users cannot INSERT, UPDATE, or DELETE system tables directly.
- System table schemas must not depend on parser code.

Tests to request:

- System table schemas validate.
- Expected columns exist.
- Primary keys exist.

Common mistakes:

- Special-casing system tables as JSON or text metadata.
- Skipping `dandb_index_columns` because v1 has single-column indexes.

Done when:

- System catalog schemas are defined and testable.

Doc note:

- Capture what each system table stores.

### D09-T03 - Bootstrap system tables in a new database

Read first:

- `Pager::new_page`
- `BTree::create_empty_tree`
- `SystemTables`

Goal:

Create catalog B+ trees when a new database is created.

Build:

- Add `include/dandb/catalog/CatalogBootstrap.h`.
- Add `src/catalog/CatalogBootstrap.cpp`.
- During database create:
  - create B+ tree root for each system table
  - update header root page IDs
  - insert system table rows into `dandb_tables`
  - insert system column rows into `dandb_columns`
  - insert system index rows into `dandb_indexes`
  - insert system index-column rows into `dandb_index_columns`
  - commit through Pager

Requirements:

- Bootstrap is transactional.
- Reopening a created DB can load catalog from system tables.
- There is no `tables.meta`.

Tests to request:

- Create DB and verify system table root IDs are valid.
- Reopen DB and scan `dandb_tables`.
- System tables describe themselves.

Common mistakes:

- Updating header outside the bootstrap transaction.
- Forgetting system tables need catalog entries too.

Done when:

- A new database has a self-describing system catalog.

Doc note:

- Capture the recursive idea that catalog tables describe catalog tables.

### D09-T04 - Implement Catalog load

Read first:

- `CatalogBootstrap`
- `BTree`
- `SystemTables`

Goal:

Load in-memory catalog descriptors from system tables.

Build:

- Add `include/dandb/catalog/Catalog.h`.
- Add `src/catalog/Catalog.cpp`.
- Implement:
  - `load(Pager&)`
  - `find_table(name)`
  - `find_table(table_id)`
  - `find_column(table_id, name)`
  - `indexes_for_table(table_id)`
  - `primary_index_for_table(table_id)`
- Catalog should scan system table B+ trees and build in-memory maps.

Requirements:

- Duplicate catalog rows are corruption.
- Missing required system rows are corruption.
- Unknown system table root IDs are corruption.
- Catalog names should follow identifier normalization rules.

Tests to request:

- Load catalog from DB.
- Find system tables by name.
- Corrupt duplicate table name fails load.

Common mistakes:

- Treating catalog corruption as normal not-found.
- Letting catalog silently repair missing rows.

Done when:

- Database open can load catalog metadata from B+ trees.

Doc note:

- Capture why catalog load validates metadata.

### D09-T05 - Implement create table through Catalog

Read first:

- `Catalog`
- `Schema`
- `BTree`
- `Pager` transaction API

Goal:

Create a user table and persist its metadata transactionally.

Build:

- Add method:
  - `Catalog::create_table(name, schema)`
- Steps:
  - validate table name not used
  - validate schema
  - create a table B+ tree root
  - insert row into `dandb_tables`
  - insert rows into `dandb_columns`
  - create primary-key index metadata as internal primary index
  - create internal unique indexes for unique columns
  - insert rows into `dandb_indexes`
  - insert rows into `dandb_index_columns`
  - update in-memory catalog only after transaction succeeds

Requirements:

- Table creation is transactional.
- User table storage is a B+ tree immediately.
- Unique constraints create internal unique B+ trees.
- Primary key index is represented in catalog even though the table B+ tree is the primary storage.

Tests to request:

- Create table and reopen, table exists.
- Duplicate table name rejected.
- Missing primary key rejected.
- Unique column creates internal index metadata.

Common mistakes:

- Updating in-memory catalog before commit.
- Forgetting to create physical roots for unique indexes.
- Treating primary key as a separate duplicate storage tree.

Done when:

- User table metadata can be created and recovered.

Doc note:

- Capture how primary key table storage differs from secondary indexes.

### D09-T06 - Implement drop table through Catalog

Read first:

- `Catalog::create_table`
- System table schemas.

Goal:

Remove a user table's metadata transactionally.

Build:

- Add `Catalog::drop_table(name)`.
- Delete rows from:
  - `dandb_tables`
  - `dandb_columns`
  - `dandb_indexes`
  - `dandb_index_columns`
- Do not reuse freed table or index pages in v1.
- Remove from in-memory catalog only after commit.

Requirements:

- Dropping system tables is rejected.
- Dropping missing table returns not found.
- Existing table pages become unreachable but not reused.

Tests to request:

- Drop table and reopen, table missing.
- Drop system table rejected.
- Drop table removes index metadata.

Common mistakes:

- Trying to reclaim pages before free-list exists.
- Leaving dangling index metadata.

Done when:

- Table metadata can be removed safely.

Doc note:

- Capture why append-only allocation means drop does not shrink the file yet.

### D09-T07 - Implement create and drop secondary index metadata

Read first:

- `Catalog::create_table`
- `IndexDescriptor`

Goal:

Support one optional user-created secondary index per table.

Build:

- Add:
  - `Catalog::create_index(table, index_name, column, unique)`
  - `Catalog::drop_index(index_name)`
- Enforce:
  - at most one non-internal secondary index per table
  - single-column index only
  - indexed column type must be indexable
  - user cannot create index on system table in v1
- Create a B+ tree root for the index.
- Insert rows into `dandb_indexes` and `dandb_index_columns`.

Requirements:

- Internal unique indexes do not count against the one user-created index.
- `CREATE UNIQUE INDEX` enforces uniqueness when built later.
- Dropping internal indexes is rejected.

Tests to request:

- Create secondary index metadata.
- Reject second user secondary index on same table.
- Create unique secondary index metadata.
- Drop user index.
- Reject drop internal index.

Common mistakes:

- Counting internal unique indexes as the user-created index.
- Forgetting `dandb_index_columns`.

Done when:

- Index metadata rules are persistent and tested.

Doc note:

- Capture the difference between primary, internal unique, and user-created secondary indexes.

### D09-T08 - Day 09 review checkpoint

Read first:

- Catalog files and catalog tests.

Goal:

Confirm the database can describe itself before SQL is added.

Build:

- Run all tests.
- Review that no `tables.meta` exists.
- Review system table read/write rules.
- Review catalog corruption tests.

Requirements:

- DB bootstrap works.
- Catalog load works.
- Create/drop table metadata works.
- Create/drop index metadata works.

Tests to request:

- Full test suite.

Common mistakes:

- Starting parser before catalog behavior is stable.

Done when:

- Day 10 can parse SQL into catalog and execution operations.

Doc note:

- Capture the final catalog table list.

## Day 10 - SQL Lexer, Parser, And AST

### D10-T01 - Define SQL tokens

Read first:

- SQL scope section in this plan.

Goal:

Tokenize the small DanDB SQL language.

Build:

- Add `include/dandb/sql/Token.h`.
- Add `include/dandb/sql/Lexer.h`.
- Add `src/sql/Lexer.cpp`.
- Token kinds:
  - identifiers
  - integer literals
  - double literals
  - string literals
  - bool literals
  - null literal
  - punctuation
  - comparison operators
  - keywords
  - end of input

Requirements:

- Keywords are case-insensitive.
- Identifiers are unquoted simple names.
- SQL comments are not supported.
- String literals use single quotes.
- Lexer reports location for errors.

Tests to request:

- Tokenize CREATE TABLE.
- Tokenize INSERT.
- Tokenize SELECT with WHERE.
- Reject unterminated string.
- Reject invalid character.

Common mistakes:

- Letting parser do character-level work.
- Supporting comments accidentally and inconsistently.

Done when:

- SQL input can be converted to tokens with useful errors.

Doc note:

- Capture the deliberately small SQL grammar.

### D10-T02 - Define AST nodes

Read first:

- SQL scope section.
- `Catalog` public methods.

Goal:

Represent parsed SQL statements in C++ before execution.

Build:

- Add `include/dandb/sql/Ast.h`.
- Define AST structs for:
  - `CreateTableStatement`
  - `DropTableStatement`
  - `CreateIndexStatement`
  - `DropIndexStatement`
  - `InsertStatement`
  - `SelectStatement`
  - `UpdateStatement`
  - `DeleteStatement`
  - `BeginStatement`
  - `CommitStatement`
  - `RollbackStatement`
  - `CheckpointStatement`
- Define expression/predicate nodes only for simple literals and one predicate.

Requirements:

- AST should not contain execution pointers.
- AST should preserve enough source location for good error messages.
- Keep AST simple. No visitor framework unless it clearly helps.

Tests to request:

- AST construction can compile.
- No parser tests yet unless parser is started.

Common mistakes:

- Combining parsing and execution.
- Creating a huge expression system for unsupported SQL.

Done when:

- Parser has clear target structures.

Doc note:

- Capture why AST is a middle layer between text and execution.

### D10-T03 - Implement parser for transaction statements

Read first:

- `Lexer`
- `Ast`

Goal:

Parse the simplest statements first.

Build:

- Add `include/dandb/sql/Parser.h`.
- Add `src/sql/Parser.cpp`.
- Parse:
  - `BEGIN;`
  - `COMMIT;`
  - `ROLLBACK;`
  - `CHECKPOINT;`
- Require semicolon.
- Decide whether parser accepts one statement per parse call. Prefer one statement per call for simplicity.

Requirements:

- Extra tokens after semicolon should be rejected or left for CLI statement splitter, but behavior must be explicit.
- Parse errors must include location.

Tests to request:

- Parse each transaction statement.
- Reject missing semicolon.
- Reject unknown statement.

Common mistakes:

- Implementing all parser features at once.
- Hiding semicolon handling in CLI only.

Done when:

- Transaction SQL can produce AST.

Doc note:

- Capture why parser development starts with tiny statements.

### D10-T04 - Implement parser for CREATE TABLE and DROP TABLE

Read first:

- `Schema`
- `LogicalType`
- SQL DDL scope.

Goal:

Parse table definitions into schema descriptions.

Build:

- Parse:
  - table name
  - column list
  - column type
  - `PRIMARY KEY`
  - `UNIQUE`
  - nullable rule if syntax supports `NOT NULL`
- Since defaults are out of scope, reject `DEFAULT`.
- Reject `ALTER`.
- Parse `DROP TABLE name;`.

Requirements:

- Exactly one primary key is required later by binder/schema validation.
- Parser can accept syntax but semantic validation happens in binder or schema layer.
- Type spelling must match:
  - `INT8`
  - `INT16`
  - `INT32`
  - `INT64`
  - `DOUBLE`
  - `STRING(N)`
  - `BOOL`

Tests to request:

- Parse valid CREATE TABLE.
- Parse STRING(N).
- Reject unknown type.
- Reject DEFAULT.
- Parse DROP TABLE.

Common mistakes:

- Letting parser allocate catalog IDs.
- Doing all schema validation in parser.

Done when:

- DDL table statements parse into AST.

Doc note:

- Capture the exact supported CREATE TABLE syntax.

### D10-T05 - Implement parser for CREATE INDEX and DROP INDEX

Read first:

- Index scope section.

Goal:

Parse secondary index statements.

Build:

- Parse:
  - `CREATE INDEX name ON table(column);`
  - `CREATE UNIQUE INDEX name ON table(column);`
  - `DROP INDEX name;`
- Composite indexes are rejected by parser.

Requirements:

- Parser records whether index is unique.
- Parser records table name and column name.
- Parser does not enforce one-index-per-table rule. Catalog/binder handles that.

Tests to request:

- Parse ordinary index.
- Parse unique index.
- Reject multiple columns.
- Parse drop index.

Common mistakes:

- Treating unique index as table constraint only.
- Allowing unsupported composite syntax silently.

Done when:

- Index DDL parses into AST.

Doc note:

- Capture how user-created indexes differ from constraints.

### D10-T06 - Implement parser for INSERT

Read first:

- `Value`
- INSERT scope.

Goal:

Parse the only supported insert form.

Build:

- Parse:
  - `INSERT INTO table_name VALUES (...);`
- Values may be:
  - integer literal
  - double literal
  - string literal
  - bool literal
  - `NULL`
- Reject named column lists.
- Reject multiple row insert in v1.

Requirements:

- Parser preserves value order.
- Value count validation happens later against schema.
- Defaults are not supported because full rows are required.

Tests to request:

- Parse valid full insert.
- Parse null value.
- Reject `INSERT INTO t (a) VALUES`.
- Reject multiple tuples.

Common mistakes:

- Supporting too many insert forms too early.
- Applying schema conversion inside parser.

Done when:

- INSERT statements parse into AST.

Doc note:

- Capture why full-row INSERT simplifies defaults and layout.

### D10-T07 - Implement parser for SELECT

Read first:

- SELECT scope.

Goal:

Parse simple read queries.

Build:

- Parse:
  - `SELECT * FROM table;`
  - `SELECT col1, col2 FROM table;`
  - optional `WHERE column operator literal`
  - optional `WHERE column IS NULL`
  - optional `WHERE column IS NOT NULL`
- Reject joins, order, group, limit, and aggregation.

Requirements:

- Projection list stores either star or ordered column names.
- Predicate is optional and single.
- Parser should produce clear error for unsupported syntax.

Tests to request:

- Parse `SELECT *`.
- Parse projected columns.
- Parse equality WHERE.
- Parse range WHERE.
- Parse IS NULL.
- Reject JOIN.
- Reject ORDER BY.

Common mistakes:

- Accidentally accepting syntax executor cannot run.
- Treating `*` as a normal identifier.

Done when:

- Simple SELECT statements parse into AST.

Doc note:

- Capture the first query grammar supported by DanDB.

### D10-T08 - Implement parser for UPDATE and DELETE

Read first:

- UPDATE and DELETE scope.

Goal:

Parse simple modifying statements.

Build:

- Parse:
  - `UPDATE table SET column = literal WHERE predicate;`
  - `UPDATE table SET column = literal;`
  - `DELETE FROM table WHERE predicate;`
  - `DELETE FROM table;`
- Only one assignment in UPDATE v1.
- Parser does not know whether the assignment targets primary key. Binder/executor rejects that.

Requirements:

- Predicate format matches SELECT predicate.
- Missing WHERE is allowed and means all rows.
- UPDATE assignment value is stored as a literal.

Tests to request:

- Parse update with WHERE.
- Parse update without WHERE.
- Parse delete with WHERE.
- Parse delete without WHERE.
- Reject multiple assignments if unsupported.

Common mistakes:

- Rejecting missing WHERE in parser when scope allows it.
- Letting parser perform table lookup.

Done when:

- All v1 SQL statements can parse.

Doc note:

- Capture why no-WHERE update/delete is allowed but should be used carefully.

### D10-T09 - Implement SQL statement splitter for CLI

Read first:

- `Lexer`
- `Parser`

Goal:

Let CLI collect input until one complete semicolon-terminated SQL statement exists.

Build:

- Add `include/dandb/sql/StatementSplitter.h`.
- Add `src/sql/StatementSplitter.cpp`.
- It should handle semicolons outside string literals.
- It does not support SQL comments.
- It returns one statement at a time.

Requirements:

- Interactive CLI can read multi-line CREATE TABLE.
- Semicolon inside string literal does not split.
- Missing semicolon keeps buffering.

Tests to request:

- Single-line statement.
- Multi-line statement.
- String containing semicolon.
- Two statements typed together if you choose to support returning first and keeping rest.

Common mistakes:

- Splitting on every semicolon without respecting strings.
- Making parser responsible for terminal input buffering.

Done when:

- CLI can later feed complete SQL strings to parser.

Doc note:

- Capture why statement splitting is separate from parsing.

### D10-T10 - Day 10 review checkpoint

Read first:

- All SQL parser tests.

Goal:

Confirm the accepted SQL language exactly matches the intended v1 scope.

Build:

- Run all tests.
- Review unsupported syntax and ensure it errors clearly.
- Add grammar notes to `docs/sql-scope.md` if useful.

Requirements:

- Parser does not execute.
- Parser does not access catalog.
- Parser does not silently accept unsupported SQL.

Tests to request:

- Full test suite.

Common mistakes:

- Allowing syntax because it is easy to parse even though executor will not support it.

Done when:

- Day 11 can bind and execute AST statements.

Doc note:

- Capture the full v1 SQL grammar in compact form.

## Day 11 - Binding, Execution, And CLI

### D11-T01 - Implement Database internal facade

Read first:

- `Pager`
- `Catalog`
- `Parser`

Goal:

Create the internal object used by CLI to open a database and execute SQL.

Build:

- Add `include/dandb/execution/Database.h`.
- Add `src/execution/Database.cpp`.
- Methods:
  - `open_or_create(path)`
  - `execute(sql_string)`
  - `close()`
- Internally owns:
  - Pager
  - Catalog
- This is not a public API feature. It is an internal boundary for clean code and tests.

Requirements:

- CLI should use Database instead of manually wiring Pager and Catalog.
- Database open should create and bootstrap if file does not exist.
- Database open should recover WAL and load catalog.

Tests to request:

- Open new DB through Database.
- Reopen existing DB through Database.
- Bad file returns error.

Common mistakes:

- Turning Database into a large God object.
- Letting CLI own storage objects directly.

Done when:

- There is one internal entry point for executing SQL.

Doc note:

- Capture why an internal facade is useful even without a public API feature.

### D11-T02 - Implement binder for table and column resolution

Read first:

- `Ast`
- `Catalog`
- `Schema`

Goal:

Validate parsed statements against the catalog.

Build:

- Add `include/dandb/sql/Binder.h`.
- Add `src/sql/Binder.cpp`.
- Binder responsibilities:
  - resolve table names
  - resolve column names
  - validate projection columns
  - validate INSERT value count
  - validate UPDATE target column
  - reject primary key updates
  - validate index target columns
  - reject writes to system tables
- Binder should return a bound statement or validation result.

Requirements:

- Parser handles syntax.
- Binder handles names and schema rules.
- Executor should not repeat basic existence checks everywhere.

Tests to request:

- Select missing table fails.
- Select missing column fails.
- Insert wrong value count fails.
- Update primary key rejected.
- Write to system table rejected.

Common mistakes:

- Doing catalog lookup in parser.
- Letting executor discover simple name errors late.

Done when:

- AST can be validated against real catalog metadata.

Doc note:

- Capture parser versus binder responsibilities.

### D11-T03 - Implement execution result format

Read first:

- CLI output expectations.

Goal:

Represent statement results consistently.

Build:

- Add `include/dandb/execution/ExecutionResult.h`.
- Result kinds:
  - success message
  - row set
  - rows affected
  - error status
- Row set includes:
  - column names
  - values as displayable `Value`

Requirements:

- CLI should not inspect internal table pages.
- Tests can assert results without parsing console output.
- Errors still use `Status`.

Tests to request:

- Success result.
- Rows affected result.
- Row set result.

Common mistakes:

- Printing directly from executor.
- Returning raw strings for everything.

Done when:

- Execution and CLI output are separated.

Doc note:

- Capture why result objects make tests cleaner.

### D11-T04 - Implement transaction statement execution

Read first:

- `Pager` transaction API.
- Transaction AST nodes.

Goal:

Wire SQL transaction statements to Pager.

Build:

- In executor or Database:
  - `BEGIN;` calls `Pager::begin`
  - `COMMIT;` calls `Pager::commit`
  - `ROLLBACK;` calls `Pager::rollback`
  - `CHECKPOINT;` calls `Pager::checkpoint`
- Enforce failed transaction rule:
  - after any statement error inside manual transaction, mark transaction failed
  - while failed, reject all statements except rollback

Requirements:

- Autocommit statements are used when no manual transaction is active.
- Checkpoint is rejected during active transaction.
- SQL errors inside manual transaction make it rollback-only.

Tests to request:

- Begin and commit empty transaction.
- Begin then error then select rejected.
- Begin then error then rollback accepted.
- Checkpoint active transaction rejected.

Common mistakes:

- Letting a failed transaction continue.
- Treating parse errors as transaction errors before statement execution begins. Decide and test this clearly.

Done when:

- SQL can control Pager transactions.

Doc note:

- Capture the rollback-only error rule.

### D11-T05 - Implement CREATE TABLE and DROP TABLE execution

Read first:

- Binder
- `Catalog::create_table`
- `Catalog::drop_table`

Goal:

Execute table DDL through SQL.

Build:

- Convert parsed column definitions into `Schema`.
- Call catalog create/drop methods inside transaction context.
- Autocommit if no manual transaction is active.
- Return useful success messages.

Requirements:

- `CREATE TABLE` persists after reopen.
- `DROP TABLE` persists after reopen.
- Schema changes rollback in manual transaction.
- System table writes are rejected.

Tests to request:

- Execute CREATE TABLE then SELECT system catalog.
- Reopen and table still exists.
- Begin, create table, rollback, table missing.
- Drop table removes metadata.

Common mistakes:

- Running DDL outside transactions.
- Updating catalog memory before commit succeeds.

Done when:

- Users can create and drop tables with SQL.

Doc note:

- Capture that DanDB DDL is transactional.

### D11-T06 - Implement INSERT execution for primary table B+ tree

Read first:

- `RowCodec`
- `BTree`
- `Catalog`

Goal:

Insert full rows into the table's clustered B+ tree.

Build:

- Validate values against schema.
- Encode row.
- Extract primary key.
- Insert into table B+ tree.
- Enforce primary key uniqueness.
- Do not maintain secondary or unique indexes yet unless Day 12 has already been completed. For this task, if unique indexes exist from schema, either implement their maintenance here or explicitly mark task incomplete until D12. Preferred: basic internal unique maintenance can wait until D12 only if tests do not create unique columns yet.

Requirements:

- Full value count required.
- Type overflow errors fail statement.
- Insert participates in active transaction or autocommit.
- Duplicate primary key rejected.

Tests to request:

- Insert row and select by primary key after later select task.
- Duplicate primary key rejected.
- Insert overflow rejected.
- Insert rollback removes row.
- Insert commit survives reopen.

Common mistakes:

- Forgetting index maintenance once unique indexes are added.
- Encoding row before all validations pass.

Done when:

- Table B+ tree can store user rows through SQL.

Doc note:

- Capture insert path from SQL literal to B+ tree entry.

### D11-T07 - Implement SELECT execution

Read first:

- `BTree::find`
- `BTree::scan`
- `BTree::range_scan`
- Binder projection validation.

Goal:

Read rows from table B+ trees and return row sets.

Build:

- For no WHERE, scan table B+ tree.
- For WHERE on primary key:
  - use exact lookup or range scan.
- For WHERE on non-indexed column:
  - full scan and filter.
- Apply projection after filtering.
- Implement null predicate behavior:
  - `IS NULL` and `IS NOT NULL` work.
  - ordinary comparisons with NULL do not match.

Requirements:

- System tables can be selected like normal tables.
- SELECT does not modify pages.
- SELECT inside failed transaction is rejected by transaction rule.

Tests to request:

- Select all rows.
- Select projected columns.
- Select by primary key.
- Select by non-indexed column.
- Select system tables.
- Null predicate behavior.

Common mistakes:

- Implementing SELECT by reading catalog memory for system tables instead of using system table B+ trees.
- Forgetting projection order.

Done when:

- Users can inspect both user data and system catalog through SQL.

Doc note:

- Capture how SELECT chooses between lookup and scan.

### D11-T08 - Implement UPDATE execution

Read first:

- `Row` helpers.
- `BTree` update/delete/insert behavior.
- Binder update validation.

Goal:

Modify non-primary-key fields in matching rows.

Build:

- Find matching rows using same access strategy as SELECT.
- Reject primary key update.
- Decode each row.
- Replace target column value after type validation.
- Re-encode row.
- Update table B+ tree entry.
- Index maintenance for secondary/unique indexes is completed in Day 12. If not implemented yet, document this as temporary and keep tests limited.

Requirements:

- UPDATE without WHERE updates all rows.
- Overflow errors fail statement and mark manual transaction failed.
- Autocommit UPDATE should be atomic.
- Manual transaction rollback should restore previous rows.

Tests to request:

- Update one row.
- Update all rows.
- Reject primary key update.
- Overflow update rejected.
- Rollback update restores row.

Common mistakes:

- Updating rows while scanning in a way that invalidates cursor state.
- Failing halfway through multi-row update without transaction rollback.

Done when:

- Non-indexed row updates work transactionally.

Doc note:

- Capture why updates must be atomic even when many rows match.

### D11-T09 - Implement DELETE execution

Read first:

- `BTree::erase`
- SELECT predicate evaluation.

Goal:

Delete matching rows from the table B+ tree.

Build:

- Find matching primary keys first.
- Delete rows by primary key after collection.
- DELETE without WHERE deletes all rows.
- Secondary/unique index maintenance is completed in Day 12.

Requirements:

- DELETE participates in transaction.
- Deleting missing rows through WHERE affects zero rows.
- Rollback restores deleted rows.

Tests to request:

- Delete one row.
- Delete all rows.
- Delete no matching rows.
- Rollback delete restores rows.
- Commit delete survives reopen.

Common mistakes:

- Deleting while scanning leaf pages and corrupting cursor traversal.
- Treating zero affected rows as error.

Done when:

- Basic row deletion works transactionally.

Doc note:

- Capture why collecting keys before deleting is safer.

### D11-T10 - Implement CLI/REPL

Read first:

- `Database`
- `StatementSplitter`
- `ExecutionResult`

Goal:

Provide the actual user-facing DanDB interface.

Build:

- Add `include/dandb/cli/Repl.h`.
- Add `src/cli/Repl.cpp`.
- Add `src/cli/main.cpp`.
- CLI behavior:
  - user runs `dandb <path>`
  - database opens or creates the file
  - prompt reads SQL until semicolon
  - executes statement
  - prints rows or success/error messages
  - exits on EOF or process interrupt
- No meta commands in v1.

Requirements:

- CLI should not know storage details.
- Errors should be readable and not crash the process.
- Multi-line statements should be comfortable enough for CREATE TABLE.

Tests to request:

- CLI component tests can be light.
- Prefer integration tests through `Database::execute`.
- Optional process-level CLI smoke test.

Common mistakes:

- Adding `.tables`, `.schema`, or other meta commands despite v1 scope.
- Printing from deep execution code instead of CLI.

Done when:

- A user can create a DB, create a table, insert, select, update, delete, commit, rollback, and checkpoint from the CLI.

Doc note:

- Capture the first real CLI demo transcript.

### D11-T11 - Day 11 review checkpoint

Read first:

- Execution and CLI files.

Goal:

Confirm the database is usable before secondary indexes are fully maintained.

Build:

- Run all tests.
- Manually run a short CLI session.
- Review transaction error behavior.
- Review unsupported SQL errors.

Requirements:

- Core SQL works through CLI.
- Storage durability still works.
- Known index maintenance limitations are explicitly resolved in Day 12.

Tests to request:

- Full test suite.

Common mistakes:

- Continuing with indexes before basic SQL is stable.

Done when:

- Day 12 can add secondary and unique index maintenance.

Doc note:

- Capture what DanDB can do for the first time as a usable database.

## Day 12 - Secondary Indexes And Unique Constraints

### D12-T01 - Implement index descriptor to B+ tree opening

Read first:

- `Catalog::indexes_for_table`
- `BTree`
- `KeyCodec`

Goal:

Open an index B+ tree from catalog metadata.

Build:

- Add helper in execution or catalog layer:
  - `open_table_tree(table_descriptor)`
  - `open_index_tree(index_descriptor)`
- Determine key layout from indexed column.
- Determine value layout:
  - unique index stores indexed key -> primary key
  - non-unique index stores composite identity `(indexed key, primary key)` or indexed key with primary key payload

Requirements:

- Primary table tree is opened differently from secondary indexes.
- Index code should not duplicate catalog lookup everywhere.

Tests to request:

- Open primary table tree.
- Open internal unique index tree.
- Open user secondary index tree.

Common mistakes:

- Treating primary table root like a separate secondary index root.
- Forgetting that non-unique index entries need primary key to distinguish duplicates.

Done when:

- Execution code can access every index tree for a table.

Doc note:

- Capture index tree layouts.

### D12-T02 - Build index contents for CREATE INDEX

Read first:

- `Catalog::create_index`
- `BTree::scan`

Goal:

When user creates an index on a table with existing rows, populate the index.

Build:

- During `CREATE INDEX` execution:
  - validate metadata rules
  - create index metadata and root
  - scan table rows
  - insert corresponding index entries
  - for unique indexes, reject duplicates before commit
- If build fails, transaction rollback removes metadata and index pages from reachability.

Requirements:

- `CREATE UNIQUE INDEX` fails if existing data violates uniqueness.
- Ordinary index allows duplicate indexed values.
- Index build is transactional.

Tests to request:

- Create index on empty table.
- Create index on populated table.
- Create unique index succeeds with unique values.
- Create unique index fails with duplicates and leaves no index metadata.

Common mistakes:

- Creating metadata before checking duplicates and not rolling it back.
- Building index only for future rows.

Done when:

- Created indexes are immediately usable and correct.

Doc note:

- Capture why index creation scans existing table data.

### D12-T03 - Maintain indexes on INSERT

Read first:

- INSERT execution.
- Index opening helper.

Goal:

Keep secondary and unique indexes correct when rows are inserted.

Build:

- During INSERT:
  - validate row
  - extract primary key
  - check primary key uniqueness
  - check internal unique indexes
  - check unique user-created index if present
  - insert table row
  - insert all index entries
- Ensure all happens in one transaction.

Requirements:

- Unique constraint violation rejects insert.
- Ordinary secondary index gets entry.
- If any index insert fails, whole INSERT rolls back in autocommit mode or marks manual transaction failed.

Tests to request:

- Insert updates ordinary index.
- Insert duplicate unique column rejected.
- Insert duplicate primary key rejected.
- Failed insert leaves no partial table row or index entry.

Common mistakes:

- Inserting table row before uniqueness checks and failing to undo.
- Maintaining user index but forgetting internal unique indexes.

Done when:

- INSERT keeps every index consistent.

Doc note:

- Capture the correct insert order with uniqueness checks.

### D12-T04 - Maintain indexes on UPDATE

Read first:

- UPDATE execution.
- Row projection helpers.

Goal:

Update index entries when indexed column values change.

Build:

- For each updated row:
  - decode old row
  - build new row
  - determine affected indexes
  - check uniqueness for new indexed values before mutating
  - remove old index entries
  - update table row
  - insert new index entries
- If updated column is not indexed, index maintenance should do nothing.

Requirements:

- Primary key updates remain rejected.
- Updating unique column to duplicate value fails atomically.
- Updating non-indexed column does not touch indexes.

Tests to request:

- Update indexed column changes index lookup result.
- Update unique column to duplicate fails.
- Update non-indexed column leaves index valid.
- Multi-row update rollback restores index entries.

Common mistakes:

- Forgetting to remove old index entry.
- Checking uniqueness after deleting old entry in a way that mishandles no-op updates.
- Updating table row and then failing index insert.

Done when:

- UPDATE keeps table and indexes consistent.

Doc note:

- Capture why index maintenance is often the hardest part of UPDATE.

### D12-T05 - Maintain indexes on DELETE

Read first:

- DELETE execution.
- Index opening helper.

Goal:

Remove index entries when rows are deleted.

Build:

- Before deleting a table row, decode it and derive index entries.
- Remove entries from all internal and user indexes.
- Delete table row.
- If any delete fails unexpectedly, fail the statement.

Requirements:

- Deleting rows leaves no dangling secondary index entries.
- DELETE without WHERE updates indexes for all removed rows.
- Rollback restores both table rows and index entries through page-level rollback.

Tests to request:

- Delete removes ordinary index entry.
- Delete removes unique index entry.
- Delete all clears index scans.
- Rollback delete restores index lookup.

Common mistakes:

- Deleting table row first and losing values needed to delete index entries.
- Ignoring not-found in index delete when it indicates corruption.

Done when:

- DELETE keeps every index consistent.

Doc note:

- Capture why delete needs the old row value.

### D12-T06 - Use indexes in SELECT planning

Read first:

- SELECT execution.
- B+ tree range scan.
- Catalog index lookup.

Goal:

Choose an index when a simple WHERE predicate can use one.

Build:

- Add minimal planner logic:
  - primary key predicate uses table B+ tree
  - predicate on user-created secondary index uses that index
  - predicate on internal unique index may use that index
  - otherwise full table scan
- For secondary index lookup:
  - find primary keys from index
  - fetch rows from table B+ tree
  - apply predicate again as a safety check

Requirements:

- Planner should be simple and deterministic.
- No EXPLAIN output in v1.
- Correctness matters more than picking the perfect plan.

Tests to request:

- SELECT uses primary key path and returns correct row.
- SELECT using secondary indexed column returns matching rows.
- SELECT using unique column returns one row.
- SELECT on non-indexed column still works.

Common mistakes:

- Returning index payload as if it were a table row.
- Forgetting to re-fetch table rows from primary tree.

Done when:

- Secondary indexes improve SELECT behavior while preserving correctness.

Doc note:

- Capture how secondary index lookup becomes primary key lookup.

### D12-T07 - Enforce one optional user-created index per table

Read first:

- Catalog create index tests.
- SQL create index execution.

Goal:

Make the index-scope rule visible and reliable.

Build:

- Ensure `CREATE INDEX` and `CREATE UNIQUE INDEX` both count as the one optional user-created index.
- Internal primary and unique constraint indexes do not count.
- Error message should explain the rule clearly.

Requirements:

- A table with many unique constraints can still have one user-created secondary index.
- A table with one ordinary index cannot create another user index.
- Dropping the user index allows creating a new one.

Tests to request:

- Unique constraints plus one user index allowed.
- Second user index rejected.
- Drop user index then create another succeeds.

Common mistakes:

- Counting internal unique indexes against the limit.
- Failing to free the slot after DROP INDEX.

Done when:

- The balanced index scope is enforced exactly.

Doc note:

- Capture the reason for limiting user-created indexes in v1.

### D12-T08 - Add index consistency checker

Read first:

- B+ tree validator.
- Catalog index metadata.

Goal:

Detect table/index mismatches in tests and maybe a future debug command.

Build:

- Add `include/dandb/execution/ConsistencyChecker.h` or keep in test utilities if preferred.
- Checker scans each table:
  - verifies every row has matching index entries
  - verifies every index entry points to an existing table row
  - verifies unique indexes have no duplicates

Requirements:

- Use in tests after complex insert/update/delete sequences.
- Return detailed error messages.
- Do not expose as user CLI command in v1 unless desired later.

Tests to request:

- Valid table/index pair passes.
- Missing index entry fails.
- Dangling index entry fails.

Common mistakes:

- Trusting index maintenance without a checker.
- Making checker mutate data.

Done when:

- Tests can prove indexes remain synchronized.

Doc note:

- Capture why database engines need consistency checks.

### D12-T09 - Day 12 review checkpoint

Read first:

- Index execution and catalog files.

Goal:

Confirm primary, secondary, and unique indexes are stable.

Build:

- Run all tests.
- Run integration test sequences:
  - create table with unique constraint
  - create user index
  - insert rows
  - update indexed values
  - delete rows
  - rollback and commit variations
  - checkpoint and reopen
- Review error messages for uniqueness violations.

Requirements:

- Table and indexes stay consistent across transactions and recovery.
- The one-user-index rule is enforced.

Tests to request:

- Full test suite.

Common mistakes:

- Calling the storage engine complete before indexes survive recovery tests.

Done when:

- Day 13 can focus on polish, SQL behavior, and crash hardening.

Doc note:

- Capture the final index model.

## Day 13 - SQL Behavior Hardening

### D13-T01 - Harden identifier normalization

Read first:

- Lexer
- Parser
- Catalog name lookup.

Goal:

Make table, column, and index name behavior consistent.

Build:

- Decide and implement normalization:
  - recommended: store identifiers lower-case.
  - preserve original spelling only if needed for display.
- Apply consistently in parser/binder/catalog.
- Ensure keywords remain case-insensitive.

Requirements:

- `CREATE TABLE Users` and `SELECT * FROM users` should behave consistently if lower-case normalization is chosen.
- Duplicate names differing only by case should be rejected.

Tests to request:

- Case-insensitive keyword tests.
- Identifier normalization tests.
- Duplicate name case variant rejected.

Common mistakes:

- Normalizing in some layers but not others.
- Making system table names case-sensitive accidentally.

Done when:

- Name behavior is predictable.

Doc note:

- Capture the identifier rules.

### D13-T02 - Harden predicate evaluation

Read first:

- SELECT/UPDATE/DELETE predicate code.
- `Value` comparison code.

Goal:

Make WHERE behavior consistent across statements.

Build:

- Centralize predicate evaluation.
- Implement comparisons for supported comparable types.
- Implement NULL rules:
  - `IS NULL` and `IS NOT NULL` work.
  - ordinary comparisons involving NULL are false.
- Reject comparing incompatible types.

Requirements:

- SELECT, UPDATE, and DELETE share the same predicate logic.
- Indexed and non-indexed execution must return the same result.

Tests to request:

- Predicate behavior for each type.
- NULL comparison behavior.
- Same WHERE result with and without index.

Common mistakes:

- Duplicating WHERE logic in three executors.
- Letting index scan and full scan disagree.

Done when:

- WHERE has one correct implementation.

Doc note:

- Capture DanDB's simple NULL predicate rules.

### D13-T03 - Harden transaction error handling

Read first:

- `Database::execute`
- Transaction statement execution.

Goal:

Make all statement failures inside manual transactions mark the transaction unusable.

Build:

- Audit every execution path.
- On any parse, bind, constraint, or storage error during an active manual transaction, mark transaction failed unless the statement is `ROLLBACK`.
- Decide whether parse errors before execution should affect transaction. User preference is easiest behavior: all errors make transaction unusable.
- Ensure autocommit errors roll back the statement and leave no active failed transaction.

Requirements:

- Failed transaction accepts only `ROLLBACK`.
- `COMMIT` after failure returns transaction error.
- User gets a clear message explaining rollback is required.

Tests to request:

- Bind error inside transaction poisons transaction.
- Constraint error inside transaction poisons transaction.
- Parse error inside transaction poisons transaction if implemented at Database level.
- Rollback clears failed state.
- Autocommit error does not poison future statements.

Common mistakes:

- Missing one error path.
- Poisoning transaction after rollback itself fails.

Done when:

- Transaction error behavior is uniform and easy to explain.

Doc note:

- Capture why this simple rule is easier than per-statement recovery.

### D13-T04 - Harden autocommit atomicity

Read first:

- Database execution paths.
- Pager transaction API.

Goal:

Ensure every standalone modifying statement is atomic.

Build:

- For each modifying statement outside manual transaction:
  - begin internal autocommit transaction
  - execute statement
  - commit on success
  - rollback on failure
- SELECT may not need a transaction unless Pager requires one for read consistency.
- DDL, INSERT, UPDATE, DELETE, CREATE INDEX, DROP INDEX all use autocommit.

Requirements:

- Partial multi-row UPDATE/DELETE cannot persist on failure.
- Failed CREATE INDEX leaves no metadata.
- Failed INSERT leaves no table or index entries.

Tests to request:

- Autocommit insert failure leaves no partial row.
- Autocommit update failure leaves all rows unchanged.
- Autocommit create unique index failure leaves no index metadata.

Common mistakes:

- Treating one SQL statement as one operation but forgetting it may touch many pages.
- Rolling back only table rows but not catalog/index changes.

Done when:

- Every standalone write statement is atomic.

Doc note:

- Capture what autocommit means internally.

### D13-T05 - Improve user-facing error messages

Read first:

- Status messages from parser, binder, storage, catalog.

Goal:

Make CLI errors educational and precise.

Build:

- Review common errors:
  - unknown table
  - unknown column
  - duplicate primary key
  - unique violation
  - type overflow
  - string overflow
  - unsupported SQL
  - transaction failed
  - database corruption
- Include enough context but avoid huge messages.

Requirements:

- Error messages should help the user fix SQL.
- Corruption and I/O errors should sound serious and specific.
- Messages should not expose internal file offsets unless useful.

Tests to request:

- Selected error message snapshot or substring tests.

Common mistakes:

- Returning `InternalError` for user mistakes.
- Giving vague "failed" messages.

Done when:

- CLI feels understandable when things go wrong.

Doc note:

- Capture examples of good database errors.

### D13-T06 - Add recovery integration scenarios

Read first:

- Pager recovery tests.
- Execution tests.

Goal:

Verify SQL-level operations survive close/open and WAL recovery.

Build:

- Add tests:
  - create table, insert, reopen before checkpoint
  - create index, insert, reopen before checkpoint
  - update indexed column, reopen before checkpoint
  - delete, reopen before checkpoint
  - checkpoint, reopen with empty WAL
  - rollback transaction, reopen

Requirements:

- Tests use real Database execution where practical.
- Tests verify both table rows and system catalog.

Tests to request:

- All listed integration scenarios.

Common mistakes:

- Testing only raw pages and assuming SQL recovery works.
- Forgetting catalog changes are just page changes too.

Done when:

- WAL recovery is proven at SQL feature level.

Doc note:

- Capture a crash-recovery story using SQL examples.

### D13-T07 - Add manual CLI smoke scripts

Read first:

- CLI behavior.

Goal:

Make it easy to manually try the database.

Build:

- Add `scripts/smoke.sql`.
- Include:
  - create table
  - insert rows
  - select
  - update
  - create index
  - select by index
  - delete
  - transaction rollback example
  - checkpoint
- If CLI does not support file input, the script is still copy-paste material.

Requirements:

- Do not add complex script runner unless needed.
- Keep script aligned with supported SQL only.

Tests to request:

- Optional manual run.

Common mistakes:

- Adding unsupported SQL to the smoke script.

Done when:

- There is a simple demo path for humans.

Doc note:

- This smoke script can later become the first blog demo.

### D13-T08 - Day 13 review checkpoint

Read first:

- SQL, execution, CLI, and recovery tests.

Goal:

Confirm user-visible behavior is coherent.

Build:

- Run all tests.
- Manually run CLI smoke session.
- Review unsupported feature errors.
- Review transaction failure behavior.

Requirements:

- Basic SQL is reliable.
- Errors are understandable.
- Recovery works through SQL-level tests.

Tests to request:

- Full test suite.

Common mistakes:

- Spending time on new SQL features instead of hardening current behavior.

Done when:

- Day 14 can focus on crash safety and corruption tests.

Doc note:

- Capture what SQL DanDB supports and intentionally does not support.

## Day 14 - Crash Safety, Corruption, And Cleanup

### D14-T01 - Create crash test harness

Read first:

- File fault injection hooks.
- WAL tests.
- Database execution tests.

Goal:

Test interrupted writes and sync failures systematically.

Build:

- Add `tests/testutil/FaultInjection.h` if not already present.
- Add helpers to run a SQL operation with injected failure at a chosen file operation.
- Reopen database after failure and verify recovery outcome.

Requirements:

- Tests must be deterministic.
- Each failure point should clearly state expected outcome.
- Harness should not require actually killing the process.

Tests to request:

- Failure before WAL frame write.
- Failure during WAL frame write.
- Failure before commit record sync.
- Failure during checkpoint main DB write.
- Failure before WAL reset.

Common mistakes:

- Calling something crash-safe without simulating failures.
- Making flaky tests that depend on timing.

Done when:

- Crash scenarios are easy to add.

Doc note:

- Capture why deterministic fault injection is better than random process killing for early testing.

### D14-T02 - Test WAL tail recovery rules

Read first:

- `WalScanner`
- Recovery tests.

Goal:

Ensure WAL handles incomplete endings correctly.

Build:

- Add tests:
  - valid committed transaction followed by partial garbage tail
  - valid frames without commit
  - partial commit record
  - corrupt frame before commit
  - corrupt frame in committed transaction

Requirements:

- Harmless tail after last committed transaction can be ignored.
- Uncommitted tail can be ignored.
- Corruption inside committed transaction fails open.

Tests to request:

- All listed WAL tail cases.

Common mistakes:

- Ignoring too much corruption.
- Failing on harmless trailing bytes after a crash.

Done when:

- WAL scanner rules are locked by tests.

Doc note:

- Capture the exact tail-handling rule.

### D14-T03 - Test checkpoint crash scenarios

Read first:

- `Pager::checkpoint`
- WAL reset behavior.

Goal:

Prove checkpoint does not destroy committed data when interrupted.

Build:

- Inject failure:
  - before writing first main DB page
  - after writing some main DB pages
  - before main DB sync
  - after main DB sync but before WAL reset
  - during WAL reset
- Reopen database and verify committed state is still available.

Requirements:

- WAL is reset only after main DB pages are durable.
- If reset fails, database should still reopen or report a controlled error according to chosen design.
- No committed data loss.

Tests to request:

- All listed checkpoint crash cases.

Common mistakes:

- Assuming checkpoint is safe because commit is safe.
- Resetting WAL too early.

Done when:

- Manual checkpoint is robust against power-loss style failures.

Doc note:

- Capture checkpoint's failure-safe ordering.

### D14-T04 - Add database corruption tests

Read first:

- Header corruption tests.
- B+ tree validator.
- Catalog load validation.

Goal:

Reject corrupted database structures predictably.

Build:

- Add tests for:
  - bad main DB header
  - bad WAL header
  - page count smaller than referenced root
  - catalog table missing required row
  - B+ tree page with invalid kind
  - B+ tree page with unsorted keys
  - index entry pointing to missing table row

Requirements:

- Corruption should not crash.
- Corruption should not silently repair.
- Error should be `Corruption` where appropriate.

Tests to request:

- All listed corruption cases.

Common mistakes:

- Treating corrupted user data as not found.
- Allowing invalid page IDs to propagate until a crash.

Done when:

- DanDB fails safely on invalid files.

Doc note:

- Capture examples of corruption checks DanDB performs.

### D14-T05 - Audit direct file and storage dependencies

Read first:

- Entire new source tree.

Goal:

Ensure architecture boundaries stayed clean.

Build:

- Search for direct uses of:
  - OS file APIs outside platform module
  - `std::fstream`
  - DiskManager outside Pager
  - WalManager outside Pager
  - BufferPoolManager outside Pager and tests
- Refactor small leaks if found.

Requirements:

- Catalog, B+ tree, SQL, and execution should depend on Pager or higher abstractions.
- Platform-specific code stays in platform module.

Tests to request:

- Full test suite after cleanup.

Common mistakes:

- Accepting one direct DiskManager call because it is convenient.
- Refactoring too broadly instead of fixing boundary leaks.

Done when:

- Dependency direction matches the architecture.

Doc note:

- Capture the final dependency rule.

### D14-T06 - Remove unused legacy concepts from the new repo

Read first:

- New repo tree.
- This plan's legacy code reference map.

Goal:

Make sure the repo did not accidentally inherit old architecture.

Build:

- Confirm there is no:
  - `tables.meta`
  - old `PagedTable` as normal table storage
  - old heap/slotted table path as active storage
  - old free-list implementation
  - direct legacy serialization map dependency
- If any old fragment was copied, ensure it was renamed and adapted to the new architecture.

Requirements:

- The new repo should feel intentionally designed, not half-copied.
- Keep only code that has a current purpose.

Tests to request:

- Full test suite.

Common mistakes:

- Keeping old files "just in case".
- Leaving dead code that confuses future tasks.

Done when:

- The codebase contains only the new architecture.

Doc note:

- Capture which legacy ideas were reused and which were intentionally discarded.

### D14-T07 - Add small performance sanity checks

Read first:

- B+ tree tests.
- SQL execution tests.

Goal:

Check that obvious operations are not accidentally terrible.

Build:

- Add non-strict benchmark-like tests or manual scripts for:
  - inserting many rows
  - selecting by primary key
  - selecting by secondary index
  - full table scan
- These should be sanity checks, not fragile performance gates.

Requirements:

- Do not fail tests based on tiny timing differences.
- Use these to catch accidental O(n^2) behavior in simple paths.

Tests to request:

- Optional benchmark executable or manual script.

Common mistakes:

- Optimizing before correctness.
- Creating flaky timing tests.

Done when:

- There is a basic sense of performance behavior.

Doc note:

- Capture what affects DanDB performance: table design, indexes, B+ tree height, scans, and WAL/checkpoint behavior.

### D14-T08 - Day 14 review checkpoint

Read first:

- Crash, corruption, and cleanup tests.

Goal:

Confirm DanDB is durable and clean enough for final polish.

Build:

- Run all tests.
- Review crash scenario coverage.
- Review corruption error messages.
- Review architecture dependency search results.

Requirements:

- WAL and checkpoint crash tests pass.
- Corruption tests fail safely.
- No old architecture files remain active.

Tests to request:

- Full test suite.

Common mistakes:

- Calling the rewrite done before crash tests exist.

Done when:

- Day 15 can focus on final usability, README, and learning artifacts.

Doc note:

- Capture the durability guarantees DanDB v1 actually provides.

## Day 15 - Final Polish, README, And Learning Artifacts

### D15-T01 - Write the main README

Read first:

- Current implemented SQL scope.
- `scripts/smoke.sql`
- This plan's locked scope.

Goal:

Explain what DanDB is, how to build it, and what it supports.

Build:

- Update `README.md` with:
  - project summary
  - build instructions
  - CLI usage
  - supported SQL
  - storage architecture summary
  - durability model
  - limitations
- Keep tone casual and clear.

Requirements:

- Do not pretend DanDB is production-ready.
- Emphasize educational database engine and learning value.
- Include a short CLI example.

Tests to request:

- No code tests.
- Manually follow build instructions.

Common mistakes:

- Writing marketing copy instead of honest project documentation.
- Hiding limitations.

Done when:

- A new reader understands what DanDB does in five minutes.

Doc note:

- README becomes the first public-facing explanation.

### D15-T02 - Add architecture notes

Read first:

- Pager, WAL, B+ tree, catalog modules.

Goal:

Create concise internal notes that explain the system for future you.

Build:

- Add `docs/architecture.md`.
- Include sections:
  - module ownership
  - file format overview
  - WAL lifecycle
  - transaction lifecycle
  - B+ tree table storage
  - catalog system tables
  - index model
- Use diagrams in text or Mermaid later if desired.

Requirements:

- Keep it explanatory, not formal API docs.
- Do not document features that do not exist.

Tests to request:

- No code tests.

Common mistakes:

- Letting docs drift from code.
- Making it too formal for the project tone.

Done when:

- A future chat can read architecture notes and understand the design quickly.

Doc note:

- This file is the architecture note.

### D15-T03 - Add file format notes

Read first:

- `DatabaseHeader`
- `WalHeader`
- `WalRecord`
- `BTreePage`
- `RowCodec`

Goal:

Document the bytes on disk enough for debugging and future changes.

Build:

- Add `docs/file-format.md`.
- Include:
  - DB header fields
  - WAL header fields
  - WAL frame and commit record fields
  - row layout
  - B+ tree page header
  - index entry layout
  - versioning rules

Requirements:

- Keep field sizes explicit.
- Mention endian rules.
- Mention checksums.

Tests to request:

- No code tests.

Common mistakes:

- Leaving file format only in code.
- Forgetting that future format changes need versioning.

Done when:

- On-disk data can be inspected with the docs beside the code.

Doc note:

- This can become one of the strongest portfolio posts.

### D15-T04 - Add limitations and future work list

Read first:

- README.
- SQL scope.

Goal:

Make project boundaries honest and intentional.

Build:

- Add `docs/limitations.md` or a README section.
- Include not supported:
  - joins
  - aggregation
  - ORDER BY
  - LIMIT
  - ALTER TABLE
  - composite indexes
  - concurrent readers/writers
  - automatic checkpoint
  - free-list/page reuse
  - variable-length rows
  - production-grade recovery guarantees
- Include possible future work:
  - page reuse
  - query planner improvements
  - automatic checkpoints
  - more SQL grammar
  - docs/blog site

Requirements:

- Limitations should sound deliberate, not apologetic.
- Future work should be realistic.

Tests to request:

- No code tests.

Common mistakes:

- Omitting limitations to look more impressive.
- Listing future work that conflicts with current architecture.

Done when:

- Readers know exactly what DanDB v1 is and is not.

Doc note:

- This list helps future blog posts stay honest.

### D15-T05 - Create casual docs/blog decision note

Read first:

- Documentation Direction section in this plan.
- Notes captured during previous tasks.

Goal:

Choose how to present DanDB publicly after implementation.

Build:

- Add `docs/publishing-options.md`.
- Compare:
  - simple Markdown docs in repo
  - README plus blog posts
  - small Astro/Starlight site
  - small VitePress site
  - Docusaurus only if you later want a more formal docs portal
- Recommend one based on how you feel after finishing the engine.
- Keep decision casual. The project does not need a heavy docs platform.

Requirements:

- Do not install a docs framework in this task unless you are ready to maintain it.
- The main outcome is a decision note and a list of captured topics.

Tests to request:

- No code tests.

Common mistakes:

- Spending more time on docs tooling than explaining the actual database.
- Choosing a platform before knowing the writing style.

Done when:

- You know what kind of public learning artifact you want.

Doc note:

- This is the docs/blog planning note.

### D15-T06 - Prepare portfolio-quality demo

Read first:

- README.
- CLI smoke script.
- Implemented SQL behavior.

Goal:

Create one impressive but honest demo path.

Build:

- Add `docs/demo.md`.
- Include:
  - create database
  - create table with primary key and unique constraint
  - insert rows
  - create secondary index
  - select by primary key
  - select by indexed column
  - demonstrate transaction rollback
  - demonstrate checkpoint
  - show that reopening preserves data
- Keep it runnable with current CLI.

Requirements:

- Demo must not use unsupported syntax.
- Demo should showcase WAL, B+ trees, catalog, indexes, and SQL.

Tests to request:

- Manually run the demo commands.

Common mistakes:

- Making a demo that hides the interesting storage work.
- Using data that does not exercise indexes.

Done when:

- A recruiter, interviewer, or future you can see the project's value quickly.

Doc note:

- This can become the first public article or README demo.

### D15-T07 - Final code cleanup audit

Read first:

- Entire source tree.

Goal:

Improve readability without changing behavior.

Build:

- Remove dead code.
- Rename unclear classes or methods.
- Shorten overlong functions where it improves understanding.
- Add comments only around non-obvious storage invariants.
- Ensure file names match responsibilities.

Requirements:

- Do not start new features.
- Do not refactor working code just to make it look different.
- Tests must pass after each cleanup group.

Tests to request:

- Full test suite.

Common mistakes:

- Breaking behavior during cosmetic cleanup.
- Adding abstractions that do not reduce complexity.

Done when:

- The code reads like a deliberately designed project.

Doc note:

- Capture the most important readability improvements made.

### D15-T08 - Final test matrix

Read first:

- All test folders.

Goal:

Make sure test coverage matches the architecture.

Build:

- Review tests by module:
  - core
  - platform
  - storage
  - wal
  - buffer
  - transaction
  - record
  - btree
  - catalog
  - sql
  - execution
  - cli
- Add missing high-value tests only.
- Avoid low-value tests that just duplicate implementation.

Requirements:

- Every major invariant has at least one test.
- Crash and corruption tests exist.
- SQL integration tests cover normal user flows.

Tests to request:

- Full test suite.

Common mistakes:

- Counting many tiny tests as coverage if important workflows are missing.
- Leaving crash safety untested because it is inconvenient.

Done when:

- Tests explain the intended behavior almost as well as docs.

Doc note:

- Capture a testing strategy summary.

### D15-T09 - Final learning review

Read first:

- Notes captured during tasks.
- README.
- Architecture docs.

Goal:

Turn the rewrite into learning you can explain.

Build:

- Write `docs/learning-review.md`.
- Answer:
  - What corruption risks existed before?
  - How WAL changes durability?
  - Why Pager owns WAL coordination?
  - Why B+ tree table storage was chosen?
  - Why fixed rows were chosen?
  - Why system tables replaced `tables.meta`?
  - What was hardest about indexes?
  - What would be next?

Requirements:

- Use your own words.
- Keep it honest.
- Mention tradeoffs.

Tests to request:

- No code tests.

Common mistakes:

- Writing generic database theory instead of reflecting on DanDB decisions.

Done when:

- You can explain the project clearly in an interview or blog post.

Doc note:

- This is the future blog backbone.

### D15-T10 - Final release checkpoint

Read first:

- README.
- All docs.
- Full test output.

Goal:

End the rewrite with a coherent v1 snapshot.

Build:

- Run the full test suite.
- Run the CLI demo.
- Open a database, commit data, reopen before checkpoint.
- Run checkpoint, reopen after checkpoint.
- Confirm `.wal` behavior.
- Confirm no unsupported docs claims.
- Create a short list of future issues.

Requirements:

- No known corruption bugs.
- No known index consistency bugs.
- No known transaction atomicity bugs.
- Limitations are documented.

Tests to request:

- Full test suite.
- Manual CLI demo.

Common mistakes:

- Ending with code that works only in tests but has no clear demo.
- Claiming production readiness.

Done when:

- DanDB v1 is a clean, understandable, CLI-driven educational database with WAL, B+ tree tables, system tables, transactions, recovery, indexes, and a credible learning story.

Doc note:

- Capture the final v1 summary and future roadmap.
