# Coding conventions

DanDB keeps its conventions small and explicit so future code stays easy to read and review.

## Namespaces

- Project code lives under the `dandb` namespace.
- Module code uses nested namespaces such as `dandb::core`, `dandb::catalog`, and `dandb::storage`.
- Test-only helpers use `dandb::testutil`.

## File layout and includes

- Public headers live under `include/dandb/<module>/`.
- Implementations live under `src/<module>/`.
- Tests live under `tests/<module>/` and should mirror the module or concept they cover.
- Public project includes use angle brackets from the public include root, such as `<dandb/core/Status.h>`.
- Test-only helper includes use the test include root, such as `<testutil/TempDir.h>`.
- Avoid relative includes such as `../core/Status.h`.

## Naming

- Namespaces, paths, build targets, functions, methods, and local variables use `snake_case`, or lowercase for one-word names.
- Types use `PascalCase`, including classes, structs, enums, and type aliases.
- Enum values and named constructors or factory methods use `PascalCase`.
- Private data members use a trailing underscore, such as `code_` or `message_`.
- Constants use `UPPER_SNAKE_CASE`, such as `PAGE_SIZE` or `INVALID_PAGE_ID`.
- Public header filenames should match the main public type or concept they expose, such as `Status.h`, `TableId.h`, or `Checksum.h`.

## On-disk data

- Encode on-disk structures manually field by field.
- Do not write raw C++ structs directly to disk.
- On-disk formats must not depend on compiler padding, struct alignment, host endianness, or in-memory object layout.
