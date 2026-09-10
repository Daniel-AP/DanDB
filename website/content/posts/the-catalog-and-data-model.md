---
title: "The Catalog and Data Model"
weight: 4
---

The [previous post](/posts/from-sql-text-to-execution/) ends when DanDB has a bound statement. Table and column names have become stable IDs and ordinals, but execution still needs to know what those objects mean, where their data begins, and how a row is represented. The catalog and data model provide that information.

## Catalog

### Durable Catalog

The durable catalog is the stored source of this information. Its rows describe tables, columns, and indexes, including their stable IDs and B+ tree root pages. Because this representation lives in DanDB's system tables, it survives after the process closes and can be used to rebuild the catalog the next time the database opens.

### System Tables

DanDB stores its metadata in four system tables:

- `dandb_tables` records each table's ID, name, and root page
- `dandb_columns` records column ownership, order, types, and constraints
- `dandb_indexes` records index ownership, root pages, and index properties
- `dandb_index_columns` connects each index to the column it indexes

These are ordinary DanDB tables with schemas and B+ tree roots. Their rows use the same record encoding and page path as user tables. There is no second metadata file with a separate storage model.

Each table also has one internal primary-index descriptor. Its root is the same root recorded for the table because the table tree is organized by primary key. Secondary indexes have their own roots. `dandb_index_columns` stores one row for every column that belongs to an index. Its `ordinal` field means the column's position inside that index, not its position inside the table. DanDB currently supports only single-column indexes, so the only column in every index is at position zero. A composite index would instead need several rows for the same index, ordered as zero, one, and so on.

Names beginning with `dandb_` are reserved for this internal catalog. This prevents a user table or index from colliding with an object that DanDB needs to open the database. The complete system schemas and their fixed IDs remain in the [system-table reference](https://github.com/Daniel-AP/DanDB/blob/main/docs/system-tables.txt).

### In-Memory Catalog

The `Catalog` object is the runtime representation built from those stored rows. A `TableDescriptor` identifies a table by ID and name and records the root page of its table B+ tree. A `ColumnDescriptor` connects a column to its table and records its type, ordinal, nullability, and constraints. An `IndexDescriptor` identifies an index, its table, its indexed column, and its own root page.

Names are still useful at the SQL boundary, but the rest of DanDB should not have to keep resolving strings. `TableId`, `ColumnId`, and `IndexId` provide stable identities after binding. They are separate C++ types even though each wraps an integer, which prevents an ID from one category from being passed where another is expected. Column ordinals serve a different purpose. They locate values inside an ordered row.

For convenient access, `Catalog` groups each table descriptor with its columns and indexes. It also keeps schemas and lookup maps for questions that binding and execution ask repeatedly, such as finding a table by name or retrieving a schema by table ID. This representation exists only while the database is open. It is rebuilt from the durable catalog and does not replace it.

### Loading the Catalog

Loading begins with information that does not depend on catalog rows. The database header stores the root page ID for each of the four required system tables. DanDB also defines their canonical schemas in `SystemTables`. Together, those roots and schemas are enough for `CatalogLoader` to open the system B+ trees.

The loader reads tables first, then columns, index-to-column mappings, and indexes. This order follows the references between records. A column must point to a known table, and an index must point to both a known table and a known column. Once those pieces are present, columns are sorted by ordinal and converted into a `Schema` for each table.

Loading is also the point where stored metadata becomes trusted runtime state. DanDB checks that IDs and names are unique, references resolve, root pages fall inside the database page range, and column ordinals are contiguous from zero. It also verifies that a B+ tree entry key matches the primary key encoded in its row. Every table must resolve to exactly one primary index, and the system tables must match their canonical schemas and expected roots.

If one of those relationships is inconsistent, opening fails instead of constructing a partial catalog. This matters because every later binding and execution decision assumes that a descriptor, schema, and root page describe the same stored object.

After validation, the loader assembles the in-memory lookup structures. The next table, column, and index IDs are calculated from the largest IDs already present. DanDB does not need a separate persistent counter record for them.

### Transactional Catalog State

A catalog change must update two representations of the same database structure. `CREATE TABLE`, for example, writes metadata rows and allocates a table B+ tree through the pager. It also adds the corresponding descriptors, schema, and lookup entries to the in-memory catalog. If only the pages changed, the current catalog would not know that the table exists. If only the in-memory state changed, reopening the database would not find the metadata or storage it describes. Both representations therefore follow the same transaction outcome.

![Catalog state across a transaction](../../diagrams/catalog-transaction-state.svg)

*Catalog lookups prefer the staged state whenever a transaction has changed the catalog. Commit publishes that state, while rollback discards it together with the related page changes.*

`Catalog` keeps a committed state and, when a transaction changes the catalog, a staged state. Each catalog operation copies the state currently visible to that transaction and applies its change to the copy. The result stays staged until the transaction finishes. This also lets multiple catalog operations build on one another. Later statements use `visible_state()`, which prefers the staged copy when it exists. A table created earlier in the transaction can therefore be resolved and used before commit. A transaction that does not change the catalog has no staged state, so its lookups continue to use the committed state.

On commit, the pager commits the system-table page changes before `Catalog` promotes the staged state to committed. On rollback, the pager restores the page state before `Catalog` discards the staged copy. This ordering keeps the in-memory view aligned with the catalog stored through the pager.

## Data Model

### Types, Values, and Rows

Catalog metadata describes the shape of stored data. The record layer represents the data itself through a small set of types.

`LogicalType` describes one of DanDB's supported kinds: four widths of signed integer, double-precision floating point, fixed-capacity strings, or Boolean values. Every logical type reports a fixed storage size. For `STRING(N)`, that size is the declared byte capacity `N`.

`Value` holds one value together with its logical type. Integer widths remain distinct even though their C++ payload uses a common integer representation. A null value has no payload, but it still carries the logical type of its column. This lets validation and decoding preserve the row's type information without treating null as an untyped value.

`Row` is an ordered collection of values. It does not search for a value by column name. The column ordinal resolved during binding is the position used to read that value from the row. `Column` connects that position to a name, logical type, constraints, and byte offset.

### Schemas

`Schema` owns the ordered columns for one table and turns their declarations into a concrete row layout. Construction rejects an empty column list, duplicate names, a missing primary key, or more than one primary-key column. Column construction also prevents primary-key and unique columns from being nullable and rejects logical types that cannot serve as keys.

After validation, the schema assigns each column its ordinal and fixed offset. It reserves one bit per column in a null bitmap, rounds that bitmap up to complete bytes, then places every column's fixed-size region after it. The final offset is the size of every encoded row for that schema.

For the `users` table used in the earlier posts, three columns require a one-byte null bitmap. The `INT64` primary key begins at byte 1, `STRING(64)` begins at byte 9, and `BOOL` begins at byte 73. Every encoded row is therefore 74 bytes. Those values come from the schema rather than being calculated independently by each insert or read.

The schema is also the contract used to validate a row. The number and order of values must match the columns. Each value must have the expected logical type and string capacity, and null is accepted only for a nullable column.

### Row Encoding

Execution works with `Value` and `Row` objects. A table B+ tree stores byte sequences, so `RowCodec` converts between those two representations using the schema.

Encoding starts with a zero-filled vector whose size comes from `Schema::row_size`. Null values set their bits in the leading bitmap and leave their payload regions zero. Non-null values are written at the offsets calculated by the schema. Integers and doubles use their fixed widths, Boolean values use `0x00` or `0x01`, and strings use their declared region followed by zero padding when they are shorter than capacity.

Decoding performs the reverse conversion, but it does not accept every byte sequence of the correct length. It checks that unused null-bitmap bits are zero, null payload regions contain only zero bytes, Boolean values use one of the two valid encodings, and string padding is zero. These checks give each valid row one predictable representation and prevent malformed bytes from silently becoming runtime values.

The exact sizes, endianness, bitmap rules, and padding requirements are defined in the [file-format reference](https://github.com/Daniel-AP/DanDB/blob/main/docs/file-format.txt). The important boundary here is that the same schema controls both directions. A row cannot be interpreted correctly without the metadata that produced its layout.

### The Fixed-Size Trade-Off

DanDB's current row representation favors predictable layout. Once a schema is known, every column offset and the total row size are known as well. A table B+ tree can use one fixed value size, and row decoding does not need a directory of variable-length fields.

The cost is most visible with strings. A `STRING(64)` column reserves 64 bytes in every row even when a particular value contains only a few characters. Wider capacities increase every row by the same amount. The current format also assumes that a table's stored schema does not change, so schema evolution would require a way to distinguish or migrate different row layouts.

This is a local trade-off rather than a general rule for database storage. It keeps DanDB's initial record and page model direct, while accepting unused space and a fixed schema for each stored table.

### Row Bytes and Key Bytes

Row encoding preserves all column values so the complete row can be reconstructed. Key encoding has a different requirement. B+ tree keys are compared byte by byte, so their binary order must match the logical order used by indexed lookups and range scans.

That difference is clearest for signed integers. Their normal little-endian row representation does not sort correctly as raw bytes. `KeyCodec` flips the sign bit and writes integer keys in big-endian order, which places negative and positive values in their expected numeric order under bytewise comparison. Fixed-capacity strings keep their bytes followed by zero padding, and Boolean keys place false before true.

Keys cannot be null. `DOUBLE` values can be stored in rows, but DanDB does not currently accept them as index keys. Supporting floating-point ordering would require a defined sortable encoding and explicit decisions for cases such as signed zero and NaN.

At this boundary, the catalog has identified the table and its schema, the record layer has produced a row value, and the codecs have produced the byte sequences required by storage. The next layer is responsible for organizing those keys and rows inside table and index B+ trees.
